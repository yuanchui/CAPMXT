#include "mxt_spi_stream.h"
#include "mxt_config.h"
#include "mxt_state.h"
#include "mxt_touch.h"
#include "mxt_usb_io.h"
#include "usbd_cdc_if.h"
#include "main.h"
#include "spi.h"
#include "stm32f1xx_hal_spi.h"
#include <stdio.h>
#include <string.h>

extern SPI_HandleTypeDef hspi1;
extern USBD_HandleTypeDef hUsbDeviceFS;

static const char g_hex_digits[] = "0123456789ABCDEF";
static uint8_t g_spi_usb_buf[2][SPI_USB_PKT_SIZE];
static volatile uint8_t g_spi_usb_buf_idx;

static void SPIUSB_LineEnqueue(const char *s);
static void SPIUSB_HexEnqueueByte(uint8_t b);
static void SPIUSB_ByteEnqueue(const uint8_t *data, uint16_t len);
static void SPIUSB_Start1_HandlePageMarker(void);
static void SPIUSB_Start1_ProcessPayloadByte(uint8_t b);
static void SPIUSB_Start3_ProcessCroppedByte(uint8_t b);
static void SPIUSB_Start3_EmitRowPacket(void);

void SPIUSB_ResetState(uint8_t mode)
{
  g_spi_stream_mode = mode;
  g_spi_in_frame = 0U;
  g_spi_frame_bytes = 0U;
  g_spi_nss_prev = (HAL_GPIO_ReadPin(SSN_GPIO_Port, SSN_Pin) == GPIO_PIN_SET) ? 1U : 0U;
  g_spi_hex_tx_len = 0U;
  g_spi_start1_nss_page = 0U;
  g_spi_start1_collecting = 0U;
  g_spi_start1_payload_bytes = 0U;
  g_spi_start1_row_bytes = 0U;
  g_spi_start1_src_row_bytes = 0U;
  g_spi_start3_frame_id = 0U;
  g_spi_start3_row_id = 0U;
  g_spi_start3_row_len = 0U;
  g_spi_rx_q_head = 0U;
  g_spi_rx_q_tail = 0U;
  g_spi_rx_overflow = 0U;
  g_spi_last_irq_ms = 0U;
  g_spi_usb_buf_idx = 0U;
}

void MXT_SPI_PrepareStream(uint8_t mode)
{
  if (!g_touch_inited) {
    MXT_InitTouchScreen();
  }
  (void)MXT_ApplyStartupDebugCtrl();
  MXT_FlushMessageBuffer();
  MXT_WaitUsbIdle(200U);
  SPIUSB_ResetState(mode);
  g_spi_check_requested = 1U;
  MXT_SPI_StartIT();
}

void MXT_SPI_StartIT(void)
{
  if (g_spi_it_active) return;

  g_spi_rx_q_head = 0;
  g_spi_rx_q_tail = 0;
  g_spi_rx_overflow = 0;
  g_spi_last_irq_ms = HAL_GetTick();
  __HAL_SPI_CLEAR_OVRFLAG(&hspi1);

  if (HAL_SPI_Receive_IT(&hspi1, g_spi_it_rx_buf, SPI_IT_CHUNK_LEN) == HAL_OK) {
    g_spi_it_active = 1;
  } else {
    (void)HAL_SPI_Abort(&hspi1);
    __HAL_SPI_CLEAR_OVRFLAG(&hspi1);
    if (HAL_SPI_Receive_IT(&hspi1, g_spi_it_rx_buf, SPI_IT_CHUNK_LEN) == HAL_OK) {
      g_spi_it_active = 1;
    }
  }
}

void MXT_SPI_StopIT(void)
{
  (void)HAL_SPI_Abort(&hspi1);
  __HAL_SPI_CLEAR_OVRFLAG(&hspi1);
  g_spi_it_active = 0;
  g_spi_last_irq_ms = 0U;
}

void MXT_USB_ServiceTx(void)
{
  uint8_t budget = 32U;

  while ((g_spi_hex_tx_len > 0U) && (budget-- > 0U)) {
    SPIUSB_TryFlush();
    if (g_spi_hex_tx_len > 0U) {
      if (hUsbDeviceFS.pClassData != NULL) {
        USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
        if (hcdc->TxState != 0U) {
          break;
        }
      }
    }
  }
  (void)MSG_BufferFlush();
}

void MXT_ProcessSPICheck(void)
{
  uint16_t local_head;
  uint16_t local_tail;
  uint8_t *chunk;
  uint8_t nss_sample;
  uint8_t b;
  uint16_t drained = 0U;
  uint32_t stall_ms;

  if (!g_spi_it_active) {
    MXT_SPI_StartIT();
    if (!g_spi_it_active) return;
  }

  stall_ms = (g_spi_stream_enabled != 0U) ? SPI_GAP_IDLE_STALL_MS : SPI_STREAM_STALL_MS;
  if ((HAL_GetTick() - g_spi_last_irq_ms) > stall_ms) {
    MXT_SPI_StopIT();
    MXT_SPI_StartIT();
  }

  local_head = g_spi_rx_q_head;
  local_tail = g_spi_rx_q_tail;

  while (local_tail != local_head) {
    chunk = g_spi_rx_queue[local_tail];
    nss_sample = g_spi_nss_queue[local_tail];

    if (g_spi_stream_enabled == 0U) {
      g_spi_nss_prev = nss_sample;
      local_tail = (uint16_t)((local_tail + 1U) % SPI_RX_QUEUE_DEPTH);
      g_spi_rx_q_tail = local_tail;
      local_head = g_spi_rx_q_head;
      continue;
    }

    /* 640U：三次 SSN 中第一次下降沿开始收集本帧 640B 有效载荷 */
    if ((g_spi_stream_mode != 0U) && (g_spi_nss_prev != 0U) && (nss_sample == 0U)) {
      g_spi_in_frame = 1U;
      g_spi_frame_bytes = 0U;
      SPIUSB_Start1_HandlePageMarker();
    }
    g_spi_nss_prev = nss_sample;

    for (uint16_t i = 0; i < SPI_IT_CHUNK_LEN; i++) {
      b = chunk[i];
      if (g_spi_stream_mode == 0U) {
        SPIUSB_HexEnqueueByte(b);
      } else {
        SPIUSB_Start1_ProcessPayloadByte(b);
      }
      g_spi_frame_bytes++;
      drained++;
      if ((drained & 0x3FU) == 0U) {
        SPIUSB_TryFlush();
      }
    }

    local_tail = (uint16_t)((local_tail + 1U) % SPI_RX_QUEUE_DEPTH);
    g_spi_rx_q_tail = local_tail;
    local_head = g_spi_rx_q_head;
  }

  if (g_spi_rx_overflow > 0U) {
    uint32_t now = HAL_GetTick();
    if ((now - g_spi_last_overflow_report_ms) >= 200U) {
      g_spi_last_overflow_report_ms = now;
      g_spi_rx_overflow = 0U;
    }
  }

  SPIUSB_TryFlush();
}

void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
  uint16_t next_head;
  uint8_t nss_now;

  if (hspi->Instance != SPI1) return;
  g_spi_last_irq_ms = HAL_GetTick();

  nss_now = (HAL_GPIO_ReadPin(SSN_GPIO_Port, SSN_Pin) == GPIO_PIN_SET) ? 1U : 0U;

  next_head = (uint16_t)((g_spi_rx_q_head + 1U) % SPI_RX_QUEUE_DEPTH);
  if (next_head != g_spi_rx_q_tail) {
    for (uint16_t i = 0; i < SPI_IT_CHUNK_LEN; i++) {
      g_spi_rx_queue[g_spi_rx_q_head][i] = g_spi_it_rx_buf[i];
    }
    g_spi_nss_queue[g_spi_rx_q_head] = nss_now;
    g_spi_rx_q_head = next_head;
  } else {
    g_spi_rx_overflow++;
  }

  if (g_spi_check_requested) {
    if (HAL_SPI_Receive_IT(&hspi1, g_spi_it_rx_buf, SPI_IT_CHUNK_LEN) != HAL_OK) {
      g_spi_it_active = 0;
    }
  } else {
    g_spi_it_active = 0;
  }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
  if (hspi->Instance != SPI1) return;

  g_spi_err_count++;
  g_spi_it_active = 0;
  __HAL_SPI_CLEAR_OVRFLAG(hspi);

  if (g_spi_check_requested) {
    MXT_SPI_StartIT();
  }
}

static void SPIUSB_LineEnqueue(const char *s)
{
  while ((*s != '\0') && (g_spi_hex_tx_len < (uint16_t)(sizeof(g_spi_hex_tx_buf) - 1U))) {
    g_spi_hex_tx_buf[g_spi_hex_tx_len++] = *s++;
  }
}

static void SPIUSB_HexEnqueueByte(uint8_t b)
{
  uint16_t pos;
  uint8_t retries;

  for (retries = 0U; retries < 8U; retries++) {
    if (g_spi_hex_tx_len < (uint16_t)(sizeof(g_spi_hex_tx_buf) - 3U)) {
      break;
    }
    SPIUSB_TryFlush();
  }

  pos = g_spi_hex_tx_len;
  if (pos >= (uint16_t)(sizeof(g_spi_hex_tx_buf) - 3U)) {
    g_spi_tx_drop++;
    return;
  }

  g_spi_hex_tx_buf[pos] = (uint8_t)g_hex_digits[(b >> 4) & 0x0FU];
  g_spi_hex_tx_buf[pos + 1U] = (uint8_t)g_hex_digits[b & 0x0FU];
  g_spi_hex_tx_buf[pos + 2U] = (uint8_t)' ';
  g_spi_hex_tx_len = (uint16_t)(pos + 3U);
}

static void SPIUSB_ByteEnqueue(const uint8_t *data, uint16_t len)
{
  uint16_t i;
  uint8_t retries;

  if ((data == NULL) || (len == 0U)) {
    return;
  }

  for (i = 0U; i < len; i++) {
    for (retries = 0U; retries < 8U; retries++) {
      if (g_spi_hex_tx_len < (uint16_t)(sizeof(g_spi_hex_tx_buf) - 1U)) {
        break;
      }
      SPIUSB_TryFlush();
    }
    if (g_spi_hex_tx_len >= (uint16_t)(sizeof(g_spi_hex_tx_buf) - 1U)) {
      g_spi_tx_drop++;
      return;
    }
    g_spi_hex_tx_buf[g_spi_hex_tx_len++] = data[i];
  }
}

static void SPIUSB_Start1_HandlePageMarker(void)
{
  if (g_spi_start1_nss_page == 0U) {
    if (g_spi_stream_mode == 1U) {
      g_spi_start3_frame_id++;
      SPIUSB_HexEnqueueByte(g_spi_start3_frame_id);
      SPIUSB_LineEnqueue("\r\n");
    } else if (g_spi_stream_mode == 2U) {
      g_spi_start3_frame_id++;
      g_spi_start3_row_id = 0U;
      g_spi_start3_row_len = 0U;
    }

    g_spi_start1_collecting = 1U;
    g_spi_start1_payload_bytes = 0U;
    g_spi_start1_row_bytes = 0U;
    g_spi_start1_src_row_bytes = 0U;
  }

  g_spi_start1_nss_page++;
  if (g_spi_start1_nss_page >= 3U) {
    g_spi_start1_nss_page = 0U;
  }
}

static void SPIUSB_Start1_ProcessPayloadByte(uint8_t b)
{
  if (g_spi_start1_collecting == 0U) {
    return;
  }

  if (g_spi_start1_payload_bytes >= SPI_FRAME_PAYLOAD_BYTES) {
    return;
  }

  g_spi_start1_payload_bytes++;
  g_spi_start1_src_row_bytes++;

  /* 源数据每行 40B：跳过每行首字节(常见 0x80)，取后续 32B */
  if ((g_spi_start1_src_row_bytes > 1U) && (g_spi_start1_src_row_bytes <= 33U)) {
    if (g_spi_stream_mode == 1U) {
      SPIUSB_HexEnqueueByte(b);
      g_spi_start1_row_bytes++;
      if (g_spi_start1_row_bytes >= 32U) {
        SPIUSB_LineEnqueue("\r\n");
        g_spi_start1_row_bytes = 0U;
      }
    } else if (g_spi_stream_mode == 2U) {
      SPIUSB_Start3_ProcessCroppedByte(b);
    }
  }

  if (g_spi_start1_src_row_bytes >= 40U) {
    g_spi_start1_src_row_bytes = 0U;
  }

  if (g_spi_start1_payload_bytes >= SPI_FRAME_PAYLOAD_BYTES) {
    g_spi_start1_collecting = 0U;
    if ((g_spi_stream_mode == 1U) && (g_spi_start1_row_bytes != 0U)) {
      SPIUSB_LineEnqueue("\r\n");
      g_spi_start1_row_bytes = 0U;
    }
    SPIUSB_TryFlush();
  }
}

static void SPIUSB_Start3_ProcessCroppedByte(uint8_t b)
{
  if (g_spi_start3_row_len < sizeof(g_spi_start3_row_buf)) {
    g_spi_start3_row_buf[g_spi_start3_row_len++] = b;
  }

  if (g_spi_start3_row_len >= 32U) {
    SPIUSB_Start3_EmitRowPacket();
    g_spi_start3_row_len = 0U;
    g_spi_start3_row_id++;
  }
}

static void SPIUSB_Start3_EmitRowPacket(void)
{
  uint8_t packet[40];
  uint16_t crc;

  packet[0] = 0xAAU;
  packet[1] = 0x10U;
  packet[2] = 0x33U;
  packet[3] = 40U;
  packet[4] = g_spi_start3_frame_id;
  packet[5] = (uint8_t)(g_spi_start3_row_id & 0x0FU);

  for (uint8_t i = 0U; i < 32U; i += 2U) {
    packet[6U + i] = g_spi_start3_row_buf[i + 1U];
    packet[6U + i + 1U] = g_spi_start3_row_buf[i];
  }

  crc = CRC16_CCITT_FALSE(packet, 38U);
  packet[38] = (uint8_t)((crc >> 8) & 0xFFU);
  packet[39] = (uint8_t)(crc & 0xFFU);

  SPIUSB_ByteEnqueue(packet, 40U);
}

void SPIUSB_TryFlush(void)
{
  USBD_CDC_HandleTypeDef *hcdc;
  uint16_t send_len;
  uint8_t pkt_idx;

  if (g_spi_hex_tx_len == 0U) {
    return;
  }

  if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) {
    return;
  }

  if (hUsbDeviceFS.pClassData == NULL) {
    return;
  }

  hcdc = (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;

  while (g_spi_hex_tx_len > 0U) {
    if (hcdc->TxState != 0U) {
      return;
    }

    send_len = g_spi_hex_tx_len;
    if (send_len > SPI_USB_PKT_SIZE) {
      send_len = SPI_USB_PKT_SIZE;
    }

    pkt_idx = g_spi_usb_buf_idx;
    memcpy(g_spi_usb_buf[pkt_idx], g_spi_hex_tx_buf, send_len);

    if (CDC_Transmit_FS(g_spi_usb_buf[pkt_idx], send_len) != USBD_OK) {
      return;
    }

    g_spi_usb_buf_idx = (uint8_t)(1U - pkt_idx);
    if (send_len < g_spi_hex_tx_len) {
      memmove(g_spi_hex_tx_buf, &g_spi_hex_tx_buf[send_len],
              (size_t)(g_spi_hex_tx_len - send_len));
    }
    g_spi_hex_tx_len = (uint16_t)(g_spi_hex_tx_len - send_len);
  }
}
