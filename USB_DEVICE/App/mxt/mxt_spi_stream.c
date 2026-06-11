#include "mxt_spi_stream.h"
#include "mxt_state.h"
#include "mxt_config.h"
#include "mxt_usb_io.h"
#include "mxt_touch.h"
#include "usbd_cdc_if.h"
#include "main.h"
#include "gpio.h"
#include "spi.h"
#include <string.h>

static void SPIUSB_TxEnqueue(uint8_t b);
static void SPIUSB_TxEnqueueBytes(const uint8_t *data, uint16_t len);
static void SPIUSB_Start1_HandlePageMarker(void);
static void SPIUSB_Start1_ProcessPayloadByte(uint8_t b);
static void SPIUSB_Start3_ProcessCroppedByte(uint8_t b);
static void SPIUSB_Start3_EmitRowPacket(void);
static void SPIUSB_RawEndFrame(void);
static void SPIUSB_RawBeginFrame(uint8_t b);
static void SPIUSB_ProcessByte(uint8_t b);
static void SPIUSB_RawFrameReset(void);
static void SPIUSB_ProcessRawByte(uint8_t b);
static void SPIUSB_LineEnqueue(const char *s);
static void SPIUSB_HexEnqueueByte(uint8_t b);

static uint8_t   g_spi_raw_first_done;
static uint8_t   g_spi_prev_ssn_sel;
static uint8_t   g_spi_usb_buf[2][SPI_USB_PKT_SIZE];
static volatile uint8_t g_spi_usb_buf_idx;

static const char g_hex_digits[] = "0123456789ABCDEF";

static uint16_t MXT_SPI_RxChunkLen(void)
{
  if ((g_spi_stream_enabled != 0U) && (g_spi_stream_mode == 0U)) {
    return 1U;
  }

  return (uint16_t)SPI_IT_CHUNK_LEN;
}

void MXT_SPI_OnSsnActive(void)
{
  /* 仅复位首字节标志；禁止在此 Abort SPI（会在 EXTI/TIM 中断里打断接收导致错位/堵塞） */
  g_spi_raw_first_done = 0U;
}

static void MXT_SPI_RestartReceive(void)
{
  uint8_t next_idx = (uint8_t)(1U - g_spi_it_buf_idx);
  uint16_t chunk_len = MXT_SPI_RxChunkLen();

  if (HAL_SPI_Receive_IT(&hspi1, g_spi_it_rx_buf[next_idx], chunk_len) == HAL_OK) {
    g_spi_it_buf_idx = next_idx;
    g_spi_it_active = 1U;
  } else {
    g_spi_it_active = 0U;
  }
}

void MXT_SPI_StartIT(void)
{
  if (g_spi_it_active) return;

  g_spi_last_irq_ms = HAL_GetTick();
  g_spi_it_buf_idx = 0U;
  __HAL_SPI_CLEAR_OVRFLAG(&hspi1);

  if (HAL_SPI_Receive_IT(&hspi1, g_spi_it_rx_buf[0], MXT_SPI_RxChunkLen()) == HAL_OK) {
    g_spi_it_active = 1U;
  } else {
    (void)HAL_SPI_Abort(&hspi1);
    __HAL_SPI_CLEAR_OVRFLAG(&hspi1);
    if (HAL_SPI_Receive_IT(&hspi1, g_spi_it_rx_buf[0], MXT_SPI_RxChunkLen()) == HAL_OK) {
      g_spi_it_active = 1U;
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

void MXT_SPI_QueueGapMarker(void)
{
  uint16_t next_head;

  if (g_spi_stream_enabled == 0U) {
    return;
  }

  next_head = (uint16_t)((g_spi_rx_q_head + 1U) % SPI_RX_QUEUE_DEPTH);
  if (next_head == g_spi_rx_q_tail) {
    g_spi_rx_overflow++;
    return;
  }

  g_spi_rx_mark[g_spi_rx_q_head] = SPI_RX_MARK_GAP;
  g_spi_rx_queue[g_spi_rx_q_head] = 0U;
  g_spi_rx_q_head = next_head;
  g_spi_raw_first_done = 0U;
}

void MXT_ProcessSPICheck(void)
{
  uint16_t local_head;
  uint16_t local_tail;
  uint8_t b;

  /* 软件持续使能：主循环中始终保持 SPI 中断接收 */
  if (!g_spi_it_active) {
    MXT_SPI_StartIT();
    if (!g_spi_it_active) return;
  }

  if ((g_spi_stream_enabled == 0U) && (g_spi_check_requested == 0U)) {
    if ((HAL_GetTick() - g_spi_last_irq_ms) > SPI_IDLE_STALL_MS) {
      MXT_SPI_StopIT();
      MXT_SPI_StartIT();
    }
  }

  local_head = g_spi_rx_q_head;
  local_tail = g_spi_rx_q_tail;

  while (local_tail != local_head) {
    if (g_spi_rx_mark[local_tail] != 0U) {
      if ((g_spi_stream_enabled != 0U) && (g_spi_stream_mode == 0U)) {
        SPIUSB_RawEndFrame();
        SPIUSB_RawFrameReset();
      }
      local_tail = (uint16_t)((local_tail + 1U) % SPI_RX_QUEUE_DEPTH);
      g_spi_rx_q_tail = local_tail;
      local_head = g_spi_rx_q_head;
      continue;
    }

    b = g_spi_rx_queue[local_tail];

    if (g_spi_stream_enabled != 0U) {
      if (g_spi_stream_mode == 0U) {
        SPIUSB_ProcessRawByte(b);
      } else {
        SPIUSB_ProcessByte(b);
      }
      g_spi_frame_bytes++;
    }

    local_tail = (uint16_t)((local_tail + 1U) % SPI_RX_QUEUE_DEPTH);
    g_spi_rx_q_tail = local_tail;
    local_head = g_spi_rx_q_head;

    if ((g_spi_frame_bytes & 0x3FU) == 0U) {
      SPIUSB_TryFlush();
    }
  }

  if (g_spi_rx_overflow > 0) {
    uint32_t now = HAL_GetTick();
    if ((now - g_spi_last_overflow_report_ms) >= 200U) {
      g_spi_rx_overflow = 0;
      g_spi_last_overflow_report_ms = now;
    }
  }

  SPIUSB_TryFlush();
}

void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
  uint16_t next_head;
  uint8_t done_idx;
  uint8_t ssn_sel;
  const uint8_t *rx_chunk;

  if (hspi->Instance != SPI1) return;

  uint16_t chunk_len;

  uint8_t prev_ssn_sel;

  done_idx = g_spi_it_buf_idx;
  rx_chunk = g_spi_it_rx_buf[done_idx];
  chunk_len = MXT_SPI_RxChunkLen();
  prev_ssn_sel = g_spi_prev_ssn_sel;
  ssn_sel = MXT_SSN_IsSelected();
  g_spi_last_irq_ms = HAL_GetTick();

  if ((ssn_sel != 0U) && (prev_ssn_sel == 0U)) {
    g_spi_raw_first_done = 0U;
  }
  g_spi_prev_ssn_sel = ssn_sel;

  /* 流模式必须在中断里立即重启，否则丢时钟导致字节错位 */
  if ((g_spi_check_requested != 0U) || (g_spi_stream_enabled != 0U)) {
    MXT_SPI_RestartReceive();
  } else {
    g_spi_it_active = 0U;
  }

  MXT_SSN_NotifySpiRx();

  if (ssn_sel != 0U) {
    if ((g_spi_stream_enabled != 0U) && (g_spi_stream_mode == 0U) && (g_spi_raw_first_done != 0U)) {
      return;
    }

    for (uint16_t i = 0U; i < chunk_len; i++) {
      next_head = (uint16_t)((g_spi_rx_q_head + 1U) % SPI_RX_QUEUE_DEPTH);
      if (next_head == g_spi_rx_q_tail) {
        g_spi_rx_overflow++;
        break;
      }

      g_spi_rx_queue[g_spi_rx_q_head] = rx_chunk[i];
      g_spi_rx_mark[g_spi_rx_q_head] = 0U;
      g_spi_rx_q_head = next_head;

      if ((g_spi_stream_enabled != 0U) && (g_spi_stream_mode == 0U)) {
        g_spi_raw_first_done = 1U;
        break;
      }
    }
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

void SPIUSB_ResetState(uint8_t mode)
{
  g_spi_stream_mode = mode;
  g_spi_in_frame = 0U;
  g_spi_frame_bytes = 0U;
  g_spi_tx_len = 0U;
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
  g_spi_it_buf_idx = 0U;
  g_spi_raw_first_done = 0U;
  g_spi_prev_ssn_sel = MXT_SSN_IsSelected();
  g_spi_usb_buf_idx = 0U;

  /* START1/START3：启动后立即采集，每 640B 自动开始下一帧 */
  if (mode != 0U) {
    SPIUSB_Start1_HandlePageMarker();
  }
}

void SPIUSB_EndRawFrame(void)
{
  SPIUSB_RawEndFrame();
}

static void SPIUSB_RawFrameReset(void)
{
  g_spi_raw_first_done = 0U;
}

static void SPIUSB_RawEndFrame(void)
{
}

static void SPIUSB_RawBeginFrame(uint8_t b)
{
  SPIUSB_HexEnqueueByte(b);
  SPIUSB_LineEnqueue("\r\n");
}

static void SPIUSB_ProcessRawByte(uint8_t b)
{
  SPIUSB_RawBeginFrame(b);
}

static void SPIUSB_ProcessByte(uint8_t b)
{
  SPIUSB_Start1_ProcessPayloadByte(b);
}

static void SPIUSB_TxEnqueue(uint8_t b)
{
  if (g_spi_tx_len >= (uint16_t)(sizeof(g_spi_tx_buf) - 1U)) {
    SPIUSB_TryFlush();
  }

  if (g_spi_tx_len < (uint16_t)sizeof(g_spi_tx_buf)) {
    g_spi_tx_buf[g_spi_tx_len++] = b;
  }
}

static void SPIUSB_TxEnqueueBytes(const uint8_t *data, uint16_t len)
{
  uint16_t i;

  if ((data == NULL) || (len == 0U)) {
    return;
  }

  for (i = 0U; i < len; i++) {
    SPIUSB_TxEnqueue(data[i]);
  }
}

static void SPIUSB_LineEnqueue(const char *s)
{
  while ((*s != '\0') && (g_spi_tx_len < (uint16_t)(sizeof(g_spi_tx_buf) - 1U))) {
    g_spi_tx_buf[g_spi_tx_len++] = (uint8_t)(*s++);
  }
}

static void SPIUSB_HexEnqueueByte(uint8_t b)
{
  uint16_t pos;
  uint8_t retries;

  for (retries = 0U; retries < 8U; retries++) {
    if (g_spi_tx_len < (uint16_t)(sizeof(g_spi_tx_buf) - 3U)) {
      break;
    }
    SPIUSB_TryFlush();
  }

  pos = g_spi_tx_len;
  if (pos >= (uint16_t)(sizeof(g_spi_tx_buf) - 3U)) {
    return;
  }

  g_spi_tx_buf[pos] = (uint8_t)g_hex_digits[(b >> 4) & 0x0FU];
  g_spi_tx_buf[pos + 1U] = (uint8_t)g_hex_digits[b & 0x0FU];
  g_spi_tx_buf[pos + 2U] = (uint8_t)' ';
  g_spi_tx_len = (uint16_t)(pos + 3U);
}

static void SPIUSB_ByteEnqueue(const uint8_t *data, uint16_t len)
{
  SPIUSB_TxEnqueueBytes(data, len);
}

static void SPIUSB_Start1_HandlePageMarker(void)
{
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

static void SPIUSB_Start1_ProcessPayloadByte(uint8_t b)
{
  if (g_spi_start1_collecting == 0U) {
    return;
  }

  if (g_spi_start1_payload_bytes >= 640U) {
    return;
  }

  g_spi_start1_payload_bytes++;
  g_spi_start1_src_row_bytes++;

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

  if (g_spi_start1_payload_bytes >= 640U) {
    if ((g_spi_stream_mode == 1U) && (g_spi_start1_row_bytes != 0U)) {
      SPIUSB_LineEnqueue("\r\n");
      g_spi_start1_row_bytes = 0U;
    }
    SPIUSB_Start1_HandlePageMarker();
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

void SPIUSB_OnTxComplete(void)
{
  if (g_spi_tx_len > 0U) {
    SPIUSB_TryFlush();
  }
}

void SPIUSB_TryFlush(void)
{
  USBD_CDC_HandleTypeDef *hcdc;
  uint16_t send_len;
  uint8_t pkt_idx;

  if (g_spi_tx_len == 0U) {
    return;
  }

  if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) {
    return;
  }

  if (hUsbDeviceFS.pClassData == NULL) {
    return;
  }

  hcdc = (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;

  while (g_spi_tx_len > 0U) {
    if (hcdc->TxState != 0U) {
      return;
    }

    send_len = g_spi_tx_len;
    if (send_len > SPI_USB_PKT_SIZE) {
      send_len = SPI_USB_PKT_SIZE;
    }

    pkt_idx = g_spi_usb_buf_idx;
    memcpy(g_spi_usb_buf[pkt_idx], g_spi_tx_buf, send_len);

    if (CDC_Transmit_FS(g_spi_usb_buf[pkt_idx], send_len) != USBD_OK) {
      return;
    }

    g_spi_usb_buf_idx = (uint8_t)(1U - pkt_idx);
    if (send_len < g_spi_tx_len) {
      memmove(&g_spi_tx_buf[0], &g_spi_tx_buf[send_len], (size_t)(g_spi_tx_len - send_len));
    }
    g_spi_tx_len = (uint16_t)(g_spi_tx_len - send_len);
  }
}
