#include "mxt_usb_io.h"
#include "mxt_state.h"
#include "mxt_work.h"
#include "mxt_config.h"
#include "mxt_i2c.h"
#include "usbd_cdc_if.h"
#include "usbd_cdc.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

void SendResponse(uint8_t *data, uint16_t len)
{
    uint32_t timeout = 10000;
    /* 等待上一包发送完成；若 pClassData 尚未初始化则直接返回 */
    if (hUsbDeviceFS.pClassData == NULL) {
        return;
    }

    while (((USBD_CDC_HandleTypeDef*)(hUsbDeviceFS.pClassData))->TxState != 0 && timeout--) {}

    /* 发送并在 USBD_BUSY 时重试，避免大帧/连续帧时被静默丢弃 */
    for (uint16_t tries = 0; tries < 1000; tries++) {
        if (CDC_Transmit_FS(data, len) == USBD_OK) {
            break;
        }
        MXT_DelayUs(500);
    }
}


uint8_t USB_SendString(const char *str)
{
  if(str == NULL) return USBD_FAIL;
  if(hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) return USBD_FAIL;

  /* mode0(桥接/二进制) 下禁止输出任何字符串，避免干扰二进制通信 */
  if (g_bridge_mode == BRIDGE_MODE_BINARY) {
    return USBD_OK;
  }

  /* mode1(字符串) 下通过消息缓冲区发送，避免阻塞 */
  MSG_BufferWrite(str);
  return USBD_OK;
}


uint8_t USB_Printf(const char *format, ...)
{
  va_list args;
  int len;

  if(hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) return USBD_FAIL;

  /* mode0(桥接/二进制) 下禁止输出任何字符串，避免干扰二进制通信 */
  if (g_bridge_mode == BRIDGE_MODE_BINARY) {
    return USBD_OK;
  }

  va_start(args, format);
  len = vsnprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE - 1, format, args);
  va_end(args);

  if (len > 0) {
    MXT_WORK_STR[len] = '\0';
    MSG_BufferWrite(MXT_WORK_STR);
    return USBD_OK;
  }
  return USBD_FAIL;
}


uint8_t USB_Flush(void) { return USBD_OK; }


uint8_t USB_SendRaw(const uint8_t *data, uint16_t len)
{
  if (data == NULL) return USBD_FAIL;
  return CDC_Transmit_FS((uint8_t*)data, len);
}


uint8_t USB_IsReady(void) { return (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED); }


uint8_t USB_FlushNonBlocking(void) { return USBD_OK; }


void MSG_BufferWrite(const char *str)
{
  if (str == NULL) return;
  
  uint16_t len = strlen(str);

  // 缓冲不够：先尝试刷新，最多等一定时间，避免把主循环卡死
  const uint32_t t0 = HAL_GetTick();
  const uint32_t MAX_WAIT_MS = 200;

  while (1) {
    uint16_t available = 0;
    if (g_msg_buffer_head >= g_msg_buffer_tail) {
      available = MSG_BUFFER_SIZE - (g_msg_buffer_head - g_msg_buffer_tail) - 1;
    } else {
      available = g_msg_buffer_tail - g_msg_buffer_head - 1;
    }

    if (len <= available) break;

    // USB 未配置时无法刷新
    if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) {
      g_msg_buffer_overflow = 1;
      return;
    }

    if ((HAL_GetTick() - t0) > MAX_WAIT_MS) {
      // 超时仍无法写入：置位溢出标志并退出，避免“满了下次不响应”
      g_msg_buffer_overflow = 1;
      return;
    }

    // 尝试刷新一部分；如果 USB 正忙就稍等
    if (MSG_BufferFlush() == 0) {
      HAL_Delay(1);
    }
  }

  // 写入数据
  for (uint16_t i = 0; i < len; i++) {
    g_msg_buffer[g_msg_buffer_head] = str[i];
    g_msg_buffer_head = (g_msg_buffer_head + 1) % MSG_BUFFER_SIZE;
  }
}


uint8_t MSG_BufferFlush(void)
{
  // 检查缓冲区是否为空
  if (g_msg_buffer_head == g_msg_buffer_tail) {
    return 0;
  }
  
  // 检查USB是否就绪
  if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) {
    return 0;
  }
  
  // 上一包仍在发送时不推进 tail，避免覆盖正在发送的数据
  USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassData;
  if (hcdc->TxState != 0) {
    return 0;
  }
  
  // 计算可发送的数据量 (最多 MSG_FLUSH_CHUNK 字节)
  uint16_t to_send = 0;
  if (g_msg_buffer_head > g_msg_buffer_tail) {
    to_send = g_msg_buffer_head - g_msg_buffer_tail;
  } else {
    to_send = MSG_BUFFER_SIZE - g_msg_buffer_tail;
  }
  
  if (to_send > MSG_FLUSH_CHUNK) {
    to_send = MSG_FLUSH_CHUNK;
  }
  
  // 拷贝到专用 TX 缓冲再发送，避免 CDC 异步发送时环形缓冲被后续写入覆盖
  for (uint16_t i = 0; i < to_send; i++) {
    g_msg_tx_chunk[i] = (uint8_t)g_msg_buffer[(g_msg_buffer_tail + i) % MSG_BUFFER_SIZE];
  }
  if (CDC_Transmit_FS(g_msg_tx_chunk, to_send) == USBD_OK) {
    g_msg_buffer_tail = (g_msg_buffer_tail + to_send) % MSG_BUFFER_SIZE;
  }

  // 如果上一轮写入阶段因为缓冲写入超时置位了 overflow，
  // 且现在缓冲已被消化（空了），提示一次避免“沉默卡死”难排查。
  if (g_msg_buffer_overflow && g_msg_buffer_head == g_msg_buffer_tail) {
    g_msg_buffer_overflow = 0;
    USB_SendString("\r\n[WARN: Message buffer overflow (write timed out, partial output possible)]\r\n");
  }
  
  return 1;
}


void MXT_FlushMessageBuffer(void)
{
  for (int i = 0; i < 256; i++) {
    if (MSG_BufferFlush() == 0) break;  /* 缓冲区空或USB忙则停止 */
  }
}

void MXT_WaitUsbIdle(uint32_t timeout_ms)
{
  uint32_t t0 = HAL_GetTick();

  while ((HAL_GetTick() - t0) < timeout_ms) {
    MXT_FlushMessageBuffer();
    if (hUsbDeviceFS.pClassData != NULL) {
      USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
      if ((hcdc->TxState == 0U) && (g_msg_buffer_head == g_msg_buffer_tail)) {
        return;
      }
    }
    HAL_Delay(1);
  }
}

