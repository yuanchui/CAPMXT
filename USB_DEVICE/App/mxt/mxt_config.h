#ifndef MXT_CONFIG_H
#define MXT_CONFIG_H

#include <stdint.h>

/* I2C addresses */
#define MXT_I2C_ADDR_APP_LOW    0x4A
#define MXT_I2C_ADDR_APP_HIGH   0x4B
#define MXT_I2C_ADDR_BL_LOW     0x24
#define MXT_I2C_ADDR_BL_HIGH    0x25
#define MXT_I2C_ADDR_BL_ALT     0x26

/* mxt-app protocol */
#define REPORT_ID              0x01
#define IIC_DATA_1             0x51
#define CMD_READ_PINS          0x82
#define CMD_CONFIG             0x80
#define CMD_FIND_IIC_ADDRESS   0xE0
#define MXT_T6_DEBUGCTRL2_OFFSET 6U
#define MXT_STARTUP_DEBUGCTRL2   0x80U
#define SPI_DMA_RING_SIZE       512U
#define SPI_STREAM_STALL_MS     25U
#define SPI_IDLE_STALL_MS       100U
#define SPI_RX_QUEUE_DEPTH      256U
#define SPI_DRAIN_BUDGET_ISR    16U
#define SPI_DRAIN_BUDGET_LOOP   96U
#define SPI_RX_MARK_GAP         1U
#define SPI_RX_MARK_START       2U
#define SPI_FRAME_MAGIC0        0x87U
#define SPI_FRAME_MAGIC1        0x78U
#define SPI_USB_PKT_SIZE        64U
#define SPI_RAW_OUT_BYTES       33U
/* 原始 hex 行：\r + 33*(HH + space) + \r\n ≈ 102B；8 槽待发送环 + 2 CDC ping-pong */
#define SPI_RAW_CDC_LINE_SIZE   (1U + (SPI_RAW_OUT_BYTES * 3U) + 2U)
#define SPI_RAW_LINE_SLOTS      4U
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
#define CFG_MAX_OBJECTS            96
#define CFG_MAX_DATA_PER_FRAME    256
#define CFG_ACK_TIMEOUT_MS         30
#define CFG_READBACK_DELAY_MS     200

#define UNFREEZE_COMMAND          0x11
#define FREEZE_COMMAND            0x22
#define BACKUPNV_COMMAND         0x55

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
