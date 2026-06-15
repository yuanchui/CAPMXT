//------------------------------------------------------------------------------
/// \file   serial_device.c
/// \brief  MXT device low level access via serial/COM (STM32 VCP bridge)
//------------------------------------------------------------------------------
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "libmaxtouch/log.h"
#include "libmaxtouch/libmaxtouch.h"
#include "libmaxtouch/info_block.h"
#include "serial_device.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <sys/select.h>
#endif

#define REPORT_ID            0x01
#define IIC_DATA_1           0x51
#define CMD_READ_PINS        0x82
#define CMD_CONFIG           0x80
#define CMD_FIND_IIC_ADDRESS 0xE0

#define COMMS_STATUS_OK          0x00
#define COMMS_STATUS_WRITE_OK    0x04

#define RESET_COMMAND          0x01
#define BOOTLOADER_COMMAND     0xA5

static void serial_sleep_ms(int ms)
{
#ifdef _WIN32
  Sleep((DWORD)ms);
#else
  usleep((useconds_t)ms * 1000);
#endif
}

#ifdef _WIN32
static int serial_write_all(struct mxt_device *mxt, const void *buf, size_t len)
{
  HANDLE h = (HANDLE)mxt->serial.handle;
  const uint8_t *p = (const uint8_t *)buf;
  size_t off = 0;

  while (off < len) {
    DWORD written = 0;
    if (!WriteFile(h, p + off, (DWORD)(len - off), &written, NULL) || written == 0)
      return MXT_ERROR_IO;
    off += written;
  }
  return MXT_SUCCESS;
}

static int serial_read_some(struct mxt_device *mxt, void *buf, size_t max_len, size_t *out_len)
{
  HANDLE h = (HANDLE)mxt->serial.handle;
  DWORD read_bytes = 0;

  if (!ReadFile(h, buf, (DWORD)max_len, &read_bytes, NULL))
    return MXT_ERROR_IO;

  *out_len = (size_t)read_bytes;
  return MXT_SUCCESS;
}

static int serial_drain_rx(struct mxt_device *mxt, int timeout_ms)
{
  uint8_t tmp[256];
  int elapsed = 0;

  PurgeComm((HANDLE)mxt->serial.handle, PURGE_RXCLEAR);

  while (elapsed < timeout_ms) {
    size_t n = 0;
    int ret = serial_read_some(mxt, tmp, sizeof(tmp), &n);
    if (ret != MXT_SUCCESS)
      return ret;
    if (n == 0) {
      serial_sleep_ms(20);
      elapsed += 20;
      continue;
    }
    elapsed = 0;
  }
  return MXT_SUCCESS;
}

static int serial_open_port(struct mxt_device *mxt, const char *path)
{
  char win_path[160];
  HANDLE h;
  DCB dcb;
  COMMTIMEOUTS timeouts;

  if (!path || !path[0])
    return MXT_ERROR_BAD_INPUT;

  if (strncmp(path, "\\\\.\\", 4) == 0)
    snprintf(win_path, sizeof(win_path), "%s", path);
  else
    snprintf(win_path, sizeof(win_path), "\\\\.\\%s", path);

  h = CreateFileA(win_path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                  OPEN_EXISTING, 0, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    mxt_err(mxt->ctx, "无法打开串口 %s", path);
    return MXT_ERROR_ACCESS;
  }

  memset(&dcb, 0, sizeof(dcb));
  dcb.DCBlength = sizeof(dcb);
  if (!GetCommState(h, &dcb)) {
    CloseHandle(h);
    return MXT_ERROR_IO;
  }
  dcb.BaudRate = CBR_115200;
  dcb.ByteSize = 8;
  dcb.Parity = NOPARITY;
  dcb.StopBits = ONESTOPBIT;
  dcb.fDtrControl = DTR_CONTROL_ENABLE;
  dcb.fRtsControl = RTS_CONTROL_ENABLE;
  if (!SetCommState(h, &dcb)) {
    CloseHandle(h);
    return MXT_ERROR_IO;
  }

  memset(&timeouts, 0, sizeof(timeouts));
  timeouts.ReadIntervalTimeout = 50;
  timeouts.ReadTotalTimeoutMultiplier = 10;
  timeouts.ReadTotalTimeoutConstant = SERIAL_TRANSFER_TIMEOUT_MS;
  timeouts.WriteTotalTimeoutConstant = SERIAL_TRANSFER_TIMEOUT_MS;
  timeouts.WriteTotalTimeoutMultiplier = 10;
  SetCommTimeouts(h, &timeouts);

  mxt->serial.handle = h;
  return MXT_SUCCESS;
}

static void serial_close_port(struct mxt_device *mxt)
{
  if (mxt->serial.handle) {
    CloseHandle((HANDLE)mxt->serial.handle);
    mxt->serial.handle = NULL;
  }
}
#else
static int serial_write_all(struct mxt_device *mxt, const void *buf, size_t len)
{
  const uint8_t *p = (const uint8_t *)buf;
  size_t off = 0;

  while (off < len) {
    ssize_t w = write(mxt->serial.fd, p + off, len - off);
    if (w < 0) {
      if (errno == EINTR)
        continue;
      return MXT_ERROR_IO;
    }
    if (w == 0)
      return MXT_ERROR_IO;
    off += (size_t)w;
  }
  return MXT_SUCCESS;
}

static int serial_read_some(struct mxt_device *mxt, void *buf, size_t max_len, size_t *out_len)
{
  ssize_t r = read(mxt->serial.fd, buf, max_len);
  if (r < 0) {
    if (errno == EINTR)
      return serial_read_some(mxt, buf, max_len, out_len);
    return MXT_ERROR_IO;
  }
  *out_len = (size_t)r;
  return MXT_SUCCESS;
}

static int serial_drain_rx(struct mxt_device *mxt, int timeout_ms)
{
  uint8_t tmp[256];
  int elapsed = 0;
  struct timeval tv;
  fd_set rfds;

  while (elapsed < timeout_ms) {
    FD_ZERO(&rfds);
    FD_SET(mxt->serial.fd, &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = 20000;
    int sel = select(mxt->serial.fd + 1, &rfds, NULL, NULL, &tv);
    if (sel < 0)
      return MXT_ERROR_IO;
    if (sel == 0) {
      elapsed += 20;
      continue;
    }
    size_t n = 0;
    int ret = serial_read_some(mxt, tmp, sizeof(tmp), &n);
    if (ret != MXT_SUCCESS)
      return ret;
    if (n == 0) {
      elapsed += 20;
      continue;
    }
    elapsed = 0;
  }
  return MXT_SUCCESS;
}

static int serial_open_port(struct mxt_device *mxt, const char *path)
{
  int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
  struct termios tio;

  if (fd < 0) {
    mxt_err(mxt->ctx, "无法打开串口 %s", path);
    return MXT_ERROR_ACCESS;
  }

  if (tcgetattr(fd, &tio) != 0) {
    close(fd);
    return MXT_ERROR_IO;
  }
  cfmakeraw(&tio);
  cfsetispeed(&tio, B115200);
  cfsetospeed(&tio, B115200);
  tio.c_cflag |= (CLOCAL | CREAD);
  tio.c_cflag &= ~PARENB;
  tio.c_cflag &= ~CSTOPB;
  tio.c_cflag &= ~CSIZE;
  tio.c_cflag |= CS8;
  if (tcsetattr(fd, TCSANOW, &tio) != 0) {
    close(fd);
    return MXT_ERROR_IO;
  }
  fcntl(fd, F_SETFL, 0);

  mxt->serial.fd = fd;
  return MXT_SUCCESS;
}

static void serial_close_port(struct mxt_device *mxt)
{
  if (mxt->serial.fd >= 0) {
    close(mxt->serial.fd);
    mxt->serial.fd = -1;
  }
}
#endif

static int serial_switch_mode0_text(struct mxt_device *mxt)
{
  static const char mode0_cmd[] = "mode0\r\n";
  int ret;

  ret = serial_write_all(mxt, mode0_cmd, sizeof(mode0_cmd) - 1);
  if (ret != MXT_SUCCESS)
    return ret;

  serial_sleep_ms(150);
  return serial_drain_rx(mxt, 300);
}

static int serial_transfer(struct mxt_device *mxt, void *cmd, int cmd_size,
                           void *response, int response_size, bool ignore_response)
{
  int ret;
  size_t bytes_read = 0;

  if (!mxt->serial.device_connected)
    return MXT_ERROR_NO_DEVICE;

  ret = serial_write_all(mxt, cmd, (size_t)cmd_size);
  if (ret != MXT_SUCCESS)
    return ret;

  mxt_log_buffer(mxt->ctx, LOG_VERBOSE, "TX:", cmd, cmd_size);

  if (ignore_response)
    return MXT_SUCCESS;

  memset(response, 0, (size_t)response_size);
  ret = serial_read_some(mxt, response, (size_t)response_size, &bytes_read);
  if (ret != MXT_SUCCESS)
    return ret;

  if (bytes_read == 0)
    return MXT_ERROR_TIMEOUT;

  mxt_log_buffer(mxt->ctx, LOG_VERBOSE, "RX:", response, (int)bytes_read);
  return MXT_SUCCESS;
}

  static int bridge_find_i2c_address(struct mxt_device *mxt)
{
  unsigned char pkt[SERIAL_MAX_PACKET_SIZE];
  unsigned char response;
  int ret;

  memset(pkt, 0, sizeof(pkt));
  pkt[0] = CMD_FIND_IIC_ADDRESS;

  ret = serial_transfer(mxt, pkt, 1, pkt, sizeof(pkt), false);
  if (ret)
    return ret;

  response = pkt[1];
  if (response == 0x81) {
    mxt_err(mxt->ctx, "桥接芯片未找到 I2C 设备");
    return MXT_ERROR_NO_DEVICE;
  }

  if (response < 0x4a) {
    mxt->serial.bootloader = true;
    mxt_info(mxt->ctx, "桥接发现 Bootloader @0x%02X", response);
  } else {
    mxt->serial.address = response;
    mxt_info(mxt->ctx, "桥接发现应用模式 @0x%02X", response);
  }
  return MXT_SUCCESS;
}

int serial_read_register(struct mxt_device *mxt, unsigned char *buf,
                         uint16_t start_register, size_t count,
                         size_t *bytes_transferred)
{
  unsigned char pkt[SERIAL_MAX_PACKET_SIZE];
  size_t cmd_size;
  size_t max_count;
  off_t response_ofs;
  int ret;

  if (!mxt->serial.device_connected)
    return MXT_ERROR_NO_DEVICE;

  memset(pkt, 0, sizeof(pkt));
  cmd_size = 6;
  max_count = (size_t)mxt->serial.ep1_in_max_packet_size - cmd_size;
  if (count > max_count)
    count = max_count;

  pkt[0] = REPORT_ID;
  pkt[1] = IIC_DATA_1;
  pkt[2] = 2;
  pkt[3] = (unsigned char)count;
  pkt[4] = start_register & 0xFF;
  pkt[5] = (start_register & 0xFF00) >> 8;
  response_ofs = 2;

  ret = serial_transfer(mxt, pkt, (int)cmd_size, pkt, sizeof(pkt), false);
  if (ret)
    return ret;

  if (pkt[response_ofs] != COMMS_STATUS_OK) {
    mxt_err(mxt->ctx, "读寄存器应答错误 0x%02X", pkt[response_ofs]);
    return MXT_ERROR_IO;
  }

  memcpy(buf, &pkt[response_ofs + 2], count);
  *bytes_transferred = count;
  return MXT_SUCCESS;
}

static int serial_write_data(struct mxt_device *mxt, unsigned char const *buf,
                             uint16_t start_register, size_t count,
                             bool ignore_response)
{
  unsigned char pkt[SERIAL_MAX_PACKET_SIZE];
  size_t max_count;
  size_t cmd_size;
  off_t response_ofs;
  int ret;

  if (!mxt->serial.device_connected)
    return MXT_ERROR_NO_DEVICE;

  memset(pkt, 0, sizeof(pkt));
  cmd_size = 6;
  max_count = (size_t)mxt->serial.ep1_in_max_packet_size - cmd_size;
  if (count > max_count)
    count = max_count;

  pkt[0] = REPORT_ID;
  pkt[1] = IIC_DATA_1;
  pkt[2] = (unsigned char)(2 + count);
  pkt[3] = 0;
  pkt[4] = start_register & 0xFF;
  pkt[5] = (start_register & 0xFF00) >> 8;
  memcpy(&pkt[6], buf, count);
  response_ofs = 2;

  ret = serial_transfer(mxt, pkt, (int)(cmd_size + count), pkt, sizeof(pkt), ignore_response);
  if (ret)
    return ret;

  if (!ignore_response && pkt[response_ofs] != COMMS_STATUS_WRITE_OK) {
    mxt_err(mxt->ctx, "写寄存器应答错误 0x%02X", pkt[response_ofs]);
    return MXT_ERROR_IO;
  }
  return MXT_SUCCESS;
}

int serial_write_register(struct mxt_device *mxt, unsigned char const *buf,
                          uint16_t start_register, size_t count)
{
  size_t bytes_written = 0;
  int ret;

  while (bytes_written < count) {
    size_t chunk = count - bytes_written;
    ret = serial_write_data(mxt, buf + bytes_written,
                            (uint16_t)(start_register + bytes_written),
                            chunk, false);
    if (ret)
      return ret;
    bytes_written += chunk;
  }
  return MXT_SUCCESS;
}

int serial_bootloader_read(struct mxt_device *mxt, unsigned char *buf, size_t count)
{
  return serial_read_register(mxt, buf, 0, count, &count);
}

int serial_bootloader_write(struct mxt_device *mxt, unsigned char const *buf, size_t count)
{
  return serial_write_register(mxt, buf, 0, count);
}

bool serial_is_bootloader(struct mxt_device *mxt)
{
  return mxt->serial.bootloader;
}

int serial_read_chg(struct mxt_device *mxt, bool *value)
{
  unsigned char pkt[SERIAL_MAX_PACKET_SIZE];
  int ret;

  memset(pkt, 0, sizeof(pkt));
  pkt[0] = CMD_READ_PINS;

  ret = serial_transfer(mxt, pkt, 1, pkt, sizeof(pkt), false);
  if (ret)
    return ret;

  *value = (pkt[2] & 0x04) != 0;
  return MXT_SUCCESS;
}

int serial_find_bus_devices(struct mxt_device *mxt, bool *device_list)
{
  (void)mxt;
  (void)device_list;
  return MXT_ERROR_NOT_SUPPORTED;
}

int serial_rediscover_device(struct mxt_device *mxt, bool *device_list)
{
  (void)mxt;
  (void)device_list;
  return MXT_ERROR_NOT_SUPPORTED;
}

int serial_reset_chip(struct mxt_device *mxt, bool bootloader_mode, uint16_t reset_time_ms)
{
  uint16_t t6_addr;
  unsigned char write_value = RESET_COMMAND;
  int ret;

  t6_addr = mxt_get_object_address(mxt, GEN_COMMANDPROCESSOR_T6, 0);
  if (t6_addr == OBJECT_NOT_FOUND)
    return MXT_ERROR_OBJECT_NOT_FOUND;

  if (bootloader_mode)
    write_value = BOOTLOADER_COMMAND;

  ret = mxt_write_register(mxt, &write_value, t6_addr + MXT_T6_RESET_OFFSET, 1);
  if (ret)
    return ret;

  serial_sleep_ms((int)reset_time_ms);
  return MXT_SUCCESS;
}

#ifdef _WIN32
static void serial_list_com_ports(struct libmaxtouch_ctx *ctx, struct mxt_conn_info *curr_conn)
{
  HKEY hKey;
  char value_name[256];
  char data[256];
  DWORD i = 0;

  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM",
                    0, KEY_READ, &hKey) != ERROR_SUCCESS)
    return;

  while (1) {
    DWORD value_name_len = (DWORD)sizeof(value_name);
    DWORD data_len = (DWORD)sizeof(data);
    DWORD type = 0;
    LONG rc = RegEnumValueA(hKey, i++, value_name, &value_name_len,
                            NULL, &type, (LPBYTE)data, &data_len);
    if (rc != ERROR_SUCCESS)
      break;

    if (type != REG_SZ || data[0] == '\0')
      continue;

    if (curr_conn->serial.path[0] != '\0' &&
#ifdef _WIN32
        _stricmp(curr_conn->serial.path, data) != 0)
#else
        strcasecmp(curr_conn->serial.path, data) != 0)
#endif
      continue;

    ctx->scan_count++;
    if (ctx->query)
      printf("serial:%s\n", data);
  }

  RegCloseKey(hKey);
}
#else
static void serial_list_com_ports(struct libmaxtouch_ctx *ctx, struct mxt_conn_info *curr_conn)
{
  const char *patterns[] = { "/dev/ttyACM", "/dev/ttyUSB", NULL };
  char path[128];
  int p;

  for (p = 0; patterns[p]; p++) {
    for (int i = 0; i < 32; i++) {
      snprintf(path, sizeof(path), "%s%d", patterns[p], i);
      if (access(path, R_OK | W_OK) != 0)
        continue;
      if (curr_conn->serial.path[0] != '\0' &&
          strcmp(curr_conn->serial.path, path) != 0)
        continue;
      ctx->scan_count++;
      if (ctx->query)
        printf("serial:%s\n", path);
    }
  }
}
#endif

int serial_scan(struct libmaxtouch_ctx *ctx, struct mxt_conn_info **conn)
{
  struct mxt_conn_info *curr_conn = *conn;
  struct mxt_conn_info *new_conn;
  int ret;

  if (!curr_conn) {
    ret = mxt_new_conn(&new_conn, E_SERIAL);
    if (ret)
      return ret;
    *conn = new_conn;
    curr_conn = new_conn;
  }

  serial_list_com_ports(ctx, curr_conn);

  if (ctx->query) {
    if (ctx->scan_count)
      return MXT_SUCCESS;
    return MXT_ERROR_NO_DEVICE;
  }

  if (curr_conn->serial.path[0] == '\0')
    return MXT_ERROR_NO_DEVICE;

  return MXT_SUCCESS;
}

int serial_open(struct mxt_device *mxt)
{
  const char *path = mxt->conn->serial.path;
  int ret;

  if (!path || !path[0])
    return MXT_ERROR_BAD_INPUT;

#ifdef _WIN32
  mxt->serial.handle = NULL;
#else
  mxt->serial.fd = -1;
#endif
  mxt->serial.ep1_in_max_packet_size = SERIAL_MAX_PACKET_SIZE;
  mxt->serial.bridge_chip = false;
  mxt->serial.bootloader = false;
  mxt->serial.device_connected = false;

  ret = serial_open_port(mxt, path);
  if (ret)
    return ret;

  ret = serial_switch_mode0_text(mxt);
  if (ret) {
    serial_close_port(mxt);
    return ret;
  }

  if (mxt->conn->b_i2c_addr == 0x00) {
    ret = bridge_find_i2c_address(mxt);
    if (ret) {
      serial_close_port(mxt);
      return ret;
    }
  }

  mxt->serial.device_connected = true;
  mxt_info(mxt->ctx, "Device registered on serial:%s", path);
  return MXT_SUCCESS;
}

void serial_release(struct mxt_device *mxt)
{
  serial_close_port(mxt);
  mxt->serial.device_connected = false;
}
