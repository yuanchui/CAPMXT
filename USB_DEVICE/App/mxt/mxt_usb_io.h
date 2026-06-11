#ifndef MXT_USB_IO_H
#define MXT_USB_IO_H
#include <stdint.h>
#include "usbd_cdc.h"
extern USBD_HandleTypeDef hUsbDeviceFS;
uint8_t CDC_Transmit_FS(uint8_t *Buf, uint16_t Len);
void SendResponse(uint8_t *data, uint16_t len);
uint8_t USB_SendString(const char *str);
uint8_t USB_Printf(const char *format, ...);
uint8_t USB_SendRaw(const uint8_t *data, uint16_t len);
uint8_t USB_IsReady(void);
uint8_t USB_Flush(void);
uint8_t USB_FlushNonBlocking(void);
void MSG_BufferWrite(const char *str);
uint8_t MSG_BufferFlush(void);
void MXT_FlushMessageBuffer(void);
void MXT_WaitUsbIdle(uint32_t timeout_ms);
#endif
