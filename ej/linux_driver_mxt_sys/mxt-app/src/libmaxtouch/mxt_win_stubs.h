#pragma once
//------------------------------------------------------------------------------
/// \file   mxt_win_stubs.h
/// \brief  Windows build: struct definitions and stub declarations for
///         Linux-only backends (sysfs, debugfs, i2c_dev, hidraw).
///         Used when MXT_OS_WINDOWS is defined (USB-only CLI).
//------------------------------------------------------------------------------

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct libmaxtouch_ctx;
struct mxt_device;
struct mxt_conn_info;

/* dmesg (referenced by sysfs_device) */
struct dmesg_item {
  void *opaque;
};

/* sysfs */
struct sysfs_conn_info {
  char *path;
  bool acpi;
  int i2c_bus;
  int i2c_addr;
  int spi_cs;
  int spi_bus;
  bool spi_found;
  bool i2c_found;
};

struct sysfs_device {
  struct sysfs_conn_info conn;
  char *mem_access_path;
  char *temp_path;
  size_t path_max;
  bool debug_v2;
  uint16_t debug_v2_msg_count;
  uint16_t debug_v2_msg_ptr;
  uint8_t *debug_v2_msg_buf;
  char *debug_msg_buf;
  int debug_msg_buf_size;
  int debug_notify_fd;
  size_t debug_v2_size;
  bool b_i2c_device;
  bool b_spi_device;
  int dmesg_count;
  struct dmesg_item *dmesg_head;
  struct dmesg_item *dmesg_ptr;
  unsigned long timestamp;
  unsigned long mtimestamp;
};

/* i2c_dev */
#define I2C_DEV_MAX_BLOCK 255
struct i2c_dev_conn_info {
  int adapter;
  int address;
};
struct i2c_dev_device {
  uint16_t t38_addr;
  uint16_t t38_size;
};

/* hidraw */
#define HIDRAW_REPORT_ID 0x06
struct hidraw_conn_info {
  char node[20];
  uint8_t report_id;
  int fd;
};

/* debugfs */
struct debugfs_device {
  char *tmp_path;
  size_t dir_max;
  char *file_path;
  bool enabled;
};

/* Stub function declarations (implemented in *_stub.c on Windows) */
int sysfs_reset_chip(struct mxt_device *mxt);
int sysfs_set_bootloader(struct mxt_device *mxt, bool value);
int sysfs_get_bootloader(struct mxt_device *mxt, bool *value);
int sysfs_scan(struct libmaxtouch_ctx *ctx, struct mxt_conn_info **conn);
int sysfs_spi_scan(struct libmaxtouch_ctx *ctx, struct mxt_conn_info **conn);
int sysfs_open_i2c(struct mxt_device *mxt);
int sysfs_open_spi(struct mxt_device *mxt);
void sysfs_release(struct mxt_device *mxt);
int sysfs_new_device(struct libmaxtouch_ctx *ctx, struct mxt_conn_info **conn, const char *dirname);
int sysfs_read_register(struct mxt_device *mxt, unsigned char *buf, int start_register, size_t count, size_t *bytes_transferred);
int sysfs_bootloader_read(struct mxt_device *mxt, unsigned char *buf, int count);
int sysfs_bootloader_write(struct mxt_device *mxt, unsigned const char *buf, int count);
int sysfs_write_register(struct mxt_device *mxt, unsigned char const *buf, int start_register, size_t count, size_t padding);
int sysfs_set_debug(struct mxt_device *mxt, bool debug_state);
int sysfs_get_debug(struct mxt_device *mxt, bool *value);
int sysfs_set_debug_irq(struct mxt_device *mxt, bool debug_state);
int sysfs_get_crc_enabled(struct mxt_device *mxt, bool *value);
char *sysfs_get_directory(struct mxt_device *mxt);
bool sysfs_has_debug_v2(struct mxt_device *mxt);
char *sysfs_get_msg_string_v2(struct mxt_device *mxt);
int sysfs_get_msg_bytes_v2(struct mxt_device *mxt, unsigned char *buf, size_t buflen, int *count);
int sysfs_get_msgs_v2(struct mxt_device *mxt, int *count);
int sysfs_msg_reset_v2(struct mxt_device *mxt);
int sysfs_get_debug_v2_fd(struct mxt_device *mxt);
int sysfs_get_i2c_address(struct libmaxtouch_ctx *ctx, struct mxt_conn_info *conn, int *adapter, int *address);

int debugfs_scan(struct mxt_device *mxt);
int debugfs_open(struct mxt_device *mxt);
int debugfs_set_irq(struct mxt_device *mxt, bool enable);
int debugfs_get_debug_irq(struct mxt_device *mxt, bool *value);
int debugfs_set_debug_irq(struct mxt_device *mxt, bool debug_state);
int debugfs_get_tx_seq_num(struct mxt_device *mxt, uint16_t *value);
int debugfs_set_tx_seq_num(struct mxt_device *mxt, uint8_t value);
int debugfs_get_crc_enabled(struct mxt_device *mxt, bool *value);
int debugfs_update_seq_num(struct mxt_device *mxt, uint8_t value);

int i2c_dev_open(struct mxt_device *mxt);
void i2c_dev_release(struct mxt_device *mxt);
int i2c_dev_read_register(struct mxt_device *mxt, unsigned char *buf, int start_register, int count, size_t *bytes_transferred);
int i2c_dev_write_register(struct mxt_device *mxt, unsigned char const *buf, int start_register, size_t count);
int i2c_dev_write_crc(struct mxt_device *mxt, unsigned char const *buf, int start_register, size_t count);
int i2c_dev_bootloader_read(struct mxt_device *mxt, unsigned char *buf, int count);
int i2c_dev_bootloader_write(struct mxt_device *mxt, unsigned char const *buf, int count, size_t *bytes_transferred);

int hidraw_register(struct mxt_device *mxt);
int hidraw_read(struct mxt_device *mxt);
int hidraw_read_register(struct mxt_device *mxt, unsigned char *buf, uint16_t start_register, size_t count, size_t *bytes_transferred);
int hidraw_write_register(struct mxt_device *mxt, unsigned char const *val, uint16_t start_register, int datalength);
void hidraw_release(struct mxt_device *mxt);
int hidraw_scan(struct libmaxtouch_ctx *ctx, struct mxt_conn_info **conn);
