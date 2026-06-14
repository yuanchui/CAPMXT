#include "mxt_spi_stream.h"
#include "mxt_state.h"
#include "mxt_config.h"
#include "mxt_usb_io.h"
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
static void MXT_SPI_QueueRxMark(uint8_t mark);
static void MXT_SPI_RestartAfterGap(void);
static void MXT_SPI_TryRestartAfterGap(void);
static void MXT_SPI_ApplyResyncIfNeeded(void);
static void MXT_SPI_CheckStreamStall(void);
static uint16_t MXT_SPI_DrainDmaRingBudget(uint16_t budget);
static void MXT_SPI_EnqueueRxByte(uint8_t b);
static uint16_t MXT_SPI_DmaWritePos(void);
static uint8_t SPIUSB_RawHandleFrameMark(uint8_t mark);
static uint8_t SPIUSB_RawCommitSlot(const uint8_t *data, uint16_t len);
static uint16_t SPIUSB_RawPktLen(uint16_t payload_len);
static uint8_t SPIUSB_RawEncodedByte(uint16_t pos, const uint8_t *raw, uint16_t payload_len);
static void SPIUSB_RawTxReset(void);
static void SPIUSB_ProcessByte(uint8_t b);
static void SPIUSB_LineEnqueue(const char *s);
static void SPIUSB_HexEnqueueByte(uint8_t b);

/* raw 514B 槽 + 64B CDC 分包：88 77 66 + LE u16 len + payload */
static uint8_t g_spi_raw_slots[SPI_RAW_LINE_SLOTS][SPI_RAW_OUT_BYTES];
static uint16_t g_spi_raw_slot_len[SPI_RAW_LINE_SLOTS];
static volatile uint8_t g_spi_raw_slot_r;
static volatile uint8_t g_spi_raw_slot_w;
static uint8_t g_spi_cdc_txbuf[2][SPI_RAW_TX_CHUNK];
static volatile uint8_t g_spi_cdc_txbuf_idx;
static volatile uint8_t g_spi_raw_tx_active;
static volatile uint16_t g_spi_raw_tx_pos;
static uint8_t g_spi_raw_staging[SPI_RAW_OUT_BYTES];
static volatile uint16_t g_spi_raw_staging_len;
static uint8_t   g_spi_usb_buf[2][SPI_USB_PKT_SIZE];
static volatile uint8_t g_spi_usb_buf_idx;

static const char g_hex_digits[] = "0123456789ABCDEF";

static uint16_t g_spi_dma_ssn_pos;
static volatile uint16_t g_spi_gap_ssn_snap;
static volatile uint16_t g_spi_gap_wr_snap;
static volatile uint8_t g_spi_gap_extract_pending;

static uint16_t MXT_SPI_DmaWritePos(void)
{
  if ((hspi1.hdmarx == NULL) || (g_spi_it_active == 0U)) {
    return g_spi_dma_last_pos;
  }

  return (uint16_t)(SPI_DMA_RING_SIZE - __HAL_DMA_GET_COUNTER(hspi1.hdmarx));
}

uint16_t MXT_SPI_GetDmaWritePos(void)
{
  return MXT_SPI_DmaWritePos();
}

static void MXT_SPI_EnqueueRxByte(uint8_t b)
{
  uint16_t next_head;
  uint8_t ssn_sel;

  ssn_sel = MXT_SSN_IsSelected();
  if (ssn_sel == 0U) {
    g_spi_prev_ssn_sel = 0U;
    return;
  }

  if (g_spi_prev_ssn_sel == 0U) {
    g_spi_raw_rx_count = 0U;
  }
  g_spi_prev_ssn_sel = 1U;

  if ((g_spi_stream_enabled != 0U) && (g_spi_stream_mode == 0U)
      && (g_spi_raw_rx_count >= SPI_RAW_OUT_BYTES)) {
    return;
  }

  next_head = (uint16_t)((g_spi_rx_q_head + 1U) % SPI_RX_QUEUE_DEPTH);
  if (next_head == g_spi_rx_q_tail) {
    g_spi_rx_overflow++;
    return;
  }

  g_spi_rx_queue[g_spi_rx_q_head] = b;
  g_spi_rx_mark[g_spi_rx_q_head] = 0U;
  g_spi_rx_q_head = next_head;

  if ((g_spi_stream_enabled != 0U) && (g_spi_stream_mode == 0U)) {
    g_spi_raw_rx_count++;
  }
}

static uint16_t SPIUSB_RawPktLen(uint16_t payload_len)
{
  return (uint16_t)(SPI_RAW_FRAME_HDR_LEN + payload_len);
}

static uint8_t SPIUSB_RawEncodedByte(uint16_t pos, const uint8_t *raw, uint16_t payload_len)
{
  if (pos == 0U) {
    return SPI_RAW_FRAME_HDR0;
  }
  if (pos == 1U) {
    return SPI_RAW_FRAME_HDR1;
  }
  if (pos == 2U) {
    return SPI_RAW_FRAME_HDR2;
  }
  if (pos == 3U) {
    return (uint8_t)(payload_len & 0xFFU);
  }
  if (pos == 4U) {
    return (uint8_t)((payload_len >> 8) & 0xFFU);
  }
  pos = (uint16_t)(pos - SPI_RAW_FRAME_HDR_LEN);
  if (pos < payload_len) {
    return raw[pos];
  }
  return 0U;
}

static void SPIUSB_RawTxReset(void)
{
  g_spi_raw_tx_active = 0U;
  g_spi_raw_tx_pos = 0U;
}

static uint8_t SPIUSB_RawCommitSlot(const uint8_t *data, uint16_t len)
{
  uint8_t next_w;

  if ((data == NULL) || (len == 0U) || (len > SPI_RAW_OUT_BYTES)) {
    return 0U;
  }

  next_w = (uint8_t)((g_spi_raw_slot_w + 1U) & (SPI_RAW_LINE_SLOTS - 1U));
  if (next_w == g_spi_raw_slot_r) {
    g_spi_raw_usb_drop++;
    return 0U;
  }

  memcpy(g_spi_raw_slots[g_spi_raw_slot_w], data, len);
  g_spi_raw_slot_len[g_spi_raw_slot_w] = len;
  g_spi_raw_slot_w = next_w;
  return 1U;
}

static void MXT_SPI_RawExtractFrameAtGap(uint16_t start_pos, uint16_t wr)
{
  uint16_t pos;
  uint16_t i;
  uint8_t next_w;

  next_w = (uint8_t)((g_spi_raw_slot_w + 1U) & (SPI_RAW_LINE_SLOTS - 1U));
  if (next_w == g_spi_raw_slot_r) {
    g_spi_raw_usb_drop++;
    g_spi_dma_last_pos = wr;
    g_spi_raw_rx_count = 0U;
    return;
  }

  pos = start_pos;
  for (i = 0U; i < SPI_RAW_OUT_BYTES; i++) {
    if (pos == wr) {
      break;
    }
    g_spi_raw_slots[g_spi_raw_slot_w][i] = g_spi_dma_ring[pos];
    pos = (uint16_t)((pos + 1U) % SPI_DMA_RING_SIZE);
  }

  if (i > 0U) {
    g_spi_raw_slot_len[g_spi_raw_slot_w] = i;
    g_spi_raw_slot_w = next_w;
  }

  g_spi_dma_last_pos = wr;
  g_spi_raw_rx_count = 0U;
}

static void MXT_SPI_ProcessGapExtract(void)
{
  uint16_t start_pos;
  uint16_t wr;

  if (g_spi_gap_extract_pending == 0U) {
    return;
  }
  if (MXT_SSN_IsSelected() != 0U) {
    return;
  }

  g_spi_gap_extract_pending = 0U;
  start_pos = g_spi_gap_ssn_snap;
  wr = MXT_SPI_DmaWritePos();
  if (wr == start_pos) {
    wr = g_spi_gap_wr_snap;
  }
  if (wr != start_pos) {
    MXT_SPI_RawExtractFrameAtGap(start_pos, wr);
  }
}

static void MXT_SPI_RawDiscardGapDrain(uint16_t budget)
{
  uint16_t wr;
  uint16_t rd;
  uint16_t rd0;

  if (g_spi_it_active == 0U) {
    return;
  }

  rd = g_spi_dma_last_pos;
  rd0 = rd;
  while (budget > 0U) {
    wr = MXT_SPI_DmaWritePos();
    if (rd == wr) {
      break;
    }
    rd = (uint16_t)((rd + 1U) % SPI_DMA_RING_SIZE);
    budget--;
  }
  if (rd != rd0) {
    g_spi_last_irq_ms = HAL_GetTick();
  }
  g_spi_dma_last_pos = rd;
}

static uint16_t MXT_SPI_DrainDmaRingBudget(uint16_t budget)
{
  uint16_t wr;
  uint16_t rd;
  uint8_t b;
  uint8_t got;
  uint8_t raw_fast;

  if (g_spi_it_active == 0U) {
    return budget;
  }

  raw_fast = ((g_spi_stream_enabled != 0U) && (g_spi_stream_mode == 0U)) ? 1U : 0U;

  if (raw_fast != 0U) {
    if (MXT_SSN_IsSelected() != 0U) {
      return budget;
    }
    MXT_SPI_RawDiscardGapDrain(budget);
    return 0U;
  }

  rd = g_spi_dma_last_pos;
  got = 0U;

  while (budget > 0U) {
    wr = MXT_SPI_DmaWritePos();
    if (rd == wr) {
      break;
    }

    b = g_spi_dma_ring[rd];
    rd = (uint16_t)((rd + 1U) % SPI_DMA_RING_SIZE);
    budget--;

    MXT_SPI_EnqueueRxByte(b);
    got = 1U;
  }

  g_spi_dma_last_pos = rd;

  if (got != 0U) {
    g_spi_last_irq_ms = HAL_GetTick();
    MXT_SSN_NotifySpiRx();
  }

  return budget;
}

void MXT_SPI_OnSsnActive(void)
{
  uint16_t pos;

  /* ISR ?????? DMA ????????ISR drain ???? */
  pos = MXT_SPI_GetDmaWritePos();
  g_spi_dma_last_pos = pos;
  g_spi_dma_ssn_pos = pos;
  g_spi_raw_rx_count = 0U;
  g_spi_prev_ssn_sel = 1U;
}

void MXT_SPI_OnSsnGap(void)
{
  if ((g_spi_stream_enabled != 0U) && (g_spi_stream_mode == 0U)) {
    /* ISR ????????3B ??/hex ??????GAP ?? */
    g_spi_gap_ssn_snap = g_spi_dma_ssn_pos;
    g_spi_gap_wr_snap = MXT_SPI_DmaWritePos();
    g_spi_gap_extract_pending = 1U;
    g_spi_gap_restart_pending = 1U;
  } else if (g_spi_stream_enabled != 0U) {
    MXT_SPI_QueueGapMarker();
  }
}

static void MXT_SPI_ApplyResyncIfNeeded(void)
{
  uint16_t pos;

  if ((g_spi_resync_pending == 0U) || (MXT_SSN_IsSelected() == 0U)) {
    return;
  }

  g_spi_resync_pending = 0U;
  MXT_SPI_StopIT();
  MXT_SPI_StartIT();
  __HAL_SPI_CLEAR_OVRFLAG(&hspi1);
  pos = MXT_SPI_GetDmaWritePos();
  g_spi_dma_last_pos = pos;
  g_spi_dma_ssn_pos = pos;
  g_spi_prev_ssn_sel = 1U;
  g_spi_last_irq_ms = HAL_GetTick();
}

static void MXT_SPI_CheckStreamStall(void)
{
  uint16_t pos;

  if ((g_spi_stream_enabled == 0U) || (g_spi_it_active == 0U)) {
    return;
  }
  if ((HAL_GetTick() - g_spi_last_irq_ms) <= SPI_STREAM_STALL_MS) {
    return;
  }

  __HAL_SPI_CLEAR_OVRFLAG(&hspi1);

  if (MXT_SSN_IsSelected() != 0U) {
    /* ??????????? SSN ?????? DMA */
    g_spi_resync_pending = 1U;
    pos = MXT_SPI_GetDmaWritePos();
    g_spi_dma_last_pos = pos;
    g_spi_dma_ssn_pos = pos;
  } else {
    MXT_SPI_StopIT();
    MXT_SPI_StartIT();
  }

  g_spi_last_irq_ms = HAL_GetTick();
}

void MXT_SPI_StartIT(void)
{
  if (g_spi_it_active != 0U) {
    return;
  }

  g_spi_last_irq_ms = HAL_GetTick();
  g_spi_dma_last_pos = 0U;
  __HAL_SPI_CLEAR_OVRFLAG(&hspi1);

  if (HAL_SPI_Receive_DMA(&hspi1, g_spi_dma_ring, SPI_DMA_RING_SIZE) == HAL_OK) {
    g_spi_it_active = 1U;
    g_spi_dma_ssn_pos = MXT_SPI_DmaWritePos();
  }
}

void MXT_SPI_StopIT(void)
{
  if (g_spi_it_active != 0U) {
    (void)HAL_SPI_DMAStop(&hspi1);
  }
  __HAL_SPI_CLEAR_OVRFLAG(&hspi1);
  g_spi_it_active = 0U;
  g_spi_last_irq_ms = 0U;
}


void MXT_SPI_QueueGapMarker(void)
{
  if (g_spi_stream_enabled == 0U) {
    return;
  }

  MXT_SPI_QueueRxMark(SPI_RX_MARK_GAP);
  g_spi_raw_rx_count = 0U;
  g_spi_gap_restart_pending = 1U;
}

void MXT_SPI_QueueStartMarker(void)
{
  if ((g_spi_stream_enabled == 0U) || (g_spi_stream_mode != 0U)) {
    return;
  }

  MXT_SPI_QueueRxMark(SPI_RX_MARK_START);
  g_spi_raw_rx_count = 0U;
}

static void MXT_SPI_QueueRxMark(uint8_t mark)
{
  uint16_t next_head;

  next_head = (uint16_t)((g_spi_rx_q_head + 1U) % SPI_RX_QUEUE_DEPTH);
  if (next_head == g_spi_rx_q_tail) {
    g_spi_rx_overflow++;
    return;
  }

  g_spi_rx_mark[g_spi_rx_q_head] = mark;
  g_spi_rx_queue[g_spi_rx_q_head] = 0U;
  g_spi_rx_q_head = next_head;
}

static void MXT_SPI_TryRestartAfterGap(void)
{
  if (g_spi_gap_restart_pending == 0U) {
    return;
  }

  if (MXT_SSN_IsSelected() != 0U) {
    return;
  }

  g_spi_gap_restart_pending = 0U;
  MXT_SPI_RestartAfterGap();
}

static void MXT_SPI_RestartAfterGap(void)
{
  uint16_t pos;

  (void)MXT_SPI_DrainDmaRingBudget(SPI_DRAIN_BUDGET_LOOP);
  __HAL_SPI_CLEAR_OVRFLAG(&hspi1);

  if (g_spi_resync_pending != 0U) {
    g_spi_resync_pending = 0U;
    MXT_SPI_StopIT();
    MXT_SPI_StartIT();
  } else if (g_spi_it_active == 0U) {
    MXT_SPI_StartIT();
  }

  pos = MXT_SPI_DmaWritePos();
  g_spi_dma_last_pos = pos;
  g_spi_dma_ssn_pos = pos;
  g_spi_prev_ssn_sel = 0U;
  g_spi_last_irq_ms = HAL_GetTick();
}

void MXT_ProcessSPICheck(void)
{
  uint16_t local_head;
  uint16_t local_tail;
  uint8_t b;
  uint8_t mark;

  MXT_SPI_ApplyResyncIfNeeded();

  if ((g_spi_stream_enabled != 0U) && (g_spi_stream_mode == 0U)) {
    if (MXT_SSN_IsSelected() != 0U) {
      MXT_SPI_CheckStreamStall();
      return;
    }

    MXT_SPI_ProcessGapExtract();
    MXT_SPI_TryRestartAfterGap();

    if (g_spi_it_active == 0U) {
      MXT_SPI_StartIT();
    }
    if (g_spi_it_active != 0U) {
      (void)MXT_SPI_DrainDmaRingBudget(SPI_DRAIN_BUDGET_LOOP);
      MXT_SPI_CheckStreamStall();
    }

    while (SPIUSB_RawFlushPending() != 0U) {
      /* 连续发送队列中的二进制帧 */
    }
    (void)MSG_BufferFlush();
    return;
  }

  MXT_SPI_TryRestartAfterGap();

  if (g_spi_it_active == 0U) {
    MXT_SPI_StartIT();
  }

  if (g_spi_it_active != 0U) {
    (void)MXT_SPI_DrainDmaRingBudget(SPI_DRAIN_BUDGET_LOOP);
  } else {
    return;
  }

  if (g_spi_stream_enabled != 0U) {
    MXT_SPI_CheckStreamStall();
  } else if (g_spi_check_requested == 0U) {
    if ((HAL_GetTick() - g_spi_last_irq_ms) > SPI_IDLE_STALL_MS) {
      MXT_SPI_StopIT();
      MXT_SPI_StartIT();
    }
  }

  local_head = g_spi_rx_q_head;
  local_tail = g_spi_rx_q_tail;

  {
    uint16_t budget = 64U;

  while ((local_tail != local_head) && (budget > 0U)) {
    budget--;
    mark = g_spi_rx_mark[local_tail];
    if (mark != 0U) {
      if ((g_spi_stream_enabled != 0U) && (g_spi_stream_mode == 0U)) {
        (void)SPIUSB_RawHandleFrameMark(mark);
      }
      local_tail = (uint16_t)((local_tail + 1U) % SPI_RX_QUEUE_DEPTH);
      g_spi_rx_q_tail = local_tail;
      local_head = g_spi_rx_q_head;
      continue;
    }

    b = g_spi_rx_queue[local_tail];

    if (g_spi_stream_enabled != 0U) {
      if (g_spi_stream_mode == 0U) {
        if (g_spi_raw_staging_len < SPI_RAW_OUT_BYTES) {
          g_spi_raw_staging[g_spi_raw_staging_len++] = b;
        }
        local_tail = (uint16_t)((local_tail + 1U) % SPI_RX_QUEUE_DEPTH);
        g_spi_rx_q_tail = local_tail;
        local_head = g_spi_rx_q_head;
        g_spi_frame_bytes++;
        continue;
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
  }

  if (g_spi_rx_overflow > 0U) {
    uint32_t now = HAL_GetTick();
    if ((now - g_spi_last_overflow_report_ms) >= 1000U) {
      g_spi_last_overflow_report_ms = now;
      /* ??????USB_SendString???? CDC ??????????*/
      if (g_spi_stream_enabled == 0U) {
        USB_SendString("[WARN: SPI RX queue overflow]\r\n");
      }
      g_spi_rx_overflow = 0U;
    }
  }

  SPIUSB_TryFlush();
}

static uint8_t SPIUSB_RawHandleFrameMark(uint8_t mark)
{
  if (mark == SPI_RX_MARK_START) {
    if (g_spi_raw_staging_len != 0U) {
      g_spi_raw_partial_drop++;
      g_spi_raw_staging_len = 0U;
    }
  } else if (mark == SPI_RX_MARK_GAP) {
    if (g_spi_raw_staging_len != 0U) {
      (void)SPIUSB_RawCommitSlot(g_spi_raw_staging, g_spi_raw_staging_len);
    }
    g_spi_raw_staging_len = 0U;
    MXT_SPI_TryRestartAfterGap();
  }

  (void)SPIUSB_RawFlushPending();
  return 1U;
}

uint8_t SPIUSB_RawHasPending(void)
{
  if (g_spi_raw_tx_active != 0U) {
    return 1U;
  }
  return (g_spi_raw_slot_r != g_spi_raw_slot_w) ? 1U : 0U;
}

uint8_t SPIUSB_RawFlushPending(void)
{
  USBD_CDC_HandleTypeDef *hcdc;
  uint8_t idx;
  uint16_t chunk;
  uint16_t enc_len;
  const uint8_t *raw;

  if (g_spi_stream_mode != 0U) {
    return 0U;
  }
  if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) {
    return 0U;
  }
  if (hUsbDeviceFS.pClassData == NULL) {
    return 0U;
  }

  hcdc = (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
  if (hcdc->TxState != 0U) {
    return 0U;
  }

  if (g_spi_raw_tx_active == 0U) {
    if (g_spi_raw_slot_r == g_spi_raw_slot_w) {
      return 0U;
    }
    g_spi_raw_tx_active = 1U;
    g_spi_raw_tx_pos = 0U;
  }

  raw = g_spi_raw_slots[g_spi_raw_slot_r];
  enc_len = SPIUSB_RawPktLen(g_spi_raw_slot_len[g_spi_raw_slot_r]);
  idx = g_spi_cdc_txbuf_idx;
  chunk = 0U;
  while ((chunk < SPI_RAW_TX_CHUNK) && (g_spi_raw_tx_pos < enc_len)) {
    g_spi_cdc_txbuf[idx][chunk++] = SPIUSB_RawEncodedByte(g_spi_raw_tx_pos, raw,
                                                          g_spi_raw_slot_len[g_spi_raw_slot_r]);
    g_spi_raw_tx_pos++;
  }

  if (chunk == 0U) {
    g_spi_raw_tx_active = 0U;
    g_spi_raw_slot_r = (uint8_t)((g_spi_raw_slot_r + 1U) & (SPI_RAW_LINE_SLOTS - 1U));
    return 0U;
  }

  if (CDC_Transmit_FS(g_spi_cdc_txbuf[idx], chunk) != USBD_OK) {
    g_spi_raw_tx_pos = (uint16_t)(g_spi_raw_tx_pos - chunk);
    return 0U;
  }

  g_spi_cdc_txbuf_idx = (uint8_t)(1U - g_spi_cdc_txbuf_idx);
  if (g_spi_raw_tx_pos >= enc_len) {
    g_spi_raw_tx_active = 0U;
    g_spi_raw_tx_pos = 0U;
    g_spi_raw_slot_r = (uint8_t)((g_spi_raw_slot_r + 1U) & (SPI_RAW_LINE_SLOTS - 1U));
  }
  return 1U;
}

void SPIUSB_RawStop(void)
{
  uint32_t t0;
  uint32_t spin;

  t0 = HAL_GetTick();
  while ((SPIUSB_RawHasPending() != 0U) && ((HAL_GetTick() - t0) < 200U)) {
    if (SPIUSB_RawFlushPending() == 0U) {
      MXT_FlushMessageBuffer();
      HAL_Delay(1);
    }
  }

  g_spi_raw_slot_r = 0U;
  g_spi_raw_slot_w = 0U;
  memset(g_spi_raw_slot_len, 0, sizeof(g_spi_raw_slot_len));
  g_spi_raw_staging_len = 0U;
  SPIUSB_RawTxReset();

  for (spin = 0U; spin < 64U; spin++) {
    MXT_FlushMessageBuffer();
    if (hUsbDeviceFS.pClassData != NULL) {
      USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
      if ((hcdc->TxState == 0U) && (SPIUSB_RawHasPending() == 0U)) {
        break;
      }
    }
    HAL_Delay(1);
  }
}

void MXT_SPI_OnDmaProgress(void)
{
  if (g_spi_it_active == 0U) {
    return;
  }

  g_spi_last_irq_ms = HAL_GetTick();

  if ((g_spi_stream_enabled != 0U) && (g_spi_stream_mode == 0U)) {
    return;
  }

  (void)MXT_SPI_DrainDmaRingBudget(SPI_DRAIN_BUDGET_ISR);
}

void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi)
{
  if (hspi->Instance == SPI1) {
    MXT_SPI_OnDmaProgress();
  }
}

void HAL_SPI_RxHalfCpltCallback(SPI_HandleTypeDef *hspi)
{
  if (hspi->Instance == SPI1) {
    MXT_SPI_OnDmaProgress();
  }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
  if (hspi->Instance != SPI1) return;

  g_spi_err_count++;
  __HAL_SPI_CLEAR_OVRFLAG(hspi);

  if ((g_spi_check_requested != 0U) || (g_spi_stream_enabled != 0U)) {
    if (g_spi_stream_enabled != 0U) {
      g_spi_resync_pending = 1U;
    }
    if (g_spi_it_active == 0U) {
      MXT_SPI_StartIT();
    }
  } else {
    g_spi_it_active = 0U;
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
  g_spi_dma_last_pos = 0U;
  g_spi_dma_ssn_pos = 0U;
  g_spi_raw_rx_count = 0U;
  g_spi_raw_staging_len = 0U;
  SPIUSB_RawTxReset();
  g_spi_gap_restart_pending = 0U;
  g_spi_resync_pending = 0U;
  g_spi_gap_extract_pending = 0U;
  g_spi_gap_ssn_snap = 0U;
  g_spi_gap_wr_snap = 0U;
  g_spi_prev_ssn_sel = MXT_SSN_IsSelected();
  g_spi_usb_buf_idx = 0U;
  g_spi_raw_slot_r = 0U;
  g_spi_raw_slot_w = 0U;
  memset(g_spi_raw_slot_len, 0, sizeof(g_spi_raw_slot_len));
  g_spi_cdc_txbuf_idx = 0U;
  g_spi_raw_usb_drop = 0U;
  g_spi_raw_partial_drop = 0U;

  /* START1/START3?????????? 640B ????????*/
  if (mode != 0U) {
    SPIUSB_Start1_HandlePageMarker();
  }
}

void SPIUSB_EndRawFrame(void)
{
  g_spi_raw_staging_len = 0U;
}

void MXT_USB_ServiceTx(void)
{
  if ((g_spi_stream_enabled != 0U) && (g_spi_stream_mode == 0U)) {
    if (MXT_SSN_IsSelected() != 0U) {
      return;
    }
    while (SPIUSB_RawFlushPending() != 0U) {
      /* CDC 空闲时尽量排空 raw 队列 */
    }
    (void)MSG_BufferFlush();
    return;
  } else if (g_spi_tx_len > 0U) {
    SPIUSB_TryFlush();
    return;
  }
  (void)MSG_BufferFlush();
}

void SPIUSB_OnTxComplete(void)
{
  MXT_USB_ServiceTx();
  if ((g_spi_stream_enabled != 0U) && (g_spi_stream_mode == 0U)) {
    MXT_SPI_TryRestartAfterGap();
  }
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
