//------------------------------------------------------------------------------
/// \file   hidraw_device.c
/// \brief  HIDRAW device support
/// \author Steven Swann
//------------------------------------------------------------------------------
// Copyright 2014 Atmel Corporation. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
//    2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY ATMEL ''AS IS'' AND ANY EXPRESS OR IMPLIED
// WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
// EVENT SHALL ATMEL OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
// OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
// EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//------------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <endian.h>
#include <errno.h>
#include <dirent.h>

struct mxt_device;
struct mxt_conn_info;

#include "hidraw_device.h"
#include "libmaxtouch/libmaxtouch.h"

#define HIDRAW_CMD_ID             0x51

#define MXT_HID_READ_SUCCESS      0x04

#define MXT_HID_READ_DATA_SIZE    15
#define MXT_HID_WRITE_DATA_SIZE   12

#define MXT_HID_ADDR_SIZE         0x02

#define HIDRAW_WRITE_RETRY_DELAY_US     25000
#define HIDRAW_READ_RETRY_DELAY_US      250
#define HIDRAW_TIMEOUT_DELAY_US         500


struct hid_packet {
  uint8_t report_id;

  union {
    struct {
      uint8_t cmd;
      uint8_t rx_bytes;
      uint8_t tx_bytes;
      uint16_t address __attribute__((packed));
      uint8_t write_data[MXT_HID_WRITE_DATA_SIZE];
    } __attribute__((packed));
    struct {
      uint8_t result;
      uint8_t bytes_read;
      uint8_t read_data[MXT_HID_READ_DATA_SIZE];
    } __attribute__((packed));
    struct {
      uint8_t raw_data[18];
    } __attribute__((packed));
  };
} __attribute__((packed));

//******************************************************************************
/// \brief  Register hidraw device
/// \return #mxt_rc
int hidraw_register(struct mxt_device *mxt)
{
  mxt_info(mxt->ctx, "\nDevice registered on hidraw adapter:%s", mxt->conn->hidraw.node );

  return MXT_SUCCESS;
}

//******************************************************************************
/// \brief  Open the hidraw dev interface and set the slave address
/// \return #mxt_rc
static int hidraw_open(struct mxt_device *mxt)
{
  char filename[20];

  snprintf(filename, sizeof(filename), "%s", mxt->conn->hidraw.node);
  mxt->conn->hidraw.fd = open(filename, O_RDWR|O_NONBLOCK);
  if (mxt->conn->hidraw.fd < 0) {
    mxt_err(mxt->ctx, "Could not open %s, error %s (%d)",
            filename, strerror(errno), errno);
    return mxt_errno_to_rc(errno);
  }
  mxt_dbg(mxt->ctx, "Opened %s, fd: %d", filename, mxt->conn->hidraw.fd);

  return MXT_SUCCESS;
}

//******************************************************************************
/// \brief  Release device
void hidraw_release(struct mxt_device *mxt)
{
  return;
}

//******************************************************************************
/// \brief  Write packet to MXT chip
/// \return Number of databytes sent
static int hidraw_write_packet(struct mxt_device *mxt, struct hid_packet *write_pkt, uint8_t *byte_count)
{
  int ret;
  uint8_t pkt_size = write_pkt->rx_bytes + 4; /* allowing for header */

  if ((ret = write(mxt->conn->hidraw.fd, write_pkt, pkt_size)) != pkt_size) {
    mxt_verb(mxt->ctx, "HIDRAW retry");
    usleep(HIDRAW_WRITE_RETRY_DELAY_US);
    if ((ret = write(mxt->conn->hidraw.fd, &write_pkt, pkt_size)) != pkt_size) {
      mxt_err(mxt->ctx, "Error %s (%d) writing to hidraw",
              strerror(errno), errno);
      ret = mxt_errno_to_rc(errno);
    }
  }

  mxt_log_buffer(mxt->ctx, LOG_VERBOSE, "PKT TX:",
                 (const unsigned char *) write_pkt, pkt_size);

  *byte_count = ret - 6;

  mxt_dbg(mxt->ctx, "Sending packet: size: %d No. data bytes TX: %d",
          pkt_size, *byte_count);

  return MXT_SUCCESS;
}

//******************************************************************************
/// \brief  Write read command packet to MXT chip
/// \return #mxt_rc
static int hidraw_write_read_cmd(struct mxt_device *mxt, uint16_t start_register, uint8_t count, uint8_t *byte_count)
{
  struct hid_packet cmd_pkt = { 0 };

  cmd_pkt.report_id = mxt->conn->hidraw.report_id;
  cmd_pkt.cmd = 0x51;
  cmd_pkt.rx_bytes = 2;      /* includes start address word */
  cmd_pkt.tx_bytes = count;
  cmd_pkt.address = htole16(start_register);

  mxt_dbg(mxt->ctx, "Sending read command");

  return hidraw_write_packet(mxt, &cmd_pkt, byte_count);
}

//******************************************************************************
/// \brief  Read packet from MXT chip
/// \return #mxt_rc
static int hidraw_read_response(struct mxt_device *mxt, struct hid_packet *read_pkt,
                                size_t count)
{
  ssize_t ret = 0;
  size_t t_count = 0;
  int timeout = 0;
  const int max_timeout = HIDRAW_TIMEOUT_DELAY_US * 1000 / HIDRAW_READ_RETRY_DELAY_US; // Convert to number of iterations

  do {
    ret = read(mxt->conn->hidraw.fd, (uint8_t *)read_pkt + t_count, count - t_count);
    
    if (ret > 0) {
      t_count += (size_t)ret;
      if (t_count >= count) {
        break; // Got all the data
      }
    } else if (ret < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Non-blocking read, no data available yet
        ret = 0;
      } else {
        mxt_err(mxt->ctx, "Error %s (%d) reading from hidraw",
                strerror(errno), errno);
        return mxt_errno_to_rc(errno);
      }
    } else {
      // ret == 0, EOF
      mxt_err(mxt->ctx, "Unexpected EOF reading from hidraw");
      return MXT_ERROR_IO;
    }
    
    if (t_count < count) {
      usleep(HIDRAW_READ_RETRY_DELAY_US);
    }
  } while (timeout++ < max_timeout && t_count < count);

  if (t_count < count) {
    mxt_err(mxt->ctx, "Timeout reading from hidraw: requested %zu bytes, got %zu",
            count, t_count);
    return MXT_ERROR_TIMEOUT;
  }

  mxt_dbg(mxt->ctx, "No. bytes requested: %zu, No. of bytes read: %zu",
          count, t_count);
  mxt_log_buffer(mxt->ctx, LOG_VERBOSE, "RD PKT RX:",
                 (const unsigned char *) read_pkt, count);
  return MXT_SUCCESS;
}

//******************************************************************************
/// \brief  Read packet from MXT chip
/// \return #mxt_rc
static int hidraw_read_packet(struct mxt_device *mxt, struct hid_packet *read_pkt,
                              uint16_t start_register, size_t count)
{
  int pkt_count = count + 3;
  int ret = 0;
  uint8_t byte_count = 0;

  ret = hidraw_write_read_cmd(mxt, start_register, count, &byte_count);
  if (ret)
    return ret;

  ret = hidraw_read_response(mxt, read_pkt, pkt_count);
  if (ret < 0)
    return ret;

  return ret - 3;
}

//******************************************************************************
/// \brief  Read register from MXT chip
/// \return #mxt_rc
int hidraw_read_register(struct mxt_device *mxt, unsigned char *buf,
                         uint16_t start_register, size_t count,
                         size_t *bytes_transferred)
{

  int ret;
  struct hid_packet read_pkt = { 0 };

  mxt_dbg(mxt->ctx, "%s - start_register:%d No. bytes requested:%zu",
          __func__, start_register, count);

  ret = hidraw_open(mxt);
  if (ret)
    return ret;

  size_t bytes_read = 0;
  while (bytes_read < count) {
    ret = hidraw_read_packet(mxt, &read_pkt,
                             start_register + bytes_read,
                             (count - bytes_read <= MXT_HID_READ_DATA_SIZE ? count - bytes_read : MXT_HID_READ_DATA_SIZE));
    if (ret < 0) {
      mxt_dbg(mxt->ctx, "read error %s (%d)", strerror(errno), errno);
      ret = mxt_errno_to_rc(errno);
    }

    memcpy(buf + bytes_read, read_pkt.read_data, ret);

    bytes_read += ret;
  }

  *bytes_transferred = bytes_read;

  close(mxt->conn->hidraw.fd);
  mxt->conn->hidraw.fd = 0;
  return MXT_SUCCESS;
}

//******************************************************************************
/// \brief  Write register from MXT chip
/// \return #mxt_rc
int hidraw_write_register(struct mxt_device *mxt, unsigned char const *val, uint16_t start_register, int datalength)
{
  int ret;
  uint8_t byte_count;
  struct hid_packet write_pkt;
  memset(&write_pkt, 0x00, sizeof(struct hid_packet));

  write_pkt.report_id = mxt->conn->hidraw.report_id;
  write_pkt.cmd = 0x51;
  write_pkt.tx_bytes = 0;

  mxt_dbg(mxt->ctx, "%s - start_register:%d No. bytes to write:%d",
          __func__, start_register, datalength);

  ret = hidraw_open(mxt);
  if (ret)
    return ret;

  int bytes_written = 0;

  while (bytes_written < datalength) {
    write_pkt.rx_bytes = MXT_HID_ADDR_SIZE +
                         (datalength - bytes_written <= MXT_HID_WRITE_DATA_SIZE ? datalength - bytes_written : MXT_HID_WRITE_DATA_SIZE);
    write_pkt.address = htole16(start_register + bytes_written);
    memcpy(write_pkt.write_data, val + bytes_written, write_pkt.rx_bytes);

    ret =  hidraw_write_packet(mxt, &write_pkt, &byte_count);
    if (ret) {
      mxt_err(mxt->ctx, "read error %s (%d)", strerror(errno), errno);
      ret = mxt_errno_to_rc(errno);
      goto close;
    }
    bytes_written += byte_count;
    mxt_dbg(mxt->ctx, "Bytes Written:%d", bytes_written);

    struct hid_packet response_pkt;
    memset(&response_pkt, 0x00, sizeof(struct hid_packet));

    ret = hidraw_read_response(mxt, &response_pkt, 2);
    if (response_pkt.result != MXT_HID_READ_SUCCESS)
      mxt_err(mxt->ctx, "HIDRAW write failed: 0x%x",
              response_pkt.result);
  }

close:
  close(mxt->conn->hidraw.fd);
  mxt->conn->hidraw.fd = 0;
  return MXT_SUCCESS;
}

//******************************************************************************
/// \brief  Scan for hidraw devices
/// \return #mxt_rc
int hidraw_scan(struct libmaxtouch_ctx *ctx, struct mxt_conn_info **conn)
{
  DIR *dir;
  struct dirent *entry;
  struct mxt_conn_info *curr_conn = *conn;
  struct mxt_conn_info *new_conn;
  int ret;
  char dev_path[256];
  struct stat st;

  dir = opendir("/dev");
  if (!dir) {
    mxt_dbg(ctx, "Could not open /dev directory");
    return MXT_ERROR_NO_DEVICE;
  }

  while ((entry = readdir(dir)) != NULL) {
    /* Check if entry starts with "hidraw" */
    if (strncmp(entry->d_name, "hidraw", 6) != 0)
      continue;

    snprintf(dev_path, sizeof(dev_path), "/dev/%s", entry->d_name);

    /* Check if device exists and is a character device */
    if (stat(dev_path, &st) != 0)
      continue;

    if (!S_ISCHR(st.st_mode))
      continue;

    /* If a specific device was requested, check if it matches */
    if (curr_conn && curr_conn->type == E_HIDRAW && curr_conn->hidraw.node[0] != '\0') {
      if (strcmp(curr_conn->hidraw.node, dev_path) != 0)
        continue;
    }

    ctx->scan_count++;

    if (ctx->query) {
      printf("hidraw:%s\n", dev_path);
    } else {
      ret = mxt_new_conn(&new_conn, E_HIDRAW);
      if (ret) {
        closedir(dir);
        return ret;
      }

      snprintf(new_conn->hidraw.node, sizeof(new_conn->hidraw.node), "%s", dev_path);
      new_conn->hidraw.report_id = HIDRAW_REPORT_ID;

      mxt_verb(ctx, "Found hidraw device: %s", dev_path);

      *conn = new_conn;
      closedir(dir);
      return MXT_SUCCESS;
    }
  }

  closedir(dir);

  if (ctx->query && ctx->scan_count > 0)
    return MXT_SUCCESS;

  return MXT_ERROR_NO_DEVICE;
}
