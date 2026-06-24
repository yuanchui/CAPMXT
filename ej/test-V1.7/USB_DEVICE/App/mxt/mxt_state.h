#ifndef MXT_STATE_H
#define MXT_STATE_H

#include "mxt_config.h"

extern uint8_t g_mxt_i2c_addr;
extern uint8_t g_bridge_mode;
extern uint8_t g_num_objects;
extern uint8_t g_output_enabled;
extern uint8_t g_t37_data[130];
extern uint8_t g_touch_inited;

extern volatile uint8_t g_cfgwrite_active;
extern uint16_t g_cfgwrite_total_objects;
extern uint16_t g_cfgwrite_total_chunks;
extern uint16_t g_cfgwrite_next_seq;
extern CfgObjectMeta_t g_cfgwrite_objects[CFG_MAX_OBJECTS];
extern volatile uint8_t g_cfgread_waiting_ack;
extern volatile uint16_t g_cfgread_current_seq;
extern volatile uint8_t g_cfgread_last_ack_status;
extern uint8_t g_cfg_rx_buf[CFG_RX_BUF_SIZE];
extern uint16_t g_cfg_rx_len;

extern volatile uint8_t g_backup_busy;
extern volatile uint32_t g_backup_busy_until_ms;
extern volatile uint8_t g_unfreeze_pending;
extern uint8_t g_debugctrl_applied;

extern volatile uint8_t g_spi_check_requested;
extern volatile uint8_t g_spi_stream_enabled;
extern volatile uint8_t g_spi_stream_mode;
extern uint8_t g_spi_in_frame;
extern uint16_t g_spi_frame_bytes;
extern volatile uint8_t g_spi_it_active;
extern volatile uint16_t g_spi_rx_q_head;
extern volatile uint16_t g_spi_rx_q_tail;
extern volatile uint16_t g_spi_rx_overflow;
extern volatile uint32_t g_spi_last_irq_ms;
extern volatile uint16_t g_spi_err_count;
extern uint8_t g_spi_it_rx_buf[SPI_IT_CHUNK_LEN];
extern uint8_t g_spi_rx_queue[SPI_RX_QUEUE_DEPTH][SPI_IT_CHUNK_LEN];
extern uint8_t g_spi_nss_queue[SPI_RX_QUEUE_DEPTH];
extern volatile uint32_t g_spi_last_overflow_report_ms;
extern uint8_t g_spi_nss_prev;
extern char g_spi_hex_tx_buf[SPI_HEX_TX_BUF_SIZE];
extern uint16_t g_spi_hex_tx_len;
extern volatile uint32_t g_spi_tx_drop;
extern uint8_t g_spi_start1_nss_page;
extern uint8_t g_spi_start1_collecting;
extern uint16_t g_spi_start1_payload_bytes;
extern uint8_t g_spi_start1_row_bytes;
extern uint8_t g_spi_start1_src_row_bytes;
extern uint8_t g_spi_start3_frame_id;
extern uint8_t g_spi_start3_row_id;
extern uint8_t g_spi_start3_row_buf[32];
extern uint8_t g_spi_start3_row_len;

extern uint16_t g_t6_addr;
extern uint16_t g_t44_addr;
extern uint16_t g_t5_addr;
extern uint16_t g_t37_addr;
extern uint16_t g_t100_addr;
extern uint8_t g_t37_size;
extern uint8_t g_t100_size;
extern uint8_t g_page_size;
extern uint8_t g_pages_per_pass;
extern uint8_t g_t6_report_id;
extern uint8_t g_t100_report_id;

extern DiagMode_t g_diag_mode;
extern uint16_t g_diag_buffer_mem[32 * 20];
extern uint16_t *g_diag_buffer;
extern volatile TouchInfo_t g_last_touch;
extern volatile uint8_t g_last_touch_valid;
extern volatile TouchInfo_t g_touch_queue[TOUCH_QUEUE_SIZE];
extern volatile uint8_t g_touch_q_head;
extern volatile uint8_t g_touch_q_tail;

extern uint32_t g_last_msg_time;
extern uint8_t g_msg_output_enabled;
extern volatile uint8_t g_chg_pending;
extern uint8_t g_chg_process_enabled;
extern char g_msg_buffer[MSG_BUFFER_SIZE];
extern volatile uint16_t g_msg_buffer_head;
extern volatile uint16_t g_msg_buffer_tail;
extern volatile uint8_t g_msg_buffer_overflow;
extern uint32_t g_diag_interval_ms;
extern uint8_t g_stream_rot;
extern uint8_t g_stream_flip;
extern uint8_t g_stream_map16_hex;
extern uint8_t g_stream_map16_char;
extern uint8_t g_stream_frame_id;
extern uint8_t g_stream_chgno;
extern uint8_t g_stream_touch_flip;
extern uint8_t g_stream_pre_cal;

extern char g_cmd_buffer[CMD_BUFFER_SIZE];
extern volatile uint8_t g_cmd_pending;
extern MenuState_t g_menu_state;
extern uint32_t g_last_diag_time;
extern uint8_t g_t37_reading;

extern uint8_t g_work_buf[MXT_WORK_BUF_SIZE];

#endif /* MXT_STATE_H */
