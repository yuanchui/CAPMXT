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
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
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

static void serial_trace_io(struct mxt_device *mxt, const char *dir,
                            const uint8_t *buf, size_t len)
{
  char hex[384];
  size_t pos = 0;
  unsigned int i;
  const char *via;

  if (!mxt || !buf || len == 0)
    return;

  via = mxt->serial.is_tcp_proxy ? "proxy" : "uart";
  for (i = 0; i < len && pos + 4 < sizeof(hex); i++)
    pos += (size_t)snprintf(hex + pos, sizeof(hex) - pos, "%02X ", buf[i]);

  fprintf(stdout, "SERIAL_TRACE [%s] %s %zuB: %s\n", via, dir, len, hex);
  fflush(stdout);
  mxt_info(mxt->ctx, "串口 %s %zuB", dir, len);
}

static const char *serial_rc_string(int rc)
{
  switch (rc) {
  case MXT_SUCCESS: return "OK";
  case MXT_ERROR_IO: return "IO";
  case MXT_ERROR_NO_DEVICE: return "NO_DEVICE";
  case MXT_ERROR_TIMEOUT: return "TIMEOUT";
  case MXT_ERROR_ACCESS: return "ACCESS";
  case MXT_ERROR_BAD_INPUT: return "BAD_INPUT";
  default: return "ERR";
  }
}

static void serial_sleep_ms(int ms)
{
#ifdef _WIN32
  Sleep((DWORD)ms);
#else
  usleep((useconds_t)ms * 1000);
#endif
}

#ifdef _WIN32
static bool g_wsa_started;

static int serial_ensure_wsa(void)
{
  WSADATA wsa;

  if (g_wsa_started)
    return MXT_SUCCESS;

  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    return MXT_ERROR_IO;

  g_wsa_started = true;
  return MXT_SUCCESS;
}

static int serial_proxy_wait_readable(SOCKET sock, int timeout_ms);
static int serial_read_for_transfer(struct mxt_device *mxt, void *buf, size_t max_len,
                                    size_t min_len, size_t *out_len, int timeout_ms);

static int serial_write_all(struct mxt_device *mxt, const void *buf, size_t len)
{
  const uint8_t *p = (const uint8_t *)buf;
  size_t off = 0;

  if (mxt->serial.is_tcp_proxy) {
    SOCKET sock = (SOCKET)(uintptr_t)mxt->serial.handle;
    while (off < len) {
      int sent = send(sock, (const char *)(p + off), (int)(len - off), 0);
      if (sent <= 0)
        return MXT_ERROR_IO;
      off += (size_t)sent;
    }
    return MXT_SUCCESS;
  }

  HANDLE h = (HANDLE)mxt->serial.handle;

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
  if (mxt->serial.is_tcp_proxy) {
    SOCKET sock = (SOCKET)(uintptr_t)mxt->serial.handle;
    int r = recv(sock, (char *)buf, (int)max_len, 0);
    if (r < 0)
      return MXT_ERROR_IO;
    *out_len = (size_t)r;
    return MXT_SUCCESS;
  }

  HANDLE h = (HANDLE)mxt->serial.handle;
  DWORD read_bytes = 0;

  if (!ReadFile(h, buf, (DWORD)max_len, &read_bytes, NULL))
    return MXT_ERROR_IO;

  *out_len = (size_t)read_bytes;
  return MXT_SUCCESS;
}

static void serial_rx_carry_clear(struct mxt_device *mxt)
{
  mxt->serial.rx_carry_len = 0U;
}

static void serial_rx_carry_append(struct mxt_device *mxt, const uint8_t *data, size_t len)
{
  size_t room;

  if (!data || len == 0U)
    return;

  room = SERIAL_RX_CARRY_SIZE - mxt->serial.rx_carry_len;
  if (len > room)
    len = room;
  if (len == 0U)
    return;

  memcpy(mxt->serial.rx_carry + mxt->serial.rx_carry_len, data, len);
  mxt->serial.rx_carry_len += len;
}

static size_t serial_rx_carry_take(struct mxt_device *mxt, uint8_t *buf, size_t len)
{
  size_t take;

  if (len == 0U || mxt->serial.rx_carry_len == 0U)
    return 0U;

  take = mxt->serial.rx_carry_len;
  if (take > len)
    take = len;

  memcpy(buf, mxt->serial.rx_carry, take);
  if (take < mxt->serial.rx_carry_len) {
    memmove(mxt->serial.rx_carry, mxt->serial.rx_carry + take,
            mxt->serial.rx_carry_len - take);
  }
  mxt->serial.rx_carry_len -= take;
  return take;
}

static void serial_flush_rx(struct mxt_device *mxt)
{
  uint8_t tmp[256];
  int quiet_ms = 0;

  serial_rx_carry_clear(mxt);

  if (!mxt->serial.is_tcp_proxy) {
    PurgeComm((HANDLE)mxt->serial.handle, PURGE_RXCLEAR);
    return;
  }

  while (quiet_ms < 120) {
    size_t n = 0U;
    SOCKET sock = (SOCKET)(uintptr_t)mxt->serial.handle;

    if (serial_proxy_wait_readable(sock, 20) <= 0) {
      quiet_ms += 20;
      continue;
    }

    if (serial_read_some(mxt, tmp, sizeof(tmp), &n) != MXT_SUCCESS || n == 0U) {
      quiet_ms += 20;
      continue;
    }

    serial_trace_io(mxt, "FLUSH", tmp, n);
    quiet_ms = 0;
  }
}

static int serial_read_exact(struct mxt_device *mxt, uint8_t *buf, size_t need, int timeout_ms)
{
  size_t got = 0U;
  int elapsed = 0;
  uint8_t tmp[128];

  while (got < need) {
    size_t from_carry = serial_rx_carry_take(mxt, buf + got, need - got);

    got += from_carry;
    if (got >= need)
      return MXT_SUCCESS;

    if (mxt->serial.is_tcp_proxy) {
      SOCKET sock = (SOCKET)(uintptr_t)mxt->serial.handle;
      if (serial_proxy_wait_readable(sock, 20) <= 0) {
        elapsed += 20;
        if (elapsed >= timeout_ms)
          return MXT_ERROR_TIMEOUT;
        continue;
      }
    }

    {
      size_t n = 0U;
      size_t use;
      int ret = serial_read_some(mxt, tmp, sizeof(tmp), &n);

      if (ret != MXT_SUCCESS)
        return ret;
      if (n == 0U) {
        elapsed += 20;
        if (elapsed >= timeout_ms)
          return MXT_ERROR_TIMEOUT;
        continue;
      }

      elapsed = 0;
      use = n;
      if (use > (need - got))
        use = need - got;
      memcpy(buf + got, tmp, use);
      got += use;
      if (use < n)
        serial_rx_carry_append(mxt, tmp + use, n - use);
    }
  }

  return MXT_SUCCESS;
}

static int serial_drain_rx(struct mxt_device *mxt, int timeout_ms)
{
  uint8_t tmp[256];
  int elapsed = 0;

  if (!mxt->serial.is_tcp_proxy)
    PurgeComm((HANDLE)mxt->serial.handle, PURGE_RXCLEAR);

  while (elapsed < timeout_ms) {
    size_t n = 0;
    int ret;

    if (mxt->serial.is_tcp_proxy) {
      SOCKET sock = (SOCKET)(uintptr_t)mxt->serial.handle;
      if (serial_proxy_wait_readable(sock, 20) <= 0) {
        elapsed += 20;
        continue;
      }
    }

    ret = serial_read_some(mxt, tmp, sizeof(tmp), &n);
    if (ret != MXT_SUCCESS)
      return ret;
    if (n == 0) {
      serial_sleep_ms(20);
      elapsed += 20;
      continue;
    }
    serial_trace_io(mxt, "RX", tmp, n);
    elapsed = 0;
  }
  return MXT_SUCCESS;
}

static int serial_rx_contains(const uint8_t *buf, size_t len, const char *needle)
{
  size_t nlen;
  size_t i;

  if (!buf || !needle || len == 0U)
    return 0;

  nlen = strlen(needle);
  if (nlen == 0U || len < nlen)
    return 0;

  for (i = 0U; i <= (len - nlen); i++) {
    if (memcmp(buf + i, needle, nlen) == 0)
      return 1;
  }
  return 0;
}

static int serial_wait_rx_contains(struct mxt_device *mxt, const char *needle, int timeout_ms)
{
  uint8_t acc[256];
  size_t acc_len = 0U;
  int elapsed = 0;

  while (elapsed < timeout_ms) {
    size_t from_carry;
    uint8_t tmp[128];
    size_t n = 0U;
    int ret;

    from_carry = serial_rx_carry_take(mxt, acc + acc_len, sizeof(acc) - acc_len);
    acc_len += from_carry;
    if (acc_len >= sizeof(acc))
      acc_len = sizeof(acc) - 1U;
    acc[acc_len] = '\0';

    if (serial_rx_contains(acc, acc_len, needle))
      return MXT_SUCCESS;

    if (mxt->serial.is_tcp_proxy) {
      SOCKET sock = (SOCKET)(uintptr_t)mxt->serial.handle;
      if (serial_proxy_wait_readable(sock, 20) <= 0) {
        elapsed += 20;
        continue;
      }
    }

    ret = serial_read_some(mxt, tmp, sizeof(tmp), &n);
    if (ret != MXT_SUCCESS)
      return ret;
    if (n == 0U) {
      serial_sleep_ms(20);
      elapsed += 20;
      continue;
    }

    serial_trace_io(mxt, "RX", tmp, n);
    elapsed = 0;

    if (n > (sizeof(acc) - acc_len - 1U))
      n = sizeof(acc) - acc_len - 1U;
    if (n > 0U) {
      memcpy(acc + acc_len, tmp, n);
      acc_len += n;
      acc[acc_len] = '\0';
      if (serial_rx_contains(acc, acc_len, needle))
        return MXT_SUCCESS;
    }
  }

  return MXT_ERROR_TIMEOUT;
}

static int serial_send_line_cmd(struct mxt_device *mxt, const char *cmd)
{
  size_t len;
  int ret;

  if (!cmd)
    return MXT_ERROR_BAD_INPUT;

  len = strlen(cmd);
  if (len == 0U)
    return MXT_SUCCESS;

  serial_trace_io(mxt, "TX", (const uint8_t *)cmd, len);
  ret = serial_write_all(mxt, cmd, len);
  return ret;
}

static int serial_switch_mode0_text(struct mxt_device *mxt);

static int serial_prepare_mcu_bridge(struct mxt_device *mxt)
{
  int ret;

  if (!mxt->serial.is_tcp_proxy) {
    ret = serial_send_line_cmd(mxt, "SPISTOP\r\n");
    if (ret != MXT_SUCCESS)
      return ret;
    serial_sleep_ms(120);
    (void)serial_drain_rx(mxt, 300);
  }

  ret = serial_switch_mode0_text(mxt);
  if (ret == MXT_SUCCESS)
    return MXT_SUCCESS;

  /* 代理路径上 serial-app 可能已切到桥模式，再发 mode0 文本不会回 OK */
  if (mxt->serial.is_tcp_proxy && ret == MXT_ERROR_TIMEOUT) {
    fprintf(stdout, "SERIAL_TEST: mode0 无 OK（可能已在桥模式），继续 FIND_IIC\n");
    fflush(stdout);
    return MXT_SUCCESS;
  }

  return ret;
}

static int serial_open_proxy(struct mxt_device *mxt, const char *hostport)
{
  char host[96];
  char port_str[16];
  const char *colon;
  SOCKET sock;
  struct addrinfo hints;
  struct addrinfo *res = NULL;
  int ret;

  if (!hostport || !hostport[0])
    return MXT_ERROR_BAD_INPUT;

  ret = serial_ensure_wsa();
  if (ret)
    return ret;

  colon = strrchr(hostport, ':');
  if (!colon || colon == hostport)
    return MXT_ERROR_BAD_INPUT;

  if ((size_t)(colon - hostport) >= sizeof(host))
    return MXT_ERROR_BAD_INPUT;

  memcpy(host, hostport, (size_t)(colon - hostport));
  host[colon - hostport] = '\0';
  snprintf(port_str, sizeof(port_str), "%s", colon + 1);

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
    mxt_err(mxt->ctx, "无法解析串口代理 %s", hostport);
    return MXT_ERROR_ACCESS;
  }

  sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (sock == INVALID_SOCKET) {
    freeaddrinfo(res);
    return MXT_ERROR_IO;
  }

  if (connect(sock, res->ai_addr, (int)res->ai_addrlen) != 0) {
    mxt_err(mxt->ctx, "无法连接串口代理 %s", hostport);
    closesocket(sock);
    freeaddrinfo(res);
    return MXT_ERROR_ACCESS;
  }

  freeaddrinfo(res);
  mxt->serial.handle = (void *)(uintptr_t)sock;
  mxt->serial.is_tcp_proxy = true;
  {
    DWORD ms = (DWORD)SERIAL_TRANSFER_TIMEOUT_MS;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ms, sizeof(ms));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&ms, sizeof(ms));
  }
  mxt_info(mxt->ctx, "已连接串口代理 %s", hostport);
  fprintf(stdout, "SERIAL_TEST: proxy connected %s:%s\n", host, port_str);
  fflush(stdout);
  return MXT_SUCCESS;
}

static int serial_proxy_wait_readable(SOCKET sock, int timeout_ms)
{
  fd_set rfds;
  struct timeval tv;
  int r;

  FD_ZERO(&rfds);
  FD_SET(sock, &rfds);
  tv.tv_sec = (long)(timeout_ms / 1000);
  tv.tv_usec = (long)((timeout_ms % 1000) * 1000);
  r = select(0, &rfds, NULL, NULL, &tv);
  if (r < 0)
    return -1;
  return r;
}

static int serial_read_for_transfer(struct mxt_device *mxt, void *buf, size_t max_len,
                                    size_t min_len, size_t *out_len, int timeout_ms)
{
  int ret;

  if (min_len > max_len)
    return MXT_ERROR_BAD_INPUT;

  ret = serial_read_exact(mxt, (uint8_t *)buf, min_len, timeout_ms);
  if (ret != MXT_SUCCESS)
    return ret;

  *out_len = min_len;
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
  mxt->serial.is_tcp_proxy = false;
  fprintf(stdout, "SERIAL_TEST: uart open %s\n", path);
  fflush(stdout);
  mxt_info(mxt->ctx, "已打开串口 %s", path);
  return MXT_SUCCESS;
}

static void serial_close_port(struct mxt_device *mxt)
{
  if (!mxt->serial.handle)
    return;

  if (mxt->serial.is_tcp_proxy) {
    closesocket((SOCKET)(uintptr_t)mxt->serial.handle);
    mxt->serial.is_tcp_proxy = false;
  } else {
    CloseHandle((HANDLE)mxt->serial.handle);
  }
  mxt->serial.handle = NULL;
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

  serial_trace_io(mxt, "TX", (const uint8_t *)mode0_cmd, sizeof(mode0_cmd) - 1);
  ret = serial_write_all(mxt, mode0_cmd, sizeof(mode0_cmd) - 1);
  if (ret != MXT_SUCCESS) {
    mxt_err(mxt->ctx, "发送 mode0 失败 (%s)", serial_rc_string(ret));
    return ret;
  }

  fprintf(stdout, "SERIAL_TEST: mode0 sent, waiting MCU reply...\n");
  fflush(stdout);
  serial_sleep_ms(80);
  ret = serial_wait_rx_contains(mxt, "OK:", 1500);
  if (ret != MXT_SUCCESS) {
    mxt_err(mxt->ctx, "mode0 未收到 MCU OK 应答 (%s)", serial_rc_string(ret));
    fprintf(stdout, "SERIAL_TEST: mode0 FAIL (%s)\n", serial_rc_string(ret));
    fflush(stdout);
    return ret;
  }
  fprintf(stdout, "SERIAL_TEST: mode0 handshake done\n");
  fflush(stdout);
  return MXT_SUCCESS;
}

static int serial_transfer(struct mxt_device *mxt, void *cmd, int cmd_size,
                           void *response, int response_size, bool ignore_response,
                           size_t min_response_bytes)
{
  int ret;
  size_t bytes_read = 0;

  if (!mxt->serial.device_connected)
    return MXT_ERROR_NO_DEVICE;

  ret = serial_write_all(mxt, cmd, (size_t)cmd_size);
  if (ret != MXT_SUCCESS)
    return ret;

  serial_trace_io(mxt, "TX", (const uint8_t *)cmd, (size_t)cmd_size);
  mxt_log_buffer(mxt->ctx, LOG_VERBOSE, "TX:", cmd, cmd_size);

  if (ignore_response)
    return MXT_SUCCESS;

  memset(response, 0, (size_t)response_size);
  ret = serial_read_for_transfer(mxt, response, (size_t)response_size,
                                 min_response_bytes, &bytes_read,
                                 SERIAL_TRANSFER_TIMEOUT_MS);
  if (ret != MXT_SUCCESS)
    return ret;

  serial_trace_io(mxt, "RX", (const uint8_t *)response, bytes_read);
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

  fprintf(stdout, "SERIAL_TEST: sending FIND_IIC (0xE0)...\n");
  fflush(stdout);
  ret = serial_transfer(mxt, pkt, 1, pkt, sizeof(pkt), false, 2);
  if (ret) {
    mxt_err(mxt->ctx, "FIND_IIC 失败 (%s)", serial_rc_string(ret));
    fprintf(stdout, "SERIAL_TEST: FIND_IIC FAIL (%s)\n", serial_rc_string(ret));
    fflush(stdout);
    return ret;
  }

  response = pkt[1];
  if (response == 0x81) {
    mxt_err(mxt->ctx, "桥接芯片未找到 I2C 设备");
    fprintf(stdout, "SERIAL_TEST: FIND_IIC NO_DEVICE\n");
    fflush(stdout);
    return MXT_ERROR_NO_DEVICE;
  }

  if (response < 0x4a) {
    mxt->serial.bootloader = true;
    mxt_info(mxt->ctx, "桥接发现 Bootloader @0x%02X", response);
    fprintf(stdout, "SERIAL_TEST: FIND_IIC OK bootloader=0x%02X\n", response);
  } else {
    mxt->serial.address = response;
    mxt_info(mxt->ctx, "桥接发现应用模式 @0x%02X", response);
    fprintf(stdout, "SERIAL_TEST: FIND_IIC OK addr=0x%02X\n", response);
  }
  fflush(stdout);
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
  /* MCU 应答 [01][status][00][data…]，与 usb_read_register 一致 */
  response_ofs = 1;

  ret = serial_transfer(mxt, pkt, (int)cmd_size, pkt, sizeof(pkt), false, 3U + count);
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
  /* MCU 写应答 [01][status] */
  response_ofs = 1;

  ret = serial_transfer(mxt, pkt, (int)(cmd_size + count), pkt, sizeof(pkt), ignore_response,
                        ignore_response ? 0U : 2U);
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

  ret = serial_transfer(mxt, pkt, 1, pkt, sizeof(pkt), false, 2);
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
  mxt->serial.is_tcp_proxy = false;
  mxt->serial.rx_carry_len = 0U;

  if (!strncmp(path, "proxy:", 6))
    ret = serial_open_proxy(mxt, path + 6);
  else
    ret = serial_open_port(mxt, path);
  if (ret)
    return ret;

  mxt->serial.device_connected = true;
  if (mxt->serial.is_tcp_proxy)
    serial_sleep_ms(80);
  serial_flush_rx(mxt);
  fprintf(stdout, "SERIAL_TEST: link open path=%s (%s)\n",
          path, mxt->serial.is_tcp_proxy ? "proxy" : "uart");
  fflush(stdout);

  if (mxt->conn->b_i2c_addr == 0x00) {
    if (mxt->serial.is_tcp_proxy) {
      ret = serial_prepare_mcu_bridge(mxt);
      if (ret) {
        serial_close_port(mxt);
        mxt->serial.device_connected = false;
        return ret;
      }
      ret = bridge_find_i2c_address(mxt);
    } else {
      ret = bridge_find_i2c_address(mxt);
      if (ret != MXT_SUCCESS) {
        fprintf(stdout, "SERIAL_TEST: FIND_IIC probe failed, sending mode0...\n");
        fflush(stdout);
        ret = serial_prepare_mcu_bridge(mxt);
        if (ret) {
          mxt_err(mxt->ctx, "串口桥接准备失败 (%s)", serial_rc_string(ret));
          serial_close_port(mxt);
          mxt->serial.device_connected = false;
          return ret;
        }
        ret = bridge_find_i2c_address(mxt);
      }
    }
    if (ret) {
      serial_close_port(mxt);
      mxt->serial.device_connected = false;
      return ret;
    }
  }

  mxt->serial.device_connected = true;
  mxt_info(mxt->ctx, "Device registered on serial:%s", path);
  fprintf(stdout, "SERIAL_TEST: link OK i2c=0x%02X\n",
          mxt->serial.bootloader ? 0 : (unsigned)mxt->serial.address);
  fflush(stdout);
  return MXT_SUCCESS;
}

int serial_run_link_test(struct libmaxtouch_ctx *ctx, struct mxt_conn_info *conn)
{
  struct mxt_device *mxt = NULL;
  int ret;

  if (!ctx || !conn || conn->type != E_SERIAL) {
    fprintf(stdout, "SERIAL_TEST: FAIL bad input\n");
    fflush(stdout);
    return MXT_ERROR_BAD_INPUT;
  }

  fprintf(stdout, "SERIAL_TEST: begin path=%s\n", conn->serial.path);
  fflush(stdout);

  ret = mxt_new_device(ctx, conn, &mxt);
  if (ret) {
    fprintf(stdout, "SERIAL_TEST: FAIL ret=%d (%s)\n", ret, serial_rc_string(ret));
    fflush(stdout);
    return ret;
  }

  fprintf(stdout, "SERIAL_TEST: PASS connected\n");
  fflush(stdout);
  mxt_free_device(mxt);
  return MXT_SUCCESS;
}

void serial_release(struct mxt_device *mxt)
{
  serial_close_port(mxt);
  mxt->serial.device_connected = false;
}
