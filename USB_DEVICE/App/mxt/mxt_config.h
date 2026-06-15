#ifndef MXT_CONFIG_H
#define MXT_CONFIG_H

#include <stdint.h>

/* I2C addresses */
#define MXT_I2C_ADDR_APP_LOW    0x4A
#define MXT_I2C_ADDR_APP_HIGH   0x4B
#define MXT_I2C_ADDR_BL_LOW     0x24
#define MXT_I2C_ADDR_BL_HIGH    0x25
#define MXT_I2C_ADDR_BL_ALT     0x26
/* mXT640UD-CC ADDR_SEL=High 时 Bootloader 常为 0x27（见 enc.txt / QTAN0051） */
#define MXT_I2C_ADDR_BL_MXT640  0x27

/* mxt-app protocol */
#define REPORT_ID              0x01
#define IIC_DATA_1             0x51
#define CMD_READ_PINS          0x82
#define CMD_CONFIG             0x80
#define CMD_FIND_IIC_ADDRESS   0xE0
#define MXT_T6_DEBUGCTRL2_OFFSET 6U
#define MXT_STARTUP_DEBUGCTRL2   0x80U
#define SPI_DMA_RING_SIZE       640U
#define SPI_STREAM_STALL_MS     25U
#define SPI_IDLE_STALL_MS       100U
#define SPI_RX_QUEUE_DEPTH      128U
#define SPI_DRAIN_BUDGET_ISR    16U
#define SPI_DRAIN_BUDGET_LOOP   96U
#define SPI_RX_MARK_GAP         1U
#define SPI_RX_MARK_START       2U
#define SPI_FRAME_MAGIC0        0x87U
#define SPI_FRAME_MAGIC1        0x78U
#define SPI_USB_PKT_SIZE        64U
#define SPI_RAW_OUT_BYTES       514U
#define SPI_RAW_FRAME_HDR0      0x88U
#define SPI_RAW_FRAME_HDR1      0x77U
#define SPI_RAW_FRAME_HDR2      0x66U
#define SPI_RAW_FRAME_MAGIC_LEN 3U
#define SPI_RAW_FRAME_LEN_BYTES 2U
#define SPI_RAW_FRAME_HDR_LEN   (SPI_RAW_FRAME_MAGIC_LEN + SPI_RAW_FRAME_LEN_BYTES)
#define SPI_RAW_PKT_BYTES       (SPI_RAW_FRAME_HDR_LEN + SPI_RAW_OUT_BYTES)
/* 槽内存原始数据（≤514B）；USB 帧：88 77 66 + LE u16 长度 + payload */
#define SPI_RAW_LINE_SLOTS      2U
#define SPI_RAW_TX_CHUNK        SPI_USB_PKT_SIZE
#define SPI_MAIN_LOOP_BURST     1U

/* CFGWRITE/CFGREAD */
#define CFG_PROTOCOL_VERSION       0x01
#define CFGWRITE_START_CMD         0xD0
#define CFGWRITE_CHUNK_CMD         0xD1
#define CFGWRITE_END_CMD           0xD2
#define CFG_RESP_ACK_CMD           0xD3
#define CFG_RESP_NACK_CMD          0xD4
#define CFGREAD_DATA_CMD           0xE1
#define CFGREAD_END_CMD            0xE2
/*
 * CFG_MAX_OBJECTS — MCU 侧 g_cfgwrite_objects[] / START 帧对象表槽位上限。
 * 实际上传对象数由上位机在 D0 START 的 total_objects 字段声明，须 ≤ 本值。
 */
#define CFG_MAX_OBJECTS            128
#define CFG_MAX_DATA_PER_FRAME    256
#define CFG_RX_BUF_SIZE            (12U + (CFG_MAX_OBJECTS) * 4U + 16U)
#define CFG_ACK_TIMEOUT_MS         30
#define CFG_READBACK_DELAY_MS     200

#define UNFREEZE_COMMAND          0x11
#define FREEZE_COMMAND            0x22
#define BACKUPNV_COMMAND         0x55

/* ENCWRITE — Host 流式下发 .enc 切帧，MCU 边收边写 Bootloader I2C */
#define ENC_PROTOCOL_VERSION       0x01
#define ENC_START_CMD              0xB0
#define ENC_FRAME_CMD              0xB1
#define ENC_END_CMD                0xB2
#define ENC_RESP_ACK_CMD           0xB3
#define ENC_RESP_NACK_CMD          0xB4
#define ENC_MAX_FRAME_BYTES        276U
#define ENC_RX_BUF_SIZE            (ENC_MAX_FRAME_BYTES + 16U)

#define STATUS_OK              0x00
#define STATUS_ADDR_NACK       0x01
#define STATUS_WRITE_OK        0x04
#define STATUS_OBJ_DONE        0x10
#define STATUS_NO_DEVICE       0x81
#define I2C_TIMEOUT_MS         10
#define MXT_MEM_ADD(reg)       (uint16_t)(((reg) & 0xFF) << 8 | ((reg) >> 8))

#define BRIDGE_MODE_BINARY  0
#define BRIDGE_MODE_STRING  1

#define TOUCH_MAX_X 830
#define TOUCH_MAX_Y 940
#define TOUCH_QUEUE_SIZE 32
#define MSG_BUFFER_SIZE 1024
#define CMD_BUFFER_SIZE 64
#define MSG_FLUSH_CHUNK  64

/* 通用工作区（单缓冲复用，非 SPI/USB 主通道） */
#define MXT_WORK_BUF_SIZE 512U

/* SPI 文本/二进制发送缓冲；与 g_msg_buffer 复用同一块 RAM */
#define MXT_USB_STREAM_BUF_SIZE 1024U
#define SPI_TX_BUF_SIZE         MXT_USB_STREAM_BUF_SIZE

typedef struct {
  uint16_t addr;
  uint16_t size;
} CfgObjectMeta_t;

typedef enum {
  DIAG_MODE_NONE = 0,
  DIAG_MODE_MUTUAL_DELTA = 0x10,
  DIAG_MODE_MUTUAL_REF = 0x11,
  DIAG_MODE_SELF_DELTA = 0xF7,
  DIAG_MODE_SELF_REF = 0xF8,
  DIAG_MODE_SELF_SIGNAL = 0xF5,
  DIAG_MODE_SELF_DC = 0x38
} DiagMode_t;

typedef enum {
  TOUCH_ACTION_NONE   = 0,
  TOUCH_ACTION_DOWN   = 1,
  TOUCH_ACTION_MOVE   = 2,
  TOUCH_ACTION_UP     = 3,
  TOUCH_ACTION_DOWNUP = 4
} TouchAction_t;

typedef struct {
  uint8_t      id;
  uint16_t     x;
  uint16_t     y;
  TouchAction_t action;
} TouchInfo_t;

typedef enum {
  MENU_IDLE = 0,
  MENU_MAIN,
  MENU_DUMP_TYPE,
  MENU_MUTUAL_CAP,
  MENU_SELF_CAP,
  MENU_RUNNING
} MenuState_t;

#endif /* MXT_CONFIG_H */
