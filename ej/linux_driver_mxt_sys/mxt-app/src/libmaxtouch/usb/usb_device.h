#pragma once
//------------------------------------------------------------------------------
/// \file   usb_device.h
/// \brief  Headers for MXT device low level access via USB
/// \author Tim Culmer


#include <libusb-1.0/libusb.h>
#define USB_MAX_BUS_DEVICES  127

//******************************************************************************
/// \brief USB library context
struct usb_context {
  libusb_context *libusb_ctx;
};

//******************************************************************************
/// \brief USB library context
struct usb_conn_info {
  int bus;
  int device;
  /* Bridge i2c address */
  int b_i2c_addr;
};

//******************************************************************************
/// \brief USB device information
struct usb_device {
  bool device_connected;
  bool bridge_chip;
  libusb_device *device;
  libusb_device_handle *handle;
  struct libusb_device_descriptor desc;
  int ep1_in_max_packet_size;
  int interface;
  bool bootloader;
  int report_id;
  int address;
  int b_i2c_addr;
  bool sent_btlr_cmd;
};

int usb_scan(struct libmaxtouch_ctx *ctx, struct mxt_conn_info **conn);
int usb_open(struct mxt_device *mxt);
int usb_close(struct libmaxtouch_ctx *ctx);
void usb_release(struct mxt_device *mxt);
int usb_reset_chip(struct mxt_device *mxt, bool bootloader_mode, uint16_t reset_time_ms);
int usb_read_register(struct mxt_device *mxt, unsigned char *buf, uint16_t start_register, size_t count, size_t *bytes_transferred);
int usb_write_register(struct mxt_device *mxt, unsigned char const *buf, uint16_t start_register, size_t count);
int usb_bootloader_read(struct mxt_device *mxt, unsigned char *buf, size_t count);
int usb_bootloader_write(struct mxt_device *mxt, unsigned char const *buf, size_t count);
bool usb_is_bootloader(struct mxt_device *mxt);
int usb_read_chg(struct mxt_device *mxt, bool *value);
int usb_find_bus_devices(struct mxt_device *mxt, bool *device_list);
int usb_rediscover_device(struct mxt_device *mxt, bool *device_list);
