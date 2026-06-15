//------------------------------------------------------------------------------
/// \file   mxt_win_stubs.c
/// \brief  Windows build: stub implementations for Linux-only backends.
///         All functions return error (MXT_ERROR_NOT_SUPPORTED or similar).
//------------------------------------------------------------------------------

#ifdef MXT_OS_WINDOWS

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include <stddef.h>

#include "libmaxtouch/libmaxtouch.h"
#include "libmaxtouch/log.h"
#include "libmaxtouch/mxt_win_stubs.h"

#define STUB_RETURN_ERROR (MXT_ERROR_NOT_SUPPORTED)

/* Windows has no __fpurge; provide a no-op so callers link. */
void __fpurge(FILE *stream)
{
  (void)stream;
}

/* sysfs stubs */
int sysfs_reset_chip(struct mxt_device *mxt) { (void)mxt; return STUB_RETURN_ERROR; }
int sysfs_set_bootloader(struct mxt_device *mxt, bool value) { (void)mxt; (void)value; return STUB_RETURN_ERROR; }
int sysfs_get_bootloader(struct mxt_device *mxt, bool *value) { (void)mxt; (void)value; return STUB_RETURN_ERROR; }
int sysfs_scan(struct libmaxtouch_ctx *ctx, struct mxt_conn_info **conn) { (void)ctx; (void)conn; return STUB_RETURN_ERROR; }
int sysfs_spi_scan(struct libmaxtouch_ctx *ctx, struct mxt_conn_info **conn) { (void)ctx; (void)conn; return STUB_RETURN_ERROR; }
int sysfs_open_i2c(struct mxt_device *mxt) { (void)mxt; return STUB_RETURN_ERROR; }
int sysfs_open_spi(struct mxt_device *mxt) { (void)mxt; return STUB_RETURN_ERROR; }
void sysfs_release(struct mxt_device *mxt) { (void)mxt; }
int sysfs_new_device(struct libmaxtouch_ctx *ctx, struct mxt_conn_info **conn, const char *dirname) { (void)ctx; (void)conn; (void)dirname; return STUB_RETURN_ERROR; }
int sysfs_read_register(struct mxt_device *mxt, unsigned char *buf, int start_register, size_t count, size_t *bytes_transferred) { (void)mxt; (void)buf; (void)start_register; (void)count; (void)bytes_transferred; return STUB_RETURN_ERROR; }
int sysfs_bootloader_read(struct mxt_device *mxt, unsigned char *buf, int count) { (void)mxt; (void)buf; (void)count; return STUB_RETURN_ERROR; }
int sysfs_bootloader_write(struct mxt_device *mxt, unsigned const char *buf, int count) { (void)mxt; (void)buf; (void)count; return STUB_RETURN_ERROR; }
int sysfs_write_register(struct mxt_device *mxt, unsigned char const *buf, int start_register, size_t count, size_t padding) { (void)mxt; (void)buf; (void)start_register; (void)count; (void)padding; return STUB_RETURN_ERROR; }
int sysfs_set_debug(struct mxt_device *mxt, bool debug_state) { (void)mxt; (void)debug_state; return STUB_RETURN_ERROR; }
int sysfs_get_debug(struct mxt_device *mxt, bool *value) { (void)mxt; (void)value; return STUB_RETURN_ERROR; }
int sysfs_set_debug_irq(struct mxt_device *mxt, bool debug_state) { (void)mxt; (void)debug_state; return STUB_RETURN_ERROR; }
int sysfs_get_crc_enabled(struct mxt_device *mxt, bool *value) { (void)mxt; (void)value; return STUB_RETURN_ERROR; }
char *sysfs_get_directory(struct mxt_device *mxt) { (void)mxt; return NULL; }
bool sysfs_has_debug_v2(struct mxt_device *mxt) { (void)mxt; return false; }
char *sysfs_get_msg_string_v2(struct mxt_device *mxt) { (void)mxt; return NULL; }
int sysfs_get_msg_bytes_v2(struct mxt_device *mxt, unsigned char *buf, size_t buflen, int *count) { (void)mxt; (void)buf; (void)buflen; (void)count; return STUB_RETURN_ERROR; }
int sysfs_get_msgs_v2(struct mxt_device *mxt, int *count) { (void)mxt; (void)count; return STUB_RETURN_ERROR; }
int sysfs_msg_reset_v2(struct mxt_device *mxt) { (void)mxt; return STUB_RETURN_ERROR; }
int sysfs_get_debug_v2_fd(struct mxt_device *mxt) { (void)mxt; return -1; }
int sysfs_get_i2c_address(struct libmaxtouch_ctx *ctx, struct mxt_conn_info *conn, int *adapter, int *address) { (void)ctx; (void)conn; (void)adapter; (void)address; return STUB_RETURN_ERROR; }

/* debugfs stubs */
int debugfs_scan(struct mxt_device *mxt) { (void)mxt; return STUB_RETURN_ERROR; }
int debugfs_open(struct mxt_device *mxt) { (void)mxt; return STUB_RETURN_ERROR; }
int debugfs_set_irq(struct mxt_device *mxt, bool enable) { (void)mxt; (void)enable; return STUB_RETURN_ERROR; }
int debugfs_get_debug_irq(struct mxt_device *mxt, bool *value) { (void)mxt; (void)value; return STUB_RETURN_ERROR; }
int debugfs_set_debug_irq(struct mxt_device *mxt, bool debug_state) { (void)mxt; (void)debug_state; return STUB_RETURN_ERROR; }
int debugfs_get_tx_seq_num(struct mxt_device *mxt, uint16_t *value) { (void)mxt; (void)value; return STUB_RETURN_ERROR; }
int debugfs_set_tx_seq_num(struct mxt_device *mxt, uint8_t value) { (void)mxt; (void)value; return STUB_RETURN_ERROR; }
int debugfs_get_crc_enabled(struct mxt_device *mxt, bool *value) { (void)mxt; (void)value; return STUB_RETURN_ERROR; }
int debugfs_update_seq_num(struct mxt_device *mxt, uint8_t value) { (void)mxt; (void)value; return STUB_RETURN_ERROR; }

/* i2c_dev stubs */
int i2c_dev_open(struct mxt_device *mxt) { (void)mxt; return STUB_RETURN_ERROR; }
void i2c_dev_release(struct mxt_device *mxt) { (void)mxt; }
int i2c_dev_read_register(struct mxt_device *mxt, unsigned char *buf, int start_register, int count, size_t *bytes_transferred) { (void)mxt; (void)buf; (void)start_register; (void)count; (void)bytes_transferred; return STUB_RETURN_ERROR; }
int i2c_dev_write_register(struct mxt_device *mxt, unsigned char const *buf, int start_register, size_t count) { (void)mxt; (void)buf; (void)start_register; (void)count; return STUB_RETURN_ERROR; }
int i2c_dev_write_crc(struct mxt_device *mxt, unsigned char const *buf, int start_register, size_t count) { (void)mxt; (void)buf; (void)start_register; (void)count; return STUB_RETURN_ERROR; }
int i2c_dev_bootloader_read(struct mxt_device *mxt, unsigned char *buf, int count) { (void)mxt; (void)buf; (void)count; return STUB_RETURN_ERROR; }
int i2c_dev_bootloader_write(struct mxt_device *mxt, unsigned char const *buf, int count, size_t *bytes_transferred) { (void)mxt; (void)buf; (void)count; (void)bytes_transferred; return STUB_RETURN_ERROR; }

/* hidraw stubs */
int hidraw_register(struct mxt_device *mxt) { (void)mxt; return STUB_RETURN_ERROR; }
int hidraw_read(struct mxt_device *mxt) { (void)mxt; return STUB_RETURN_ERROR; }
int hidraw_read_register(struct mxt_device *mxt, unsigned char *buf, uint16_t start_register, size_t count, size_t *bytes_transferred) { (void)mxt; (void)buf; (void)start_register; (void)count; (void)bytes_transferred; return STUB_RETURN_ERROR; }
int hidraw_write_register(struct mxt_device *mxt, unsigned char const *val, uint16_t start_register, int datalength) { (void)mxt; (void)val; (void)start_register; (void)datalength; return STUB_RETURN_ERROR; }
void hidraw_release(struct mxt_device *mxt) { (void)mxt; }
int hidraw_scan(struct libmaxtouch_ctx *ctx, struct mxt_conn_info **conn) { (void)ctx; (void)conn; return STUB_RETURN_ERROR; }

#endif /* MXT_OS_WINDOWS */
