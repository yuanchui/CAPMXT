#pragma once
//------------------------------------------------------------------------------
/// \file   serial_device.h
/// \brief  MXT device low level access via serial/COM (STM32 VCP bridge)
//------------------------------------------------------------------------------

#define SERIAL_MAX_PACKET_SIZE  64
#define SERIAL_TRANSFER_TIMEOUT_MS 2000

struct serial_conn_info {
  char path[128];
};

struct serial_device {
  bool device_connected;
  bool bridge_chip;
  int ep1_in_max_packet_size;
  bool bootloader;
  int address;
  int b_i2c_addr;
  bool sent_btlr_cmd;
#ifdef _WIN32
  void *handle;
#else
  int fd;
#endif
};

int serial_scan(struct libmaxtouch_ctx *ctx, struct mxt_conn_info **conn);
int serial_open(struct mxt_device *mxt);
void serial_release(struct mxt_device *mxt);
int serial_reset_chip(struct mxt_device *mxt, bool bootloader_mode, uint16_t reset_time_ms);
int serial_read_register(struct mxt_device *mxt, unsigned char *buf,
                         uint16_t start_register, size_t count,
                         size_t *bytes_transferred);
int serial_write_register(struct mxt_device *mxt, unsigned char const *buf,
                          uint16_t start_register, size_t count);
int serial_bootloader_read(struct mxt_device *mxt, unsigned char *buf, size_t count);
int serial_bootloader_write(struct mxt_device *mxt, unsigned char const *buf, size_t count);
bool serial_is_bootloader(struct mxt_device *mxt);
int serial_read_chg(struct mxt_device *mxt, bool *value);
int serial_find_bus_devices(struct mxt_device *mxt, bool *device_list);
int serial_rediscover_device(struct mxt_device *mxt, bool *device_list);
