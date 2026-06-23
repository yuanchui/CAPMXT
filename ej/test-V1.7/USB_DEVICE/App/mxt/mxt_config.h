#ifndef MXT_CONFIG_H
#define MXT_CONFIG_H

#include <stdint.h>

/* test-V1.7：mXT640U，SPI 硬件 NSS(PA4) + IT 逐字节 */
#define MXT_HAS_ENC                0
#define MXT_SPI_USE_IT_NSS         1

/* I2C addresses */
#define MXT_I2C_ADDR_APP_LOW    0x4A
#define MXT_I2C_ADDR_APP_HIGH   0x4B
#define MXT_I2C_ADDR_BL_LOW     0x24
#define MXT_I2C_ADDR_BL_HIGH    0x25
#define MXT_I2C_ADDR_BL_ALT     0x26
#define MXT_I2C_ADDR_BL_MXT640  0x27

/* mxt-app protocol */
#define REPORT_ID              0x01
#define IIC_DATA_1             0x51
#define CMD_READ_PINS          0x82
#define CMD_CONFIG             0x80
#define CMD_FIND_IIC_ADDRESS   0xE0
#define MXT_T6_DEBUGCTRL_OFFSET 4U
#define MXT_STARTUP_DEBUGCTRL   0x20U  /* SIGNAL only (Byte4) */

/* SPI IT + 硬件 NSS（F103 20KB SRAM：SPI 独立 TX 缓冲，队列留 USB 背压余量） */
#define SPI_IT_CHUNK_LEN          1U
#define SPI_FRAME_PAYLOAD_BYTES   640U
/* 640U 三次 SSN/帧约 640B，队列与 TX 须与 test-V1.7原 一致，否则 SPI 丢包 */
#define SPI_RX_QUEUE_DEPTH        2048U
#define SPI_HEX_TX_BUF_SIZE       4096U
#define SPI_USB_PKT_SIZE          64U
#define SPI_STREAM_STALL_MS       100U
#define SPI_GAP_IDLE_STALL_MS     500U
#define MSG_BUFFER_SIZE             1024U
#define MSG_FLUSH_CHUNK_SPI       128U    /* SPI 流期间加大 USB 刷新块 */

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
 * CFG 对象槽：须 ≥ 上位机 xcfg 解析出的 INSTANCE 段数。
 * demo.xcfg 头 NO_OBJECTS=48 为保存时信息块字段；实际可写段为 125（含多实例 T110/T141 等）。
 */
#define CFG_MAX_OBJECTS            128
#define CFG_MAX_DATA_PER_FRAME    256
#define CFG_RX_BUF_SIZE            (12U + (CFG_MAX_OBJECTS) * 4U + 16U)
#define CFG_ACK_TIMEOUT_MS         30
#define CFG_READBACK_DELAY_MS     200

#define UNFREEZE_COMMAND          0x11
#define FREEZE_COMMAND            0x22
#define BACKUPNV_COMMAND         0x55

#define STATUS_OK              0x00
#define STATUS_ADDR_NACK       0x01
#define STATUS_WRITE_OK        0x04
#define STATUS_STILL_IN_APP    0x83
#define STATUS_OBJ_DONE        0x10
#define STATUS_NO_DEVICE       0x81
#define I2C_TIMEOUT_MS         10
#define MXT_MEM_ADD(reg)       (uint16_t)(((reg) & 0xFF) << 8 | ((reg) >> 8))

#define BRIDGE_MODE_BINARY  0
#define BRIDGE_MODE_STRING  1

#define TOUCH_MAX_X 830
#define TOUCH_MAX_Y 940
#define TOUCH_QUEUE_SIZE 32
#define CMD_BUFFER_SIZE 64
#define MSG_FLUSH_CHUNK  64
#define MXT_WORK_BUF_SIZE 512U

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
