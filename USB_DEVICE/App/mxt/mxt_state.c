#include "mxt_state.h"

uint8_t g_mxt_i2c_addr = MXT_I2C_ADDR_APP_HIGH;
uint8_t g_bridge_mode = BRIDGE_MODE_STRING;
uint8_t g_num_objects = 0;
uint8_t g_output_enabled = 0;
uint8_t g_t37_data[130];
uint8_t g_touch_inited = 0;

volatile uint8_t g_cfgwrite_active = 0;
uint16_t g_cfgwrite_total_objects = 0;
uint16_t g_cfgwrite_total_chunks = 0;
uint16_t g_cfgwrite_next_seq = 1;
CfgObjectMeta_t g_cfgwrite_objects[CFG_MAX_OBJECTS];

volatile uint8_t g_cfgread_waiting_ack = 0;
volatile uint16_t g_cfgread_current_seq = 0;
volatile uint8_t g_cfgread_last_ack_status = STATUS_OK;

uint8_t g_cfg_rx_buf[CFG_RX_BUF_SIZE];
uint16_t g_cfg_rx_len = 0;

volatile uint8_t g_encwrite_active = 0;
uint16_t g_enc_total_frames = 0;
uint16_t g_enc_next_seq = 1;
uint8_t g_enc_rx_buf[ENC_RX_BUF_SIZE];
uint16_t g_enc_rx_len = 0;

volatile uint8_t g_backup_busy = 0;
volatile uint32_t g_backup_busy_until_ms = 0;
volatile uint8_t g_unfreeze_pending = 0;
uint8_t g_debugctrl_applied = 0;

volatile uint8_t g_spi_check_requested = 0U;
volatile uint8_t g_spi_stream_enabled = 0;
volatile uint8_t g_spi_stream_mode = 0;
volatile uint8_t g_spi_in_frame = 0;
volatile uint16_t g_spi_frame_bytes = 0;
volatile uint8_t g_spi_it_active = 0;
volatile uint16_t g_spi_rx_q_head = 0U;
volatile uint16_t g_spi_rx_q_tail = 0U;
volatile uint16_t g_spi_rx_overflow = 0;
volatile uint32_t g_spi_last_irq_ms = 0;
volatile uint16_t g_spi_err_count = 0;
uint8_t g_spi_dma_ring[SPI_DMA_RING_SIZE];
volatile uint16_t g_spi_dma_last_pos = 0U;
uint8_t g_spi_rx_queue[SPI_RX_QUEUE_DEPTH];
uint8_t g_spi_rx_mark[SPI_RX_QUEUE_DEPTH];
volatile uint32_t g_spi_last_overflow_report_ms = 0;
MxtUsbStreamBuf_u g_usb_stream_buf;
volatile uint16_t g_spi_tx_len = 0U;
volatile uint8_t g_spi_start1_collecting = 0U;
volatile uint16_t g_spi_start1_payload_bytes = 0U;
volatile uint8_t g_spi_start1_row_bytes = 0U;
volatile uint8_t g_spi_start1_src_row_bytes = 0U;
volatile uint8_t g_spi_start3_frame_id = 0U;
volatile uint8_t g_spi_start3_row_id = 0U;
volatile uint8_t g_spi_start3_row_buf[32];
volatile uint8_t g_spi_start3_row_len = 0U;
volatile uint8_t g_spi_raw_rx_count = 0U;
volatile uint8_t g_spi_gap_restart_pending = 0U;
volatile uint8_t g_spi_resync_pending = 0U;
volatile uint8_t g_spi_prev_ssn_sel = 0U;
volatile uint32_t g_spi_raw_usb_drop = 0U;
volatile uint32_t g_spi_raw_partial_drop = 0U;

uint16_t g_t6_addr = 0;
uint16_t g_t44_addr = 0;
uint16_t g_t5_addr = 0;
uint16_t g_t37_addr = 0;
uint16_t g_t100_addr = 0;
uint8_t g_t37_size = 0;
uint8_t g_t100_size = 0;
uint8_t g_page_size = 0;
uint8_t g_pages_per_pass = 0;
uint8_t g_t6_report_id = 1;
uint8_t g_t100_report_id = 2;

DiagMode_t g_diag_mode = DIAG_MODE_NONE;
uint16_t g_diag_buffer_mem[32 * 20];
uint16_t *g_diag_buffer = g_diag_buffer_mem;

volatile TouchInfo_t g_last_touch;
volatile uint8_t g_last_touch_valid = 0;
volatile TouchInfo_t g_touch_queue[TOUCH_QUEUE_SIZE];
volatile uint8_t g_touch_q_head = 0;
volatile uint8_t g_touch_q_tail = 0;

uint32_t g_last_msg_time = 0;
uint8_t g_msg_output_enabled = 1;
volatile uint8_t g_chg_pending = 0;
uint8_t g_chg_process_enabled = 0;

volatile uint16_t g_msg_buffer_head = 0;
volatile uint16_t g_msg_buffer_tail = 0;
volatile uint8_t g_msg_buffer_overflow = 0;
uint32_t g_diag_interval_ms = 1000;
uint8_t g_stream_rot = 0;
uint8_t g_stream_flip = 0;
uint8_t g_stream_map16_hex = 0;
uint8_t g_stream_map16_char = 0;
uint8_t g_stream_frame_id = 0;
uint8_t g_stream_chgno = 0;
uint8_t g_stream_touch_flip = 0;
uint8_t g_stream_pre_cal = 0;

char g_cmd_buffer[CMD_BUFFER_SIZE];
volatile uint8_t g_cmd_pending = 0;
MenuState_t g_menu_state = MENU_IDLE;
uint32_t g_last_diag_time = 0;
uint8_t g_t37_reading = 0;

uint8_t g_msg_tx_chunk[MSG_FLUSH_CHUNK];

uint8_t g_work_buf[MXT_WORK_BUF_SIZE];
