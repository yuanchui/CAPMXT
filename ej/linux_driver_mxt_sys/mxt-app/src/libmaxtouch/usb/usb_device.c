//------------------------------------------------------------------------------
/// \file   usb_device.c
/// \brief  MXT device low level access via USB
/// \author Tim Culmer
//------------------------------------------------------------------------------
// Copyright 2011 Atmel Corporation. All rights reserved.
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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>

#include "libmaxtouch/log.h"
#include "libmaxtouch/libmaxtouch.h"
#include "libmaxtouch/info_block.h"
#include "usb_device.h"

/* USB device configuration */
#define VENDOR_ID    0x03EB
#define ENDPOINT_1_IN  0x81
#define ENDPOINT_2_OUT 0x02

/* timeout in ms */
#define USB_TRANSFER_TIMEOUT 2000

#define REPORT_ID            0x01
#define IIC_DATA_1           0x51
#define CMD_READ_PINS        0x82
#define CMD_CONFIG           0x80
#define CMD_CONFIG_I2C_RETRY_ON_NAK (1 << 7)
#define CMD_FIND_IIC_ADDRESS 0xE0
#define CMD_FAST_MODE        0xFA
#define CMD_PARALLEL_MODE    0xFE
#define SAVE_CONFIGS_EEPROM   0xEA

/* mXT command status codes */
#define COMMS_STATUS_OK          0x00
#define COMMS_STATUS_DATA_NACK   0x01
#define COMMS_STATUS_ADDR_NACK   0x01
#define COMMS_STATUS_WRITE_OK    0x04

//******************************************************************************
/// \brief Converts a libusb error code into a string
/// \return Error string
static const char *usb_error_name(int errcode)
{
  switch (errcode) {
  case LIBUSB_SUCCESS:
    return "LIBUSB_SUCCESS";
  case LIBUSB_ERROR_IO:
    return "LIBUSB_ERROR_IO";
  case LIBUSB_ERROR_INVALID_PARAM:
    return "LIBUSB_ERROR_INVALID_PARAM";
  case LIBUSB_ERROR_ACCESS:
    return "LIBUSB_ERROR_ACCESS";
  case LIBUSB_ERROR_NO_DEVICE:
    return "LIBUSB_ERROR_NO_DEVICE";
  case LIBUSB_ERROR_NOT_FOUND:
    return "LIBUSB_ERROR_NOT_FOUND";
  case LIBUSB_ERROR_BUSY:
    return "LIBUSB_ERROR_BUSY";
  case LIBUSB_ERROR_TIMEOUT:
    return "LIBUSB_ERROR_TIMEOUT";
  case LIBUSB_ERROR_OVERFLOW:
    return "LIBUSB_ERROR_OVERFLOW";
  case LIBUSB_ERROR_PIPE:
    return "LIBUSB_ERROR_PIPE";
  case LIBUSB_ERROR_INTERRUPTED:
    return "LIBUSB_ERROR_INTERRUPTED";
  case LIBUSB_ERROR_NO_MEM:
    return "LIBUSB_ERROR_NO_MEM";
  case LIBUSB_ERROR_NOT_SUPPORTED:
    return "LIBUSB_ERROR_NOT_SUPPORTED";
  case LIBUSB_ERROR_OTHER:
    return "LIBUSB_ERROR_OTHER";
  default:
    return "unrecognised error code";
  }
}

//******************************************************************************
/// \brief Convert errors from the libusb API to #mxt_rc values
/// \return #mxt_rc
static int usberror_to_rc(int errcode)
{
  switch (errcode) {
  case LIBUSB_SUCCESS:
    return MXT_SUCCESS;

  case LIBUSB_ERROR_NO_DEVICE:
    return MXT_ERROR_NO_DEVICE;

  case LIBUSB_ERROR_ACCESS:
    return MXT_ERROR_ACCESS;

  case LIBUSB_ERROR_NO_MEM:
    return MXT_ERROR_NO_MEM;

  case LIBUSB_ERROR_TIMEOUT:
    return MXT_ERROR_TIMEOUT;

  case LIBUSB_ERROR_INVALID_PARAM:
    return MXT_INTERNAL_ERROR;

  case LIBUSB_ERROR_INTERRUPTED:
    return MXT_ERROR_INTERRUPTED;

  case LIBUSB_ERROR_NOT_SUPPORTED:
    return MXT_ERROR_NOT_SUPPORTED;

  case LIBUSB_ERROR_IO:
  case LIBUSB_ERROR_NOT_FOUND:
  case LIBUSB_ERROR_BUSY:
  case LIBUSB_ERROR_OVERFLOW:
  case LIBUSB_ERROR_PIPE:
  case LIBUSB_ERROR_OTHER:
  default:
    return MXT_ERROR_IO;
  }
}
//******************************************************************************
/// \brief  Read a packet of data from the MXT chip
/// \return #mxt_rc
static int usb_transfer(struct mxt_device *mxt, void *cmd, int cmd_size,
                        void *response, int response_size, bool ignore_response)
{
  int ret;
  int bytes_transferred;
  uint8_t out_endpoint = ENDPOINT_2_OUT;
  bool is_stm32 = (mxt->usb.desc.idVendor == 0x0483 && mxt->usb.desc.idProduct == 0x5740);

  // 针对 STM32 CDC 桥接器，数据输出端点是 0x01
  if (is_stm32) {
    out_endpoint = 0x01;
  }

  /* Send command to request read */
  if (is_stm32) {
    ret = libusb_bulk_transfer(mxt->usb.handle, out_endpoint, cmd,
                               cmd_size, &bytes_transferred, USB_TRANSFER_TIMEOUT);
  } else {
    ret = libusb_interrupt_transfer(mxt->usb.handle, out_endpoint, cmd,
                                    cmd_size, &bytes_transferred, USB_TRANSFER_TIMEOUT);
  }

  if (ret != LIBUSB_SUCCESS) {
    mxt_err(mxt->ctx, "USB command error %s", usb_error_name(ret));
    return usberror_to_rc(ret);
  } else if (bytes_transferred != cmd_size) {
    mxt_err
    (
      mxt->ctx,
      "Read request failed - %d bytes transferred, returned %s",
      bytes_transferred, usb_error_name(ret)
    );
    return MXT_ERROR_IO;
  } else {
    mxt_log_buffer(mxt->ctx, LOG_VERBOSE, "TX:", cmd, cmd_size);
  }

  if (ignore_response) {
    mxt_verb(mxt->ctx, "Ignoring response command");
    return MXT_SUCCESS;
  }

  /* Read response from read request */
  if (is_stm32) {
    ret = libusb_bulk_transfer(mxt->usb.handle, ENDPOINT_1_IN, response,
                               response_size, &bytes_transferred, USB_TRANSFER_TIMEOUT);
  } else {
    ret = libusb_interrupt_transfer(mxt->usb.handle, ENDPOINT_1_IN, response,
                                    response_size, &bytes_transferred, USB_TRANSFER_TIMEOUT);
  }

  if (ret != LIBUSB_SUCCESS) {
    mxt_err(mxt->ctx, "USB response error %s", usb_error_name(ret));
    return usberror_to_rc(ret);
  } else if (bytes_transferred < response_size && !is_stm32) {
    mxt_err
    (
      mxt->ctx,
      "Read response failed - %d bytes transferred, returned %s",
      bytes_transferred, usb_error_name(ret)
    );
    return MXT_ERROR_IO;
  } else {
    mxt_log_buffer(mxt->ctx, LOG_VERBOSE, "RX:", response, bytes_transferred);
  }

  return MXT_SUCCESS;
}

//******************************************************************************
/// \brief  Read register from MXT chip
/// \return #mxt_rc
int usb_read_register(struct mxt_device *mxt, unsigned char *buf,
                      uint16_t start_register, size_t count,
                      size_t *bytes_transferred)
{
  unsigned char pkt[mxt->usb.ep1_in_max_packet_size];
  size_t cmd_size;
  size_t max_count;
  off_t response_ofs;
  int ret;

  /* Check a device is present before trying to read from it */
  if (!mxt->usb.device_connected) {
    mxt_err(mxt->ctx, "Device uninitialised");
    return MXT_ERROR_NO_DEVICE;
  }

  memset(&pkt, 0, sizeof(pkt));

  /* Command packet */
  if (mxt->usb.bridge_chip) {
    cmd_size = 5;
    max_count = mxt->usb.ep1_in_max_packet_size - cmd_size;

    if (count > max_count)
      count = max_count;

    pkt[0] = IIC_DATA_1;
    pkt[1] = 2;
    pkt[2] = count;
    pkt[3] = start_register & 0xFF;
    pkt[4] = (start_register & 0xFF00) >> 8;

    response_ofs = 0;
  } else {
    cmd_size = 6;
    max_count = mxt->usb.ep1_in_max_packet_size - cmd_size;

    if (count > max_count)
      count = max_count;

    pkt[0] = REPORT_ID;
    pkt[1] = IIC_DATA_1;
    pkt[2] = 2;
    pkt[3] = count;
    pkt[4] = start_register & 0xFF;
    pkt[5] = (start_register & 0xFF00) >> 8;

    response_ofs = 1;
  }

  mxt_verb(mxt->ctx, "Reading %" PRIuPTR " bytes starting from address %d",
           count, start_register);

  /* Command packet */

  ret = usb_transfer(mxt, &pkt, cmd_size, &pkt, sizeof(pkt), false);
  if (ret)
    return ret;

  /* Check the result in the response */
  if (pkt[response_ofs] != COMMS_STATUS_OK) {
    mxt_err
    (
      mxt->ctx,
      "Wrong result in read response - expected 0x%02X got 0x%02X",
      COMMS_STATUS_OK, pkt[response_ofs]
    );
    return MXT_ERROR_IO;
  }

  /* Output the data read from the registers */
  (void)memcpy(buf, &pkt[response_ofs + 2], count);

  *bytes_transferred = count;
  return MXT_SUCCESS;
}

//******************************************************************************
/// \brief  Write a packet of data to the MXT chip
/// \return #mxt_rc
static int write_data(struct mxt_device *mxt, unsigned char const *buf,
                      uint16_t start_register, size_t count,
                      int *bytes_written, bool ignore_response)
{
  unsigned char pkt[mxt->usb.ep1_in_max_packet_size];
  int ret;
  size_t max_count;
  size_t cmd_size;
  int packet_size;
  off_t response_ofs;

  /* Check a device is present before trying to write to it */
  if (!mxt->usb.device_connected) {
    mxt_err(mxt->ctx, "Device uninitialised");
    return MXT_ERROR_NO_DEVICE;
  }

  memset(&pkt, 0, sizeof(pkt));

  /* Command packet */
  if (mxt->usb.bridge_chip) {
    cmd_size = 5;
    max_count = mxt->usb.ep1_in_max_packet_size - cmd_size;

    if (count > max_count)
      count = max_count;

    pkt[0] = IIC_DATA_1;
    pkt[1] = 2 + count;
    pkt[2] = 0;
    pkt[3] = start_register & 0xFF;
    pkt[4] = (start_register & 0xFF00) >> 8;

    response_ofs = 0;
  } else {
    cmd_size = 6;
    max_count = mxt->usb.ep1_in_max_packet_size - cmd_size;

    if (count > max_count)
      count = max_count;

    pkt[0] = REPORT_ID;
    pkt[1] = IIC_DATA_1;
    pkt[2] = 2 + count;
    pkt[3] = 0;
    pkt[4] = start_register & 0xFF;
    pkt[5] = (start_register & 0xFF00) >> 8;

    response_ofs = 1;
  }

  packet_size = cmd_size + count;

  (void)memcpy(pkt + cmd_size, buf, count);

  mxt_verb(mxt->ctx, "Writing %" PRIuPTR " bytes to address %d",
           count, start_register);

  ret = usb_transfer(mxt, pkt, packet_size, pkt, sizeof(pkt), ignore_response);
  if (ret)
    return ret;

  /* Check the result in the response */
  if (!ignore_response && pkt[response_ofs] != COMMS_STATUS_WRITE_OK) {
    mxt_err
    (
      mxt->ctx,
      "Wrong result in write response - expected 0x%02X got 0x%02X",
      COMMS_STATUS_WRITE_OK, pkt[response_ofs]
    );
    return MXT_ERROR_IO;
  }

  *bytes_written = count;
  return MXT_SUCCESS;
}

//******************************************************************************
/// \brief Try to find descriptor of QRG interface
/// \return #mxt_rc
static int usb_scan_for_qrg_if(struct mxt_device *mxt)
{
  int ret;
  char buf[128];
  const char qrg_if[] = "QRG-I/F";

  ret = libusb_get_string_descriptor_ascii(mxt->usb.handle,
        mxt->usb.desc.iProduct, (unsigned char *)buf, sizeof(buf));
  if (ret < 0)
    return usberror_to_rc(ret);

  if (!strncmp(buf, qrg_if, sizeof(qrg_if))) {
    mxt_verb(mxt->ctx, "Found %s", qrg_if);
    mxt->usb.interface = 0;
    return MXT_SUCCESS;
  } else {
    return MXT_ERROR_NO_DEVICE;
  }
}

//******************************************************************************
/// \brief  Try to find control interface
/// \return #mxt_rc
static int usb_scan_for_control_if(struct mxt_device *mxt,
                                   struct libusb_config_descriptor *config)
{
  int j, k, ret;
  char buf[128];
  const char control_if[] = "Atmel maXTouch Control";
  const char bootloader_if[] = "Atmel maXTouch Bootloader";
  const char bootloader_if_b[] = "Atmel_maXTouch_Bootloader";

  for (j = 0; j < config->bNumInterfaces; j++) {
    const struct libusb_interface *interface = &config->interface[j];
    for (k = 0; k < interface->num_altsetting; k++) {
      const struct libusb_interface_descriptor *altsetting = &interface->altsetting[k];

      if (altsetting->iInterface >= 0) {

         ret = libusb_get_string_descriptor_ascii(mxt->usb.handle,
              altsetting->iInterface, (unsigned char *)buf, sizeof(buf));

        if (ret > 0) {
          if (!strncmp(buf, control_if, sizeof(control_if))) {
            mxt_verb(mxt->ctx, "Found %s at interface %d altsetting %d",
                buf, altsetting->bInterfaceNumber, altsetting->bAlternateSetting);

            mxt->usb.bootloader = false;
            mxt->usb.interface = altsetting->bInterfaceNumber;
            return MXT_SUCCESS;
          } else if (!strncmp(buf, bootloader_if, sizeof(bootloader_if))) {
            mxt_verb(mxt->ctx, "Found %s at interface %d altsetting %d",
                    buf, altsetting->bInterfaceNumber, altsetting->bAlternateSetting);
      
            mxt->usb.bootloader = true;
            mxt->usb.interface = altsetting->bInterfaceNumber;
            return MXT_SUCCESS;
          } 
        } else {
            ret = libusb_get_string_descriptor_ascii(mxt->usb.handle,
                mxt->usb.desc.iProduct, (unsigned char *)buf, sizeof(buf));

            if (ret > 0) {
                if (!strncmp(buf, bootloader_if_b, sizeof(bootloader_if_b))) {
                  mxt_verb(mxt->ctx, "Found %s at interface %d altsetting %d",
                      buf, altsetting->bInterfaceNumber, altsetting->bAlternateSetting);

                  mxt->usb.bootloader = true;
                  mxt->usb.interface = altsetting->bInterfaceNumber;
                  return MXT_SUCCESS;

                } else {
                  mxt_verb(mxt->ctx, "Ignoring %s at interface %d altsetting %d",
                      buf, altsetting->bInterfaceNumber, altsetting->bAlternateSetting);
                }
            }
        }
      }
    }
  }

  return MXT_ERROR_NO_DEVICE;
}

//******************************************************************************
/// \brief  Device is bootloader
/// \return true or false
bool usb_is_bootloader(struct mxt_device *mxt)
{
  return mxt->usb.bootloader;
}

//******************************************************************************
/// \brief  Scan configurations
/// \return #mxt_rc
static int usb_scan_device_configs(struct mxt_device *mxt)
{
  int i, ret;

  // 新增：针对 STM32 桥接器直接指定接口，跳过字符串描述符检查
  if (mxt->usb.desc.idVendor == 0x0483 && mxt->usb.desc.idProduct == 0x5740) {
    mxt->usb.interface = 1;  // CDC Data Interface 1
    return MXT_SUCCESS;
  }

  if (mxt->usb.desc.idProduct != 0x2119) {
    if (mxt->usb.bridge_chip && mxt->usb.desc.bNumConfigurations == 1) {
      return usb_scan_for_qrg_if(mxt);
    }
  }

  /* Scan through interfaces */
  for (i = 0; i < mxt->usb.desc.bNumConfigurations; ++i) {
    struct libusb_config_descriptor *config;
    ret = libusb_get_config_descriptor(mxt->usb.device, i, &config);
    if (ret < 0) {
      mxt_err(mxt->ctx, "Couldn't get config descriptor %d", i);
    } else {
      ret = usb_scan_for_control_if(mxt, config);
      libusb_free_config_descriptor(config);
      if (ret == MXT_SUCCESS)
        // Found interface number
        return ret;
    }
  }

  return MXT_ERROR_NO_DEVICE;
}

//******************************************************************************
/// \brief  Switch USB5030 to USB FS Bridge mode
/// \return #mxt_rc
static int bridge_set_fs_mode(struct mxt_device *mxt)
{
  unsigned char pkt[mxt->usb.ep1_in_max_packet_size];
  int ret;

  memset(&pkt, 0, sizeof(pkt));
  pkt[0] = 0;
  pkt[1] = 0xFA;
  pkt[2] = 0xE7;

  ret = usb_transfer(mxt, &pkt, sizeof(pkt), &pkt, sizeof(pkt), true);
  if (ret)
    return ret;

  libusb_reset_device(mxt->usb.handle);

  return MXT_SUCCESS;
}

//******************************************************************************
/// \brief  Set the parameters for the comms mode on USB5030
/// \return #mxt_rc
int bridge_configure(struct mxt_device *mxt)
{
  unsigned char pkt[mxt->usb.ep1_in_max_packet_size];
  int buf = 0;

  if (mxt->usb.address == 0x4a && mxt->usb.sent_btlr_cmd == true) {
    buf = (CMD_CONFIG_I2C_RETRY_ON_NAK | 0x26);
  } else if (mxt->usb.address == 0x4b && mxt->usb.sent_btlr_cmd == true) {
    buf = (CMD_CONFIG_I2C_RETRY_ON_NAK | 0x27);
  } else {
    buf = CMD_CONFIG_I2C_RETRY_ON_NAK;
  }

  if (mxt->conn->usb.b_i2c_addr != 0x00)
    buf = mxt->conn->usb.b_i2c_addr;

  /* Command packet */
  memset(&pkt, 0, sizeof(pkt));
  pkt[0] = CMD_CONFIG;
  /* 200kHz */
  pkt[1] = 0x30;
  pkt[2] = buf;
  pkt[3] = 0x00;
  pkt[4] = 0x80;
  pkt[5] = 0x00;
  pkt[6] = 0x00;
  pkt[7] = 0xC8;
  pkt[8] = 0x11;
  /* I2C retry delay */
 // pkt[5] = 25 * 8;

  mxt_verb(mxt->ctx, "Sending CMD_CONFIG");

  return usb_transfer(mxt, &pkt, sizeof(pkt), &pkt, sizeof(pkt), false);
}

//******************************************************************************
/// \brief  Save config parameters
/// \return #mxt_rc
static int bridge_save_config(struct mxt_device *mxt)
{
  unsigned char pkt[mxt->usb.ep1_in_max_packet_size];

  /* Command packet */
  memset(&pkt, 0, sizeof(pkt));
  pkt[0] = SAVE_CONFIGS_EEPROM;

  mxt_verb(mxt->ctx, "Saving config parameters");

  return usb_transfer(mxt, &pkt, sizeof(pkt), &pkt, sizeof(pkt), false);
}

//******************************************************************************
/// \brief Hunt for I2C device using bridge chip
/// \return #mxt_rc
static int bridge_find_i2c_address(struct mxt_device *mxt)
{
  int ret;
  unsigned char pkt[mxt->usb.ep1_in_max_packet_size];
  unsigned char response;

  /* Command packet */
  memset(&pkt, 0, sizeof(pkt));
  pkt[0] = CMD_FIND_IIC_ADDRESS;

  mxt_verb(mxt->ctx, "Sending CMD_FIND_IIC_ADDRESS");

  ret = usb_transfer(mxt, &pkt, sizeof(pkt), &pkt, sizeof(pkt), false);
  if (ret)
    return ret;

  response = pkt[1];

  if (response == 0x81) {
    mxt_err(mxt->ctx, "No device found by bridge chip");
    return MXT_ERROR_NO_DEVICE;
  } else {
    if (response < 0x4a) {
      mxt->usb.bootloader = true;
      mxt_info(mxt->ctx, "\nBridge found bootloader at 0x%02X", response);
    } else {
      mxt->usb.address = response;
      mxt_info(mxt->ctx, "\nBridge found control interface at 0x%02X", response);
    }

    return MXT_SUCCESS;
  }
}

//******************************************************************************
/// \brief  Find device by bus/number
/// \return #mxt_rc
static int usb_find_device(struct libmaxtouch_ctx *ctx, struct mxt_device *mxt)
{
  int ret, count, i;
  struct libusb_device **devs;
  int usb_bus, usb_device;

  count = libusb_get_device_list(ctx->usb.libusb_ctx, &devs);
  if (count <= 0) {
    mxt_err(mxt->ctx, "%s enumerating devices", usb_error_name(count));
    return usberror_to_rc(count);
  }

  for (i = 0; i < count; i++) {
    struct libusb_device_descriptor desc;
    ret = libusb_get_device_descriptor(devs[i], &desc);
    if (ret != LIBUSB_SUCCESS) {
      mxt_warn(mxt->ctx, "%s trying to retrieve descriptor",
               usb_error_name(ret));
      continue;
    }

    usb_bus = libusb_get_bus_number(devs[i]);
    usb_device = libusb_get_device_address(devs[i]);

     if (mxt->conn->usb.bus == usb_bus && mxt->conn->usb.device == usb_device) {
      if (desc.idProduct == 0x6123 || desc.idProduct == 0x2119) {

        mxt->usb.bridge_chip = true;
        mxt_verb(mxt->ctx, "Found usb:%03d-%03d 5030 bridge chip",
                usb_bus, usb_device);
      } else if (desc.idVendor == 0x0483 && desc.idProduct == 0x5740) {
        mxt->usb.bridge_chip = false;
        mxt_verb(mxt->ctx, "Found usb:%03d-%03d STM32 Virtual Bridge",
                 usb_bus, usb_device);
      } else {
        mxt->usb.bridge_chip = false;
        mxt_verb(mxt->ctx, "Found usb:%03d-%03d VID=%04X PID=%04X",
                 usb_bus, usb_device,
                 desc.idVendor, desc.idProduct);
      }

      mxt->usb.device = devs[i];
      libusb_ref_device(mxt->usb.device);
      mxt->usb.desc = desc;
      ret = MXT_SUCCESS;
      goto free_device_list;
    } else {
      mxt_verb(mxt->ctx, "Ignoring usb:%03d-%03d VID=%04X PID=%04X",
               usb_bus, usb_device,
               desc.idVendor, desc.idProduct);
    }
  }

  mxt_err(mxt->ctx, "未找到设备 usb:%03d-%03d",
          mxt->conn->usb.bus, mxt->conn->usb.device);

  ret = MXT_ERROR_NO_DEVICE;

free_device_list:
  libusb_free_device_list(devs, 1);
  return ret;
}

//******************************************************************************
/// \brief  Initialise library if necessary
/// \return #mxt_rc
static int usb_initialise_libusb(struct libmaxtouch_ctx *ctx)
{
  int ret;

  /* Skip if already initialised */
  if (ctx->usb.libusb_ctx)
    return MXT_SUCCESS;

  /* Initialise library */
  ret = libusb_init(&(ctx->usb.libusb_ctx));
  if (ret != LIBUSB_SUCCESS) {
    mxt_err(ctx, "%s 初始化 libusb 失败", usb_error_name(ret));
    return usberror_to_rc(ret);
  }

  mxt_verb(ctx, "已初始化 libusb");

  /* Set the debug level for the library */
  if (mxt_get_log_level(ctx) < LOG_DEBUG) {
    mxt_dbg(ctx, "Enabling libusb debug");
    /* Level 3: informational messages are printed to stdout, warning and
     * error messages are printed to stderr */
    libusb_set_debug(ctx->usb.libusb_ctx, 3);
  }

  return MXT_SUCCESS;
}

//******************************************************************************
/// \brief  Try to connect device
/// \return #mxt_rc
int usb_open(struct mxt_device *mxt)
{
  int ret;
  unsigned int tries = 5;

  ret = usb_initialise_libusb(mxt->ctx);
  if (ret)
    return ret;

  ret = usb_find_device(mxt->ctx, mxt);
  if (ret != 0)
    return MXT_ERROR_NO_DEVICE;

retry:
  ret = libusb_open(mxt->usb.device, &mxt->usb.handle);
  if (ret == LIBUSB_ERROR_NO_DEVICE) {
    usleep(500000);
    if (tries--) {
      mxt_warn(mxt->ctx, "%s opening USB device, retrying", usb_error_name(ret));
      goto retry;
    } else {
      mxt_err(mxt->ctx, "%s opening USB device", usb_error_name(ret));
      return usberror_to_rc(ret);
    }
  } else if (ret != LIBUSB_SUCCESS) {
    mxt_err(mxt->ctx, "%s opening USB device", usb_error_name(ret));
#ifdef MXT_OS_WINDOWS
    if (ret == LIBUSB_ERROR_NOT_SUPPORTED || ret == LIBUSB_ERROR_ACCESS) {
      mxt_err(mxt->ctx,
              "该 USB 设备未安装 WinUSB/libusb 兼容驱动。\n"
              "请使用 Zadig (https://zadig.akeo.ie/) 为该设备安装 WinUSB 驱动：\n"
              "  1. 打开 Zadig\n"
              "  2. 在 Options 菜单中勾选 \"List All Devices\"\n"
              "  3. 从下拉列表中选择该设备（VID:PID 或设备名称）\n"
              "  4. 选择 \"WinUSB\" 驱动\n"
              "  5. 点击 \"Install Driver\" 或 \"Replace Driver\"\n"
              "  6. 安装完成后重新运行 mxt-app");
    }
#endif
    return usberror_to_rc(ret);
  }

  mxt->usb.interface = -1;

  ret = usb_scan_device_configs(mxt);
  if (ret) {
    mxt_warn(mxt->ctx, "未找到控制接口(Control interface)");
    return ret;
  }

  /* Disconnect the kernel driver if it is active */
  if (libusb_kernel_driver_active(mxt->usb.handle, mxt->usb.interface) == 1) {
    mxt_verb(mxt->ctx, "内核驱动已激活 - 在声明(Claim)该接口前必须先解绑(Detach)");

    if (libusb_detach_kernel_driver(mxt->usb.handle, mxt->usb.interface) == 0) {
      mxt_verb(mxt->ctx, "已解绑(Detach)内核驱动");
    }
  }

  /* Claim the bInterfaceNumber 1 of the device */
  ret = libusb_claim_interface(mxt->usb.handle, mxt->usb.interface);
  if (ret != LIBUSB_SUCCESS) {
    mxt_err
    (
      mxt->ctx,
      "无法声明(Claim)设备接口 bInterfaceNumber %d，返回 %s",
      mxt->usb.interface, usb_error_name(ret)
    );

    if (ret == LIBUSB_ERROR_BUSY) {
#ifdef MXT_OS_WINDOWS
      mxt_err
      (
        mxt->ctx,
        "接口被占用：可能已被其它程序占用。请先关闭占用该设备的程序（如串口终端、设备管理器等）后重试。"
      );
#else
      mxt_err
      (
        mxt->ctx,
        "接口被占用：可能已被内核驱动或其它进程占用。请先关闭占用该设备的程序，或在 Linux 下检查 /dev/bus/usb/BBB/DDD 的占用情况，并尝试解绑(Detach)内核驱动后重试。"
      );
#endif
    }
#ifdef MXT_OS_WINDOWS
    else if (ret == LIBUSB_ERROR_NOT_SUPPORTED || ret == LIBUSB_ERROR_ACCESS) {
      mxt_err(mxt->ctx,
              "无法访问设备接口：该设备可能未安装 WinUSB 驱动。\n"
              "请使用 Zadig (https://zadig.akeo.ie/) 为该设备安装 WinUSB 驱动。");
    }
#endif

    return usberror_to_rc(ret);
  } else {
    mxt_verb(mxt->ctx, "已声明(Claim) USB 接口");
  }

  /* Get the maximum size of packets on endpoint 1 */
  ret = libusb_get_max_packet_size(libusb_get_device(mxt->usb.handle),
                                   ENDPOINT_1_IN);
  if (ret < LIBUSB_SUCCESS) {
    mxt_err(mxt->ctx, "%s getting maximum packet size on endpoint 1 IN",
            usb_error_name(ret));
    return usberror_to_rc(ret);
  }

  mxt->usb.ep1_in_max_packet_size = ret;
  mxt_verb(mxt->ctx, "Maximum packet size on endpoint 1 IN is %d bytes",
           mxt->usb.ep1_in_max_packet_size);

  /* Configure bridge chip if necessary */
  if (mxt->usb.bridge_chip) {
    mxt->usb.report_id = 0;
    ret = bridge_set_fs_mode(mxt);
    if (ret)
      return ret;

    ret = bridge_configure(mxt);
    if (ret)
      return ret;

    ret = bridge_save_config(mxt);
    if (ret)
      return ret;

    if (mxt->conn->usb.b_i2c_addr == 0x00) {
      if (!((mxt->usb.bootloader == true) || (mxt->usb.sent_btlr_cmd == true))) {
        ret = bridge_find_i2c_address(mxt);
        if (ret)
          return ret;
      }
    }
  } else {
    mxt->usb.report_id = 1;
  }

  mxt->usb.device_connected = true;
  mxt_info(mxt->ctx, "Device registered on usb:%03d-%03d VID=0x%04X PID=0x%04X Interface=%d",
           mxt->conn->usb.bus, mxt->conn->usb.device,
           mxt->usb.desc.idVendor, mxt->usb.desc.idProduct, mxt->usb.interface);

  return MXT_SUCCESS;
}

//******************************************************************************
/// \brief  Get USB product string (model name) for query listing
/// \return 0 on success, negative on error; product_str filled if iProduct set
static int usb_get_product_string(struct libusb_device *dev,
                                  const struct libusb_device_descriptor *desc,
                                  char *product_str, size_t product_str_size)
{
  libusb_device_handle *handle;
  int r;

  if (product_str_size == 0)
    return -1;
  product_str[0] = '\0';
  if (desc->iProduct == 0)
    return 0;

  r = libusb_open(dev, &handle);
  if (r != LIBUSB_SUCCESS)
    return r;
  r = libusb_get_string_descriptor_ascii(handle, desc->iProduct,
                                         (unsigned char *)product_str,
                                         (int)product_str_size);
  libusb_close(handle);
  if (r < 0)
    return r;
  if (r > 0 && (size_t)r < product_str_size)
    product_str[r] = '\0';
  return 0;
}

//******************************************************************************
/// \brief  Check USB product ID against supported list
static bool usb_supported_pid_vid(struct libusb_device_descriptor desc)
{
  if (desc.idVendor == 0x0483 && desc.idProduct == 0x5740)
    return true;

  return ((desc.idVendor == VENDOR_ID) &&
          ((desc.idProduct == 0x211D) || (desc.idProduct == 0x2119) ||
           (desc.idProduct >= 0x2126 && desc.idProduct <= 0x212D) ||
           (desc.idProduct >= 0x2135 && desc.idProduct <= 0x2139) ||
           (desc.idProduct >= 0x213A && desc.idProduct <= 0x21FC) ||
           (desc.idProduct >= 0x8000 && desc.idProduct <= 0x8FFF) ||
           (desc.idProduct == 0x6123)));
}

//******************************************************************************
/// \brief  Scan for supported devices on the USB bus
/// \return #mxt_rc
int usb_scan(struct libmaxtouch_ctx *ctx, struct mxt_conn_info **conn)
{
  int ret, count, i;
  struct libusb_device **devs;
  int usb_bus, usb_device;
  struct mxt_conn_info *curr_conn;

  curr_conn  = *conn;

  ret = usb_initialise_libusb(ctx);
  if (ret)
    return ret;

  count = libusb_get_device_list(ctx->usb.libusb_ctx, &devs);
  if (count <= 0) {
    mxt_err(ctx, "%s enumerating devices", usb_error_name(count));
    if (ctx->query)
      printf("无法枚举 USB 设备（%s）。请确认 libusb 可用且设备已连接。\n", usb_error_name(count));
    return usberror_to_rc(count);
  }

  for (i = 0; i < count; i++) {
    struct libusb_device_descriptor desc;
    ret = libusb_get_device_descriptor(devs[i], &desc);
    if (ret != LIBUSB_SUCCESS) {
      mxt_warn(ctx, "%s trying to retrieve descriptor",
               usb_error_name(ret));
      continue;
    }

    if (usb_supported_pid_vid(desc)) {
      usb_bus = libusb_get_bus_number(devs[i]);
      usb_device = libusb_get_device_address(devs[i]);

      if (curr_conn->usb.bus != 0x00) {
        if (!((curr_conn->usb.bus == usb_bus) && (curr_conn->usb.device == usb_device))) {
          continue;
        }
      }

      ctx->scan_count++;

      if (ctx->query) {
        char product[256];
        if (usb_get_product_string(devs[i], &desc, product, sizeof(product)) == 0
            && product[0] != '\0')
          printf("usb:%03u-%03u %04X:%04X  %s\n",
                 usb_bus, usb_device,
                 (unsigned)desc.idVendor, (unsigned)desc.idProduct, product);
        else
          printf("usb:%03u-%03u %04X:%04X\n",
                 usb_bus, usb_device,
                 (unsigned)desc.idVendor, (unsigned)desc.idProduct);
      } else {
        struct mxt_conn_info *new_conn;
        ret = mxt_new_conn(&new_conn, E_USB);
        if (ret)
          return ret;
        
        new_conn->usb.b_i2c_addr = curr_conn->usb.b_i2c_addr;
        new_conn->usb.bus = usb_bus;
        new_conn->usb.device = usb_device;

        mxt_verb(ctx, "Found VID=%04X PID=%04X %03d-%03d",
                 desc.idVendor, desc.idProduct, usb_bus, usb_device);

        *conn = new_conn;
        ret = MXT_SUCCESS;
        goto free_device_list;
      }
    } else {
      mxt_verb(ctx, "Ignoring VID=%04X PID=%04X", desc.idVendor, desc.idProduct);
    }
  }

  ret = MXT_ERROR_NO_DEVICE;

  if (ctx->query && ctx->scan_count == 0) {
    printf("未找到支持的 USB 设备。\n");
#ifdef MXT_OS_WINDOWS
    /* Windows：列出当前所有 USB 设备（VID:PID 及型号），便于确认连接与安装 WinUSB 驱动 */
    printf("当前连接的 USB 设备（VID:PID 及型号）：\n");
    for (i = 0; i < count; i++) {
      struct libusb_device_descriptor d;
      int usb_bus_num = libusb_get_bus_number(devs[i]);
      int usb_dev_num = libusb_get_device_address(devs[i]);
      if (libusb_get_device_descriptor(devs[i], &d) != LIBUSB_SUCCESS)
        continue;
      {
        char product[256];
        if (usb_get_product_string(devs[i], &d, product, sizeof(product)) == 0
            && product[0] != '\0')
          printf("  usb:%03d-%03d  %04X:%04X  %s\n",
                 usb_bus_num, usb_dev_num,
                 (unsigned)d.idVendor, (unsigned)d.idProduct, product);
        else
          printf("  usb:%03d-%03d  %04X:%04X\n",
                 usb_bus_num, usb_dev_num,
                 (unsigned)d.idVendor, (unsigned)d.idProduct);
      }
    }
    printf("若需使用 maXTouch/STM32 设备，请用 Zadig 为该设备安装 WinUSB 驱动。\n");
#endif
  }

free_device_list:
  libusb_free_device_list(devs, 1);
  return ret;
}

//******************************************************************************
/// \brief  Release device
void usb_release(struct mxt_device *mxt)
{
  /* Are we connected to a device? */
  if (mxt->usb.device_connected) {
    libusb_release_interface(mxt->usb.handle, mxt->usb.interface);
    mxt_dbg(mxt->ctx, "Released the USB interface");

    libusb_close(mxt->usb.handle);
    mxt_dbg(mxt->ctx, "Disconnected from the device");
    mxt->usb.handle = NULL;

    mxt->usb.device_connected = false;
  }

  if (mxt->usb.device) {
    libusb_unref_device(mxt->usb.device);
    mxt->usb.device = NULL;
  }
}

//******************************************************************************
/// \brief  Release USB library
int usb_close(struct libmaxtouch_ctx *ctx)
{
  /* Is the library initialised? */
  if (ctx->usb.libusb_ctx) {
    libusb_exit(ctx->usb.libusb_ctx);
    ctx->usb.libusb_ctx = NULL;
    mxt_dbg(ctx, "Exited from libusb");
  }

  return 0;
}

//******************************************************************************
/// \brief Enumerate the maxtouch devices on the same bus
/// \return #mxt_rc
int usb_find_bus_devices(struct mxt_device *mxt, bool *found)
{
  int ret, count, i;
  struct libusb_device **devs;
  int usb_bus = 0;
  int usb_device = 0;

  count = libusb_get_device_list(mxt->ctx->usb.libusb_ctx, &devs);
  if (count <= 0) {
    mxt_err(mxt->ctx, "%s enumerating devices", usb_error_name(count));
    return usberror_to_rc(count);
  }

  for (i = 0; i < count; i++) {
    struct libusb_device_descriptor desc;
    ret = libusb_get_device_descriptor(devs[i], &desc);
    if (ret != LIBUSB_SUCCESS) {
      mxt_warn(mxt->ctx, "%s trying to retrieve descriptor",
               usb_error_name(ret));
      continue;
    }

    usb_bus = libusb_get_bus_number(devs[i]);
    usb_device = libusb_get_device_address(devs[i]);

    if ((usb_bus == mxt->conn->usb.bus) && (usb_supported_pid_vid(desc))) {
      found[usb_device] = true;
    }
  }

  libusb_free_device_list(devs, 1);
  return MXT_SUCCESS;
}

//******************************************************************************
/// \brief Rediscover device on bus
/// \return #mxt_rc
int usb_rediscover_device(struct mxt_device *mxt, bool *device_list)
{
  int ret, count, i;
  struct libusb_device **devs;
  int usb_bus = 0;
  int usb_device = 0;

  count = libusb_get_device_list(mxt->ctx->usb.libusb_ctx, &devs);
  if (count <= 0) {
    mxt_err(mxt->ctx, "%s enumerating devices", usb_error_name(count));
    return usberror_to_rc(count);
  }

  for (i = 0; i < count; i++) {
    struct libusb_device_descriptor desc;
    ret = libusb_get_device_descriptor(devs[i], &desc);
    if (ret != LIBUSB_SUCCESS) {
      mxt_warn(mxt->ctx, "%s trying to retrieve descriptor",
               usb_error_name(ret));
      continue;
    }

    usb_bus = libusb_get_bus_number(devs[i]);
    usb_device = libusb_get_device_address(devs[i]);

    if (usb_supported_pid_vid(desc)) {
      if (usb_bus == mxt->conn->usb.bus) {
        if (usb_device == mxt->conn->usb.device) {
          /* Old device still connected, fine */
          mxt_dbg(mxt->ctx, "USB device still there at %03d-%03d",
                  usb_bus, usb_device);
          ret = MXT_SUCCESS;
          goto free_device_list;
        } else if (device_list[usb_device] == false) {
          /* Found a new device on bus. We make the assumption that it is the
           * device that just reset. It might be another device with the same
           * PID/VID just plugged in, but there's no way of knowing */
          mxt->conn->usb.device = usb_device;
          mxt_info(mxt->ctx, "Found new device on bus at address %03d-%03d",
                   usb_bus, usb_device);
          ret = MXT_SUCCESS;
          goto free_device_list;
        }
      }
    } else {
      mxt_verb(mxt->ctx, "Ignoring usb:%03d-%03d VID=%04X PID=%04X",
               usb_bus, usb_device, desc.idVendor, desc.idProduct);
    }
  }

  ret = MXT_ERROR_NO_DEVICE;

free_device_list:
  libusb_free_device_list(devs, 1);
  return ret;
}

//******************************************************************************
/// \brief  Reset the maxtouch chip, in normal or bootloader mode
/// \return #mxt_rc
int usb_reset_chip(struct mxt_device *mxt, bool bootloader_mode, uint16_t reset_time_ms)
{
  int ret;
  uint16_t t6_addr;
  unsigned char write_value = RESET_COMMAND;
  bool bus_devices[USB_MAX_BUS_DEVICES] = { 0 };
  int tries;
  int bytes_written;

  /* Obtain command processor's address */
  t6_addr = mxt_get_object_address(mxt, GEN_COMMANDPROCESSOR_T6, 0);
  if (t6_addr == OBJECT_NOT_FOUND)
    return MXT_ERROR_OBJECT_NOT_FOUND;

  /* The value written determines which mode the chip will boot into */
  if (bootloader_mode) {
    write_value = BOOTLOADER_COMMAND;
    mxt->usb.sent_btlr_cmd = true;
  }

  /* Store bus device list */
  ret = usb_find_bus_devices(mxt, bus_devices);
  if (ret)
    return ret;

  tries = 10;
retry:
  /* Send write command to reset the chip */
  ret = write_data(mxt, &write_value, t6_addr + MXT_T6_RESET_OFFSET, 1,
                   &bytes_written, true);
  if (ret == MXT_ERROR_NO_DEVICE && tries--) {
    usleep(500000);
    mxt_warn(mxt->ctx, "Error sending reset command, retrying");
    goto retry;
  } else if (ret) {
    mxt_err(mxt->ctx, "Reset of the chip unsuccessful");
    return ret;
  }

  mxt_info(mxt->ctx, "Sent reset command");

  usb_release(mxt);

  tries = 10;
  while (tries--) {
    /* sleep 1000 ms, may need more depending on device, PI5 requires at least 600ms*/
    usleep(1000000);

    ret = usb_rediscover_device(mxt, bus_devices);
    if (ret == MXT_SUCCESS) {
      mxt_info(mxt->ctx, "device found!");
      break;
    }
  }

  if (ret) {
    mxt_err(mxt->ctx, "Did not find device after reset");
    return ret;
  }

  /* Re-connect to chip */
  ret = usb_open(mxt);
  if (ret) {
    mxt_err(mxt->ctx, "Failed to re-connect to chip after reset");
    return ret;
  }

  return MXT_SUCCESS;
}

//******************************************************************************
/// \brief  Write register to MXT chip
/// \return #mxt_rc
int usb_write_register(struct mxt_device *mxt, unsigned char const *buf,
                       uint16_t start_register, size_t count)
{
  int ret;
  int sent = 0;
  size_t off = 0;

  while (off < count) {
    ret = write_data(mxt, buf + off, start_register + off,
                     count - off, &sent, false);
    if (ret)
      return ret;

    off += sent;
  }

  return MXT_SUCCESS;
}

//******************************************************************************
/// \brief  Read from bootloader
int usb_bootloader_read(struct mxt_device *mxt, uint8_t *buf, size_t count)
{
  unsigned char pkt[mxt->usb.ep1_in_max_packet_size];
  const off_t data_offset = 2;
  size_t max_count = mxt->usb.ep1_in_max_packet_size - data_offset;
  int ret;

  if (count > max_count)
    return MXT_ERROR_IO;

  memset(&pkt, 0, sizeof(pkt));
  pkt[0] = IIC_DATA_1;
  pkt[1] = 0x00;
  pkt[2] = (uint8_t)count;

  ret = usb_transfer(mxt, pkt, sizeof(pkt), pkt, sizeof(pkt), false);
  if (ret)
    return ret;

  /* Output the data read from the registers */
  (void)memcpy(buf, &pkt[data_offset], count);

  return MXT_SUCCESS;
}

//******************************************************************************
/// \brief  Write packet to bootloader
/// \return #mxt_rc
static int usb_bootloader_write_packet(struct mxt_device *mxt, uint8_t const *buf,
                                       size_t count, int *bytes_transferred)
{
  unsigned char pkt[mxt->usb.ep1_in_max_packet_size];
  const off_t data_offset = 3;
  size_t max_count = mxt->usb.ep1_in_max_packet_size - data_offset;
  int ret;

  if (count > max_count)
    count = max_count;

  memset(&pkt, 0, sizeof(pkt));
  pkt[0] = IIC_DATA_1;
  pkt[1] = (uint8_t)count;
  pkt[2] = 0x00;

  (void)memcpy(&pkt[data_offset], buf, count);

  ret = usb_transfer(mxt, pkt, sizeof(pkt), pkt, sizeof(pkt), false);
  if (ret)
    return ret;

  *bytes_transferred = count;
  return MXT_SUCCESS;
}

//******************************************************************************
/// \brief  Write to bootloader
int usb_bootloader_write(struct mxt_device *mxt, unsigned char const *buf, size_t count)
{
  int ret;
  int bytes_transferred;
  size_t bytes = 0;

  while (bytes < count) {
    ret = usb_bootloader_write_packet(mxt, buf + bytes, count - bytes,
                                      &bytes_transferred);
    if (ret)
      return ret;

    bytes += bytes_transferred;
  }

  return MXT_SUCCESS;
}

//******************************************************************************
/// \brief Read CHG line
/// \return #mxt_rc
int usb_read_chg(struct mxt_device *mxt, bool *value)
{
  unsigned char pkt[mxt->usb.ep1_in_max_packet_size];
  bool chg;
  int ret;

  memset(&pkt, 0, sizeof(pkt));
  pkt[0] = CMD_READ_PINS;

  ret = usb_transfer(mxt, pkt, sizeof(pkt), pkt, sizeof(pkt), false);
  if (ret)
    return ret;

  chg = pkt[2] & 0x4;

  mxt_verb(mxt->ctx, "CHG line %s", chg ? "HIGH" : "LOW");

  *value = chg;
  return MXT_SUCCESS;
}

//******************************************************************************
/// \brief  Switch to parallel digitizer mode
/// \return #mxt_rc
int usb_switch_parallel_mode(struct mxt_device *mxt, struct mxt_conn_info *conn)
{
  unsigned char pkt[mxt->usb.ep1_in_max_packet_size];
  int ret;

  if (conn->type != E_USB) {
    return MXT_ERROR_NO_DEVICE;
  }

  /* Command packet */
  memset(&pkt, 0, sizeof(pkt));
  pkt[0] = CMD_PARALLEL_MODE;
  /* Make default power-on/reset */
  pkt[1] = 0xE7;

  mxt_verb(mxt->ctx, "Sending CMD_PARALLEL_MODE");

  ret = usb_transfer(mxt, &pkt, sizeof(pkt), &pkt, sizeof(pkt), true);
  if (ret)
    return ret;

  libusb_reset_device(mxt->usb.handle);

  return MXT_SUCCESS;
}

//******************************************************************************
/// \brief  Switch 5030 bridge to Fast Mode mode
/// \return #mxt_rc
int usb_switch_fast_mode(struct mxt_device *mxt, struct mxt_conn_info *conn)
{
  unsigned char pkt[mxt->usb.ep1_in_max_packet_size];
  int ret;
  
  if (conn->type != E_USB) {
    return MXT_ERROR_NO_DEVICE;
  }

  printf("bridge_parallel: Set to fast mode\n");

  /* Command packet */
  memset(&pkt, 0, sizeof(pkt));
  pkt[0] = CMD_FAST_MODE;
  /* Make default power-on/reset */
  pkt[1] = 0xE7;

  ret = usb_transfer(mxt, &pkt, sizeof(pkt), &pkt, sizeof(pkt), true);
  if(ret)
    return ret;

  libusb_reset_device(mxt->usb.handle);

  mxt_verb(mxt->ctx, "Sending CMD_FAST_MODE");

  return MXT_SUCCESS;
}



