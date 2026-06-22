/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usbd_cdc_if.c
  * @version        : v2.0_Cube
  * @brief          : Usb device for Virtual Com Port.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "usbd_cdc_if.h"

/* USER CODE BEGIN INCLUDE */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "i2c.h"
#include "spi.h"
#include "gpio.h"
#include "stm32f1xx_hal_i2c.h"  /* I2C_MEMADD_SIZE_16BIT for maXTouch 16-bit register */
/* USER CODE END INCLUDE */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/

/* USER CODE END PV */

/** @addtogroup STM32_USB_OTG_DEVICE_LIBRARY
  * @brief Usb device library.
  * @{
  */

/** @addtogroup USBD_CDC_IF
  * @{
  */

/** @defgroup USBD_CDC_IF_Private_TypesDefinitions USBD_CDC_IF_Private_TypesDefinitions
  * @brief Private types.
  * @{
  */

/* USER CODE BEGIN PRIVATE_TYPES */

/* USER CODE END PRIVATE_TYPES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Private_Defines USBD_CDC_IF_Private_Defines
  * @brief Private defines.
  * @{
  */

/* USER CODE BEGIN PRIVATE_DEFINES */
/* I2C 地址定义 */
#define MXT_I2C_ADDR_APP_LOW    0x4A   /* Application 模式低地址 */
#define MXT_I2C_ADDR_APP_HIGH   0x4B   /* Application 模式高地址 */
#define MXT_I2C_ADDR_BL_LOW     0x24   /* Bootloader 模式低地址 */
#define MXT_I2C_ADDR_BL_HIGH    0x25   /* Bootloader 模式高地址 */
#define MXT_I2C_ADDR_BL_ALT     0x26   /* Bootloader 备用地址 */

/* mxt-app 协议命令定义 */
#define REPORT_ID              0x01
#define IIC_DATA_1             0x51
#define CMD_READ_PINS          0x82
#define CMD_CONFIG             0x80
#define CMD_FIND_IIC_ADDRESS   0xE0
#define MXT_T6_DEBUGCTRL_OFFSET 4U
#define MXT_STARTUP_DEBUGCTRL   0x20U  /* SIGNAL only */
#define SPI_IT_CHUNK_LEN        1U
#define SPI_RX_QUEUE_DEPTH      2048U
#define SPI_HEX_TX_BUF_SIZE     4096

/* =========================================================================
 * CFGWRITE/CFGREAD 协议（自定义二进制配置写入 + CRC16 + ACK）
 *  - host -> MCU: CFGWRITE_START(0xD0), CFGWRITE_CHUNK(0xD1), CFGWRITE_END(0xD2)
 *  - MCU -> host: ACK/NACK(0xD3/0xD4)，以及读回数据 CFGREAD_DATA(0xE1)、读回结束 CFGREAD_END(0xE2)
 *  - 多字节字段均使用 LE（低字节在前）
 *  - CRC16 使用现有 Map16_CalcCRC16（Modbus/IBM，poly=0xA001, init=0xFFFF），并按 LE 发送
 *  - Mode3 / SPISTART3 二进制帧（AA 10 33…）使用 CRC-16/CCITT-FALSE（poly=0x1021），CRC 按大端附在帧尾
 * ========================================================================= */
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

/* T6 backup/freeze/unfreeze 命令值（参考 mxt-app/libmaxtouch） */
#define UNFREEZE_COMMAND          0x11
#define FREEZE_COMMAND            0x22
#define BACKUPNV_COMMAND         0x55

/* 状态码定义 */
#define STATUS_OK              0x00
#define STATUS_ADDR_NACK       0x01
#define STATUS_WRITE_OK        0x04
#define STATUS_OBJ_DONE        0x10
#define STATUS_NO_DEVICE       0x81

/* I2C 超时时间 (ms) */
#define I2C_TIMEOUT_MS         10

/* HAL I2C: maXTouch 寄存器地址为低字节先发，HAL 16bit 为高字节先发，故交换 */
#define MXT_MEM_ADD(reg)       (uint16_t)(((reg) & 0xFF) << 8 | ((reg) >> 8))
/* USER CODE END PRIVATE_DEFINES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Private_Macros USBD_CDC_IF_Private_Macros
  * @brief Private macros.
  * @{
  */

/* USER CODE BEGIN PRIVATE_MACRO */

/* USER CODE END PRIVATE_MACRO */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Private_Variables USBD_CDC_IF_Private_Variables
  * @brief Private variables.
  * @{
  */
/* Create buffer for reception and transmission           */
/* It's up to user to redefine and/or remove those define */
/** Received data over USB are stored in this buffer      */
uint8_t UserRxBufferFS[APP_RX_DATA_SIZE];

/** Data to send over USB CDC are stored in this buffer   */
uint8_t UserTxBufferFS[APP_TX_DATA_SIZE];

/* USER CODE BEGIN PRIVATE_VARIABLES */
static uint8_t g_mxt_i2c_addr = MXT_I2C_ADDR_APP_HIGH;  /* 当前 I2C 地址，默认 0x4B */

/* 标志位和相关变量：默认字符串模式，发 HELP/INFO 等文本命令；发 MODE0 后为 I2C 桥模式供 mxt-app */
#define BRIDGE_MODE_BINARY  0  /* I2C-USB 桥模式：二进制协议 0x82/0xE0/0x51... */
#define BRIDGE_MODE_STRING  1  /* 字符串模式：help、INFO、FRAME 等 */
static uint8_t g_bridge_mode = BRIDGE_MODE_STRING;  /* 默认字符串模式 */
uint8_t g_matrix_x_size = 0;  /* X轴大小 */
uint8_t g_matrix_y_size = 0;  /* Y轴大小 */
static uint8_t g_num_objects = 0;    /* 对象表数量（InfoBlock[6]） */

static uint8_t g_output_enabled = 0;  /* 输出使能标志，用于控制启动/停止输出 */
static uint8_t g_t37_data[130];      /* 存储T37诊断数据 */
static uint8_t g_touch_inited = 0;    /* 触摸屏配置是否已完成 */

/* ======================= 配置写入协议状态 ======================= */
typedef struct {
  uint16_t addr; /* OBJECT_ADDRESS */
  uint16_t size; /* OBJECT_SIZE */
} CfgObjectMeta_t;

static volatile uint8_t g_cfgwrite_active = 0;
static uint16_t g_cfgwrite_total_objects = 0;
static uint16_t g_cfgwrite_total_chunks = 0;
static uint16_t g_cfgwrite_next_seq = 1; /* 从 1 开始 */
static CfgObjectMeta_t g_cfgwrite_objects[CFG_MAX_OBJECTS];

/* 读回阶段：MCU 发送 CFGREAD_DATA，等待 host 发送 ACK/NACK */
static volatile uint8_t g_cfgread_waiting_ack = 0;
static volatile uint16_t g_cfgread_current_seq = 0;
static volatile uint8_t g_cfgread_last_ack_status = STATUS_OK;

/* host->MCU CFG 接收重组缓冲（CDC 64B 分包） */
static uint8_t g_cfg_rx_buf[CFG_MAX_DATA_PER_FRAME + 128];
static uint16_t g_cfg_rx_len = 0;

/* 控制命令（BACKUPNV/UNFREEZE）异步调度
 * 避免在 USB 接收回调里做 HAL_Delay(2000) 阻塞，导致后续命令 ACK 丢失/失效。 */
static volatile uint8_t g_backup_busy = 0;
static volatile uint32_t g_backup_busy_until_ms = 0;
static volatile uint8_t g_unfreeze_pending = 0;
static uint8_t g_debugctrl_applied = 0;
static volatile uint8_t g_spi_check_requested = 1U;
static volatile uint8_t g_spi_stream_enabled = 0;
static volatile uint8_t g_spi_stream_mode = 0; /* 0: SPISTART(raw hex), 1: SPISTART1(16x16 text), 2: SPISTART3(packet) */
static uint8_t g_spi_in_frame = 0;
static uint16_t g_spi_frame_bytes = 0;
static volatile uint8_t g_spi_it_active = 0;
static volatile uint16_t g_spi_rx_q_head = 0U;
static volatile uint16_t g_spi_rx_q_tail = 0U;
static volatile uint16_t g_spi_rx_overflow = 0;
static volatile uint32_t g_spi_last_irq_ms = 0;
static volatile uint16_t g_spi_err_count = 0;
static uint8_t g_spi_it_rx_buf[SPI_IT_CHUNK_LEN];
static uint8_t g_spi_rx_queue[SPI_RX_QUEUE_DEPTH][SPI_IT_CHUNK_LEN];
static uint8_t g_spi_nss_queue[SPI_RX_QUEUE_DEPTH];
static uint32_t g_spi_last_overflow_report_ms = 0;
static uint8_t g_spi_nss_prev = 1U;
static char g_spi_hex_tx_buf[SPI_HEX_TX_BUF_SIZE];
static uint16_t g_spi_hex_tx_len = 0U;
static uint8_t g_spi_start1_nss_page = 0U;
static uint8_t g_spi_start1_collecting = 0U;
static uint16_t g_spi_start1_payload_bytes = 0U;
static uint8_t g_spi_start1_row_bytes = 0U;
static uint8_t g_spi_start1_src_row_bytes = 0U;
static uint8_t g_spi_start3_frame_id = 0U;
static uint8_t g_spi_start3_row_id = 0U;
static uint8_t g_spi_start3_row_buf[32];
static uint8_t g_spi_start3_row_len = 0U;

/* 对象地址信息 */
static uint16_t g_t6_addr = 0;       /* T6 Command Processor地址 */
static uint16_t g_t44_addr = 0;      /* T44 Message Count地址 */
static uint16_t g_t5_addr = 0;       /* T5 Message Processor地址 */
static uint16_t g_t37_addr = 0;      /* T37 Debug Diagnostic地址 */
static uint16_t g_t100_addr = 0;     /* T100 Multiple Touch地址 */
static uint8_t g_t37_size = 0;       /* T37对象大小 */
static uint8_t g_t100_size = 0;      /* T100对象大小 */
static uint8_t g_page_size = 0;      /* T37页大小 (去掉mode+page) */
static uint8_t g_pages_per_pass = 0; /* 每次完整扫描需要的页数 */

/* Report ID信息 (从TObject Table计算) */
static uint8_t g_t6_report_id = 1;   /* T6的Report ID (默认1) */
static uint8_t g_t100_report_id = 2; /* T100起始Report ID (默认2) */

/* 诊断数据模式 */
typedef enum {
  DIAG_MODE_NONE = 0,
  DIAG_MODE_MUTUAL_DELTA = 0x10,
  DIAG_MODE_MUTUAL_REF = 0x11,
  DIAG_MODE_SELF_DELTA = 0xF7,
  DIAG_MODE_SELF_REF = 0xF8,
  DIAG_MODE_SELF_SIGNAL = 0xF5,
  DIAG_MODE_SELF_DC = 0x38
} DiagMode_t;

static DiagMode_t g_diag_mode = DIAG_MODE_NONE;   /* 当前诊断模式 */
/* 诊断数据缓冲区：固定分配，覆盖最大 32x20 矩阵（640 节点） */
static uint16_t g_diag_buffer_mem[32 * 20];
static uint16_t *g_diag_buffer = g_diag_buffer_mem;  /* 完整帧数据缓冲区 */

/* CHGNO 模式下用于在 Mode3 包中附加的触点信息 */
#define TOUCH_MAX_X 830
#define TOUCH_MAX_Y 940

typedef enum {
  TOUCH_ACTION_NONE   = 0,
  TOUCH_ACTION_DOWN   = 1,
  TOUCH_ACTION_MOVE   = 2,
  TOUCH_ACTION_UP     = 3,
  TOUCH_ACTION_DOWNUP = 4
} TouchAction_t;

typedef struct {
  uint8_t      id;     /* 触点号 0-16 */
  uint16_t     x;      /* 原始/未翻转坐标 X */
  uint16_t     y;      /* 原始/未翻转坐标 Y */
  TouchAction_t action;/* 动作类型 0-4 */
} TouchInfo_t;

static volatile TouchInfo_t g_last_touch;
static volatile uint8_t     g_last_touch_valid = 0;  /* 是否有有效触点数据 */

/* 触点事件队列：用于 CHGNO 模式下依次上传多个触摸点（TCH0/TCH1/...） */
#define TOUCH_QUEUE_SIZE 32
static volatile TouchInfo_t g_touch_queue[TOUCH_QUEUE_SIZE];
static volatile uint8_t     g_touch_q_head = 0;  /* 出队索引 */
static volatile uint8_t     g_touch_q_tail = 0;  /* 入队索引 */

/* 菜单状态机 */
typedef enum {
  MENU_IDLE = 0,      /* 空闲状态 */
  MENU_MAIN,          /* 主菜单: u/q */
  MENU_DUMP_TYPE,     /* 导出类型选择: m/s/k/a/q */
  MENU_MUTUAL_CAP,    /* 互容选择: d/r/q */
  MENU_SELF_CAP,      /* 自容选择: d/r/s/q */
  MENU_RUNNING        /* 运行中 */
} MenuState_t;

/* 消息解析辅助变量 */
static uint32_t g_last_msg_time = 0;  /* 上次消息时间 */
static uint8_t g_msg_output_enabled = 1;  /* 消息输出使能 (默认开启) */
volatile uint8_t g_chg_pending = 0;   /* CHG 中断置位，主循环处理并清零 */
static uint8_t g_chg_process_enabled = 0;  /* 是否处理 CHG 消息：0=不处理(默认)，1=处理；CHGON/CHGOFF 切换 */

/* 消息缓冲区（避免USB阻塞）
 * 提高容量以减少在 RAW/矩阵输出时因 CDC 吞吐不足导致的溢出/截断。
 */
#define MSG_BUFFER_SIZE 2048
static char g_msg_buffer[MSG_BUFFER_SIZE];
static uint16_t g_msg_buffer_head = 0;  /* 写入位置 */
static uint16_t g_msg_buffer_tail = 0;  /* 读取位置 */
static uint8_t g_msg_buffer_overflow = 0;  /* 预留：用于告警（避免死等） */
static uint32_t g_diag_interval_ms = 1000; /* 诊断输出间隔，可由命令设置 */
static uint8_t g_stream_rot = 0;      /* 0=无旋转,1=CW90,2=CCW90 */
static uint8_t g_stream_flip = 0;     /* bit0=X, bit1=Y，用于 MAP16 矩阵旋转/翻转 */
static uint8_t g_stream_map16_hex = 0;/* 1=启动MAP16 二进制输出(Mode3包) */
static uint8_t g_stream_map16_char = 0;/* 1=启动MAP16 文本输出 */
static uint8_t g_stream_frame_id = 0; /* Mode3 风格帧号自增 */
static uint8_t g_stream_chgno = 0;    /* 1=START CHGNO 模式：Mode3 包中增加触点信息 */
static uint8_t g_stream_touch_flip = 0; /* bit0=X, bit1=Y，仅用于 CHGNO 触点坐标翻转 */
static uint8_t g_stream_pre_cal = 0;  /* 1=每次循环采集前执行一次 CAL（用于 START1） */

/* 命令队列（在主循环中处理） */
#define CMD_BUFFER_SIZE 64
static char g_cmd_buffer[CMD_BUFFER_SIZE];
static uint8_t g_cmd_pending = 0;  /* 有命令待处理 */

static MenuState_t g_menu_state = MENU_IDLE;
static uint32_t g_last_diag_time = 0;  /* 上次诊断读取时间 */
static uint8_t g_t37_reading = 0;      /* T37读取进行中标志，阻止CHG消息处理 */
/* USER CODE END PRIVATE_VARIABLES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Exported_Variables USBD_CDC_IF_Exported_Variables
  * @brief Public variables.
  * @{
  */

extern USBD_HandleTypeDef hUsbDeviceFS;

/* USER CODE BEGIN EXPORTED_VARIABLES */
/* 导出全局变量供main.c使用 */
extern uint8_t g_matrix_x_size;
extern uint8_t g_matrix_y_size;
/* USER CODE END EXPORTED_VARIABLES */

/**
  * @}
  */

/** @defgroup USBD_CDC_IF_Private_FunctionPrototypes USBD_CDC_IF_Private_FunctionPrototypes
  * @brief Private functions declaration.
  * @{
  */

static int8_t CDC_Init_FS(void);
static int8_t CDC_DeInit_FS(void);
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length);
static int8_t CDC_Receive_FS(uint8_t* pbuf, uint32_t *Len);

/* USER CODE BEGIN PRIVATE_FUNCTIONS_DECLARATION */
static uint8_t MXT_I2C_Write(uint8_t addr, uint16_t reg, const uint8_t *data, uint16_t len);
static uint8_t MXT_I2C_Read(uint8_t addr, uint16_t reg, uint8_t *data, uint16_t len);
static uint8_t MXT_I2C_WriteNoReg(uint8_t addr, const uint8_t *data, uint16_t len);
static uint8_t MXT_I2C_ReadNoReg(uint8_t addr, uint8_t *data, uint16_t len);
static uint8_t MXT_I2C_Probe(uint8_t addr);
static uint8_t MXT_FindI2CAddress(void);
static void ProcessBridgePacket(uint8_t *buf, uint32_t len);
static void ProcessStringCommand(uint8_t *buf, uint32_t len);
static void SendResponse(uint8_t *data, uint16_t len);
/* 新增函数声明 */
static uint8_t MXT_ReadInfoBlock(void);
static uint8_t MXT_ReadObjectTable(void);
//static uint8_t MXT_EnableSPIMode(void);
static void MXT_ReadT100UnknownFields(void);
static void MXT_ExportConfigAsTxt(void);
static void MXT_ExportConfigAsBin(void);
static uint8_t MXT_ApplyStartupDebugCtrl(void);
static void MXT_SPI_StartIT(void);
static void MXT_SPI_StopIT(void);
static void MXT_SPI_USBFlush(void);
static void SPIUSB_ResetState(uint8_t mode);
static void SPIUSB_LineEnqueue(const char *s);
static void SPIUSB_ByteEnqueue(const uint8_t *data, uint16_t len);
static void SPIUSB_HexEnqueueByte(uint8_t b);
static void SPIUSB_HexEnqueueBytesWithNewline(const uint8_t *data, uint16_t len);
static void SPIUSB_Start1_HandlePageMarker(void);
static void SPIUSB_Start1_ProcessPayloadByte(uint8_t b);
static void SPIUSB_Start3_ProcessCroppedByte(uint8_t b);
static void SPIUSB_Start3_EmitRowPacket(void);
static uint16_t CRC16_CCITT_FALSE(const uint8_t *data, uint16_t length);
static void SPIUSB_TryFlush(void);

static void MXT_EnableOutput(uint8_t enable);
static uint8_t MXT_IsOutputEnabled(void);
static void MXT_ShowMainMenu(void);
static void MXT_ShowDumpTypeMenu(void);
static void MXT_ShowMutualCapMenu(void);
static void MXT_ShowSelfCapMenu(void);

static uint8_t MXT_ReadT37Page(uint8_t mode, uint8_t page);
static uint8_t MXT_ReadCompleteDiagnosticFrame(void);
static void MXT_OutputDiagnosticData(void);
static void MXT_OutputMapAll(void);   /* 输出完整矩阵 (mapall) */
static void MXT_OutputMap16(void);    /* 输出 16x16，行列顺序 0-15 (map16) */
static void MXT_OutputMap16Transformed(uint8_t rot, uint8_t flip_mask); /* 先旋转再翻转：通用实现 */
static void MXT_SendMode3Packets(uint8_t rot, uint8_t flip_mask, uint8_t frame_id);
static void MXT_SendSelfCapMode3Packets(uint8_t use_map16, uint8_t frame_id);
static uint16_t Map16_CalcCRC16(const uint8_t *data, uint16_t length);
static void MSG_BufferWrite(const char *str);
static uint8_t MSG_BufferFlush(void);  /* 返回1=已发送数据, 0=缓冲区空或USB忙 */
static void ProcessPendingCommand(void);
static int MXT_ParseMessage(char *msg_str, int pos, int max_len, uint8_t report_id, uint8_t *msg_data);
static void MXT_TouchQueuePush(uint8_t id, uint16_t x, uint16_t y, TouchAction_t action);
static uint8_t MXT_TouchQueuePop(TouchInfo_t *out);
/* USER CODE END PRIVATE_FUNCTIONS_DECLARATION */

/**
  * @}
  */

USBD_CDC_ItfTypeDef USBD_Interface_fops_FS =
{
  CDC_Init_FS,
  CDC_DeInit_FS,
  CDC_Control_FS,
  CDC_Receive_FS
};

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  Initializes the CDC media low layer over the FS USB IP
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Init_FS(void)
{
  /* USER CODE BEGIN 3 */
  /* Set Application Buffers */
  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBufferFS, 0);
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);

  /*
   * 注意：CDC_Init_FS 发生在“枚举/配置”阶段，往往早于 PC 端真正打开串口。
   * 此时如果调用 USB_SendString()，因为 dev_state 还不是 CONFIGURED，会直接丢弃，
   * 导致你看到“打开串口无任何输出”。
   *
   * 这里改为写入消息缓冲区：等枚举完成且主循环调用 MXT_FlushMessageBuffer() 后再发出。
   */

  MSG_BufferWrite("\r\n======================================\r\n");
  MSG_BufferWrite("    maXTouch640 USB CDC Interface\r\n");
  MSG_BufferWrite("======================================\r\n");

  /* 查找I2C设备地址 */
  MSG_BufferWrite("Scanning I2C address...\r\n");
  uint8_t found_addr = MXT_FindI2CAddress();
  if (found_addr != STATUS_NO_DEVICE) {
    char addr_str[64];
    snprintf(addr_str, sizeof(addr_str), "Found device at I2C address: 0x%02X\r\n", found_addr);
    MSG_BufferWrite(addr_str);

    /* 读取设备信息 */
    MSG_BufferWrite("Reading device info...\r\n");
    MXT_InitTouchScreen();
  } else {
    MSG_BufferWrite("ERROR: No maXTouch device found!\r\n");
    MSG_BufferWrite("Please check I2C connections and power.\r\n");
  }

  /* 显示配置信息 */
  if (g_matrix_x_size > 0 && g_matrix_y_size > 0) {
    char config_str[64];
    snprintf(config_str, sizeof(config_str), "Matrix Size: X=%d, Y=%d\r\n", g_matrix_x_size, g_matrix_y_size);
    MSG_BufferWrite(config_str);
  }

  MSG_BufferWrite("\r\nAvailable commands:\r\n");
  MSG_BufferWrite("  HELP   - Show available commands\r\n");
  MSG_BufferWrite("  INFO   - Read Info Block (0x0000, 7 bytes)\r\n");
  MSG_BufferWrite("  OBJTBL - Read object table (0x0007, first 10 objects)\r\n");
  MSG_BufferWrite("  MSGCNT - Read message count (T44 from object table)\r\n");
  MSG_BufferWrite("  MSG    - Read one message (T5 from object table)\r\n");
  MSG_BufferWrite("  START  - Start diagnostic output (1000ms interval)\r\n");
  MSG_BufferWrite("  STOP   - Stop diagnostic output\r\n");
  MSG_BufferWrite("  FRAME  - Read one diagnostic frame (Mutual Reference)\r\n");
  MSG_BufferWrite("  u      - Enter diagnostic menu\r\n");
  MSG_BufferWrite("-------------------------------------\r\n");

  return (USBD_OK);
  /* USER CODE END 3 */
}

/**
  * @brief  DeInitializes the CDC media low layer
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_DeInit_FS(void)
{
  /* USER CODE BEGIN 4 */
  return (USBD_OK);
  /* USER CODE END 4 */
}

/**
  * @brief  Manage the CDC class requests
  * @param  cmd: Command code
  * @param  pbuf: Buffer containing command data (request parameters)
  * @param  length: Number of data to be sent (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length)
{
  /* USER CODE BEGIN 5 */
  switch(cmd)
  {
    case CDC_SEND_ENCAPSULATED_COMMAND:

    break;

    case CDC_GET_ENCAPSULATED_RESPONSE:

    break;

    case CDC_SET_COMM_FEATURE:

    break;

    case CDC_GET_COMM_FEATURE:

    break;

    case CDC_CLEAR_COMM_FEATURE:

    break;

  /*******************************************************************************/
  /* Line Coding Structure                                                       */
  /*-----------------------------------------------------------------------------*/
  /* Offset | Field       | Size | Value  | Description                          */
  /* 0      | dwDTERate   |   4  | Number |Data terminal rate, in bits per second*/
  /* 4      | bCharFormat |   1  | Number | Stop bits                            */
  /*                                        0 - 1 Stop bit                       */
  /*                                        1 - 1.5 Stop bits                    */
  /*                                        2 - 2 Stop bits                      */
  /* 5      | bParityType |  1   | Number | Parity                               */
  /*                                        0 - None                             */
  /*                                        1 - Odd                              */
  /*                                        2 - Even                             */
  /*                                        3 - Mark                             */
  /*                                        4 - Space                            */
  /* 6      | bDataBits  |   1   | Number Data bits (5, 6, 7, 8 or 16).          */
  /*******************************************************************************/
    case CDC_SET_LINE_CODING:

    break;

    case CDC_GET_LINE_CODING:

    break;

    case CDC_SET_CONTROL_LINE_STATE:

    break;

    case CDC_SEND_BREAK:

    break;

  default:
    break;
  }

  return (USBD_OK);
  /* USER CODE END 5 */
}

/**
  * @brief  Data received over USB OUT endpoint are sent over CDC interface
  *         through this function.
  *
  *         @note
  *         This function will issue a NAK packet on any OUT packet received on
  *         USB endpoint until exiting this function. If you exit this function
  *         before transfer is complete on CDC interface (ie. using DMA controller)
  *         it will result in receiving more data while previous ones are still
  *         not sent.
  *
  * @param  Buf: Buffer of data to be received
  * @param  Len: Number of data received (in bytes)
  * @retval Result of the operation: USBD_OK if all operations are OK else USBD_FAIL
  */
static int8_t CDC_Receive_FS(uint8_t* Buf, uint32_t *Len)
{
  /* USER CODE BEGIN 6 */
  if(Buf != NULL && *Len > 0)
  {
    ProcessBridgePacket(Buf, *Len);
  }
  
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, &Buf[0]);
  USBD_CDC_ReceivePacket(&hUsbDeviceFS);
  return (USBD_OK);
  /* USER CODE END 6 */
}

/**
  * @brief  CDC_Transmit_FS
  *         Data to send over USB IN endpoint are sent over CDC interface
  *         through this function.
  *         @note
  *
  *
  * @param  Buf: Buffer of data to be sent
  * @param  Len: Number of data to be sent (in bytes)
  * @retval USBD_OK if all operations are OK else USBD_FAIL or USBD_BUSY
  * @note   USBD_BUSY 表示上一包仍在发送，本次数据不会被排队，调用方会丢包；
  *         应用层在模式1下应通过 MSG_BufferWrite/Flush 发送，由 Flush 在空闲时重试
  */
uint8_t CDC_Transmit_FS(uint8_t* Buf, uint16_t Len)
{
  uint8_t result = USBD_OK;
  /* USER CODE BEGIN 7 */
  USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassData;
  if (hcdc->TxState != 0) {
    return USBD_BUSY;  /* 上一包未发完，本包丢弃 */
  }
  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, Buf, Len);
  result = USBD_CDC_TransmitPacket(&hUsbDeviceFS);
  /* USER CODE END 7 */
  return result;
}

/* USER CODE BEGIN PRIVATE_FUNCTIONS_IMPLEMENTATION */

/**
  * @brief  简单的微秒级延时函数（基于空循环）
  * @note   只在主循环环境中短暂等待 USB/I2C 空闲使用，避免使用 HAL_Delay(ms)
  */
static void MXT_DelayUs(uint32_t us)
{
  /* 估算每个空循环的大致周期数，这里给一个保守系数 */
  uint32_t cycles = (SystemCoreClock / 1000000U) * us / 5U;
  while (cycles--) {
    __NOP();
  }
}

/**
  * @brief  等待 USB 传输完成并发送响应
  */
static void SendResponse(uint8_t *data, uint16_t len)
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

/**
  * @brief  探测 I2C 地址是否有设备响应 (使用硬件I2C)
  * @retval 0-有响应, 1-无响应
  */
static uint8_t MXT_I2C_Probe(uint8_t addr)
{
    return HAL_I2C_IsDeviceReady(&hi2c2, (uint16_t)(addr << 1), 3, I2C_TIMEOUT_MS) == HAL_OK ? 0 : 1;
}

/**
  * @brief  自动扫描查找 I2C 地址
  * @retval 找到的地址，或 0x81 表示未找到
  */
static uint8_t MXT_FindI2CAddress(void)
{
    /* 优先检查 Application 模式地址 */
    if (MXT_I2C_Probe(MXT_I2C_ADDR_APP_HIGH) == 0) {
        g_mxt_i2c_addr = MXT_I2C_ADDR_APP_HIGH;
        return MXT_I2C_ADDR_APP_HIGH;
    }
    if (MXT_I2C_Probe(MXT_I2C_ADDR_APP_LOW) == 0) {
        g_mxt_i2c_addr = MXT_I2C_ADDR_APP_LOW;
        return MXT_I2C_ADDR_APP_LOW;
    }
    
    /* 检查 Bootloader 模式地址 */
    if (MXT_I2C_Probe(MXT_I2C_ADDR_BL_HIGH) == 0) {
        g_mxt_i2c_addr = MXT_I2C_ADDR_BL_HIGH;
        return MXT_I2C_ADDR_BL_HIGH;
    }
    if (MXT_I2C_Probe(MXT_I2C_ADDR_BL_LOW) == 0) {
        g_mxt_i2c_addr = MXT_I2C_ADDR_BL_LOW;
        return MXT_I2C_ADDR_BL_LOW;
    }
    if (MXT_I2C_Probe(MXT_I2C_ADDR_BL_ALT) == 0) {
        g_mxt_i2c_addr = MXT_I2C_ADDR_BL_ALT;
        return MXT_I2C_ADDR_BL_ALT;
    }
    
    return STATUS_NO_DEVICE;  /* 0x81 = 未找到设备 */
}

/**
  * @brief  maXTouch I2C 写寄存器（带寄存器地址，使用硬件I2C）
  *         使用 MXT_MEM_ADD 宏交换字节序
  */
static uint8_t MXT_I2C_Write(uint8_t addr, uint16_t reg, const uint8_t *data, uint16_t len)
{
    if (HAL_I2C_Mem_Write(&hi2c2, addr << 1, MXT_MEM_ADD(reg), I2C_MEMADD_SIZE_16BIT, (uint8_t*)data, len, I2C_TIMEOUT_MS) != HAL_OK) {
        return STATUS_ADDR_NACK;
    }
    return 0;
}

/**
  * @brief  maXTouch I2C 读寄存器（带 16 位寄存器地址，使用硬件I2C）
  *         MemAddSize 使用 I2C_MEMADD_SIZE_16BIT(0x10)，与 maXTouch 协议一致
  *         使用 MXT_MEM_ADD 宏交换字节序，因为 HAL 默认大端序发送，而 maXTouch 要求小端序
  */
static uint8_t MXT_I2C_Read(uint8_t addr, uint16_t reg, uint8_t *data, uint16_t len)
{
    if (HAL_I2C_Mem_Read(&hi2c2, addr << 1, MXT_MEM_ADD(reg), I2C_MEMADD_SIZE_16BIT, data, len, I2C_TIMEOUT_MS) != HAL_OK) {
        return STATUS_ADDR_NACK;
    }
    return 0;
}

/**
  * @brief  I2C 写数据（不带寄存器地址，用于 Bootloader 模式，使用硬件I2C）
  */
static uint8_t MXT_I2C_WriteNoReg(uint8_t addr, const uint8_t *data, uint16_t len)
{
    if (HAL_I2C_Master_Transmit(&hi2c2, (uint16_t)(addr << 1), (uint8_t*)data, len, I2C_TIMEOUT_MS) != HAL_OK) {
        return STATUS_ADDR_NACK;
    }
    return 0;
}

/**
  * @brief  I2C 读数据（不带寄存器地址，用于 Bootloader 模式，使用硬件I2C）
  */
static uint8_t MXT_I2C_ReadNoReg(uint8_t addr, uint8_t *data, uint16_t len)
{
    if (HAL_I2C_Master_Receive(&hi2c2, (uint16_t)(addr << 1), data, len, I2C_TIMEOUT_MS) != HAL_OK) {
        return STATUS_ADDR_NACK;
    }
    return 0;
}

/**
  * @brief  Process string command in string mode (mode 1)
  *         只存储命令，在主循环中处理
  */
static void ProcessStringCommand(uint8_t *buf, uint32_t len)
{
    /* 复制到临时缓冲区并去掉尾部 \r \n 和空格 */
    if (len >= CMD_BUFFER_SIZE) len = CMD_BUFFER_SIZE - 1;
    char trimmed[CMD_BUFFER_SIZE];
    memcpy(trimmed, buf, len);
    trimmed[len] = '\0';
    for (int i = len - 1; i >= 0; i--) {
        if (trimmed[i] == '\r' || trimmed[i] == '\n' || trimmed[i] == ' ') {
            trimmed[i] = '\0';
        } else {
            break;
        }
    }
    /* 若本包仅为 \r\n/空格（空命令），不覆盖原命令、不置位 pending，避免第二包覆盖掉 "help" */
    if (strlen(trimmed) == 0) return;
    /* 若已有待处理命令，忽略新命令 */
    if (g_cmd_pending) return;

    memcpy(g_cmd_buffer, trimmed, strlen(trimmed) + 1);
    g_cmd_pending = 1;
}

/**
  * @brief  在主循环中处理命令 (从命令队列)
  */
static void ProcessPendingCommand(void)
{
    if (!g_cmd_pending) return;
    g_cmd_pending = 0;
    
    char *cmd_str = g_cmd_buffer;
    

    
    /* Command: "MODE0" - Switch to I2C-USB bridge mode */
    if (strcmp(cmd_str, "MODE0") == 0 || strcmp(cmd_str, "mode0") == 0) {
        g_bridge_mode = BRIDGE_MODE_BINARY;
        g_menu_state = MENU_IDLE;
        USB_SendString("OK: Switched to I2C-USB bridge mode\r\n");
    }
    /* Command: "MODE1" - Switch to string mode */
    else if (strcmp(cmd_str, "MODE1") == 0 || strcmp(cmd_str, "mode1") == 0) {
        g_bridge_mode = BRIDGE_MODE_STRING;
        MXT_InitTouchScreen();
        g_menu_state = MENU_IDLE;
        USB_SendString("OK: Switched to string mode\r\n");
        char config_str[64];
        snprintf(config_str, sizeof(config_str), "Config: X=%d, Y=%d\r\n", g_matrix_x_size, g_matrix_y_size);
        USB_SendString(config_str);
        USB_SendString("Type 'u' to enter diagnostic menu\r\n");
    }
    else if (strcmp(cmd_str, "SPISTART") == 0 || strcmp(cmd_str, "spistart") == 0) {
        (void)MXT_ApplyStartupDebugCtrl();
        SPIUSB_ResetState(0U);
        g_spi_stream_enabled = 1U;
        MXT_SPI_StartIT();
        USB_SendString("INFO: SPI stream START (raw hex)\r\n");
    }
    else if (strcmp(cmd_str, "SPISTART1") == 0 || strcmp(cmd_str, "spistart1") == 0) {
        (void)MXT_ApplyStartupDebugCtrl();
        SPIUSB_ResetState(1U);
        g_spi_stream_enabled = 1U;
        MXT_SPI_StartIT();
        USB_SendString("INFO: SPI stream START1 (16x16 text)\r\n");
    }
    else if (strcmp(cmd_str, "SPISTART3") == 0 || strcmp(cmd_str, "spistart3") == 0) {
        (void)MXT_ApplyStartupDebugCtrl();
        SPIUSB_ResetState(2U);
        g_spi_stream_enabled = 1U;
        MXT_SPI_StartIT();
        USB_SendString("INFO: SPI stream START3 (cropped packets)\r\n");
    }
    else if (strcmp(cmd_str, "SPISTOP") == 0 || strcmp(cmd_str, "spistop") == 0) {
        g_spi_stream_enabled = 0U;
        SPIUSB_TryFlush();
        USB_SendString("INFO: SPI stream STOP\r\n");
    }
    /* Command: "START" / "START1" - Start diagnostic output
     * 语法1: START  [MAP16*] [HEX|CHAR] interval_ms   -> 默认 FRAME0 (Mutual Delta, 0x10)
     * 语法2: START1 [MAP16*] [HEX|CHAR] interval_ms   -> 默认 FRAME1 (Mutual Reference, 0x11)
     * 语法3: START CHGNO [X|Y|XY] interval_ms         -> CHGNO 模式在 Mode3 包中增加 [触点号,x,y,动作类型]
     * 说明: START1 模式下每次采集前会执行一次 CAL。
     */
    else if (strncmp(cmd_str, "START", 5) == 0 || strncmp(cmd_str, "start", 5) == 0) {
        uint8_t start_frame1_mode = (strncmp(cmd_str, "START1", 6) == 0 || strncmp(cmd_str, "start1", 6) == 0) ? 1 : 0;
        /* 解析参数 */
        char *tok = strtok(cmd_str, " "); /* START/START1 */
        char *arg1 = strtok(NULL, " ");   /* 可选: MAP16... */
        char *arg2 = strtok(NULL, " ");   /* 可选: HEX/CHAR 或 翻转/interval ms */
        char *arg3 = strtok(NULL, " ");   /* 可选: interval ms (若 arg2 是 HEX/CHAR 或 翻转) */

        /* 默认：无旋转翻转；MAP16 默认二进制（Mode3）；可用 CHAR 切换为文本 */
        g_stream_rot = 0;
        g_stream_flip = 0;        /* 仅用于 MAP16 矩阵旋转/翻转 */
        g_stream_touch_flip = 0;  /* 仅用于 CHGNO 触点坐标翻转 */
        g_stream_map16_hex = 0;
        g_stream_map16_char = 0;
        g_stream_chgno = 0;
        g_last_touch_valid = 0;

        /* 默认间隔字符串 */
        const char *iv_str = NULL;

        /* START CHGNO 模式：
         *  - 语法A: START CHGNO [X|Y|XY] [MAP16*] interval_ms
         *  - 语法B: START CHGNOXY [MAP16*] interval_ms   (X/Y/XY 后缀直接跟在 CHGNO 后面)
         *  使用与 MAP16 Mode3 相同的二进制输出（AA 10 33），只是额外在每行尾部附加触点信息。
         */
        if (arg1 && (strncasecmp(arg1, "CHGNO", 5) == 0)) {
            g_stream_chgno = 1;
            g_stream_map16_hex = 1;   /* CHGNO 模式强制启用 Mode3 二进制输出 */

            /* 1) 解析 CHGNO 自身可能携带的后缀（如 CHGNOX / CHGNOXY），仅作用于触点坐标 */
            const char *p_chg = arg1 + 5;
            for (; *p_chg; p_chg++) {
                if (*p_chg == 'X' || *p_chg == 'x') g_stream_touch_flip |= 0x01;
                else if (*p_chg == 'Y' || *p_chg == 'y') g_stream_touch_flip |= 0x02;
            }

            /* 2) 如果后面紧跟 MAP16 变体，则解析其旋转/翻转：START CHGNOXY MAP16L90X 10 */
            if (arg2 && (strncasecmp(arg2, "MAP16", 5) == 0)) {
                const char *p = arg2 + 5;
                if (*p == 'R' || *p == 'r') {
                    if (p[1] == '9' && p[2] == '0') { g_stream_rot = 1; p += 3; }
                } else if (*p == 'L' || *p == 'l') {
                    if (p[1] == '9' && p[2] == '0') { g_stream_rot = 2; p += 3; }
                }
                for (; *p; p++) {
                    if (*p == 'X' || *p == 'x') g_stream_flip |= 0x01;
                    else if (*p == 'Y' || *p == 'y') g_stream_flip |= 0x02;
                }
                /* MAP16 在 CHGNO 下始终为二进制 Mode3，不支持 CHAR 文本输出 */
                iv_str = arg3;  /* 第三个参数为间隔 ms */
            }
            else {
                /* 3) 无 MAP16 变体时，沿用原 CHGNO [X|Y|XY] interval_ms 语法（翻转在 arg2 中，仍然只影响触点坐标） */
                uint8_t has_flip = 0;
                if (arg2 && (strchr(arg2, 'X') || strchr(arg2, 'x') || strchr(arg2, 'Y') || strchr(arg2, 'y'))) {
                    const char *p = arg2;
                    for (; *p; p++) {
                        if (*p == 'X' || *p == 'x') g_stream_touch_flip |= 0x01;
                        else if (*p == 'Y' || *p == 'y') g_stream_touch_flip |= 0x02;
                    }
                    has_flip = 1;
                }
                /* 时间参数：若存在翻转，则取 arg3，否则取 arg2 */
                iv_str = has_flip ? arg3 : arg2;
            }

            USB_SendString("INFO: START CHGNO mode enabled (Mode3 + touch info)\r\n");
        }
        /* 参数1: MAP16 变体（如 MAP16R90X、MAP16L90XY 等），保持原有行为不变（无 CHGNO 时） */
        else if (arg1 && (strncasecmp(arg1, "MAP16", 5) == 0)) {
            const char *p = arg1 + 5;
            if (*p == 'R' || *p == 'r') {
                if (p[1] == '9' && p[2] == '0') { g_stream_rot = 1; p += 3; }
            } else if (*p == 'L' || *p == 'l') {
                if (p[1] == '9' && p[2] == '0') { g_stream_rot = 2; p += 3; }
            }
            for (; *p; p++) {
                if (*p == 'X' || *p == 'x') g_stream_flip |= 0x01;
                else if (*p == 'Y' || *p == 'y') g_stream_flip |= 0x02;
            }
            g_stream_map16_hex = 1; /* MAP16 变体默认二进制 */
            /* 可选后缀/参数：HEX 或 CHAR */
            if ((arg2 && strcasecmp(arg2, "CHAR") == 0) || (arg3 && strcasecmp(arg3, "CHAR") == 0)) {
                g_stream_map16_char = 1;
                g_stream_map16_hex = 0;
            }
            if ((arg2 && strcasecmp(arg2, "HEX") == 0) || (arg3 && strcasecmp(arg3, "HEX") == 0)) {
                g_stream_map16_hex = 1;
                g_stream_map16_char = 0;
            }

            /* MAP16 路径下，若未指定专门的间隔参数，则保持原逻辑 */
            if (!iv_str) {
                iv_str = (arg3 && ( (arg2 && (strcasecmp(arg2,"HEX")==0 || strcasecmp(arg2,"CHAR")==0)) )) ? arg3 : arg2;
            }
        }

        /* 若上面未设置 iv_str，则默认为原始 START 语法: 第二个参数为间隔 */
        if (!iv_str) {
            iv_str = arg2;
        }

        /* 参数: 间隔 ms */
        if (iv_str) {
            uint32_t iv = (uint32_t)atoi(iv_str);
            /* 允许的范围从原来的 50–5000 ms 放宽到 20–5000 ms */
            if (iv >= 10 && iv <= 5000) {
                g_diag_interval_ms = iv;
            } else {
                g_diag_interval_ms = 1000;
            }
        } else {
            g_diag_interval_ms = 1000;
        }

        /* 默认诊断模式：START=互容 Delta(0x10), START1=互容 Reference(0x11) */
        if (g_diag_mode == DIAG_MODE_NONE) {
            if (!g_touch_inited) {
                MXT_InitTouchScreen();
            }
            if (start_frame1_mode) {
                g_diag_mode = DIAG_MODE_MUTUAL_REF;
                USB_SendString("INFO: Default diag mode = Mutual Reference (0x11)\r\n");
            } else {
                g_diag_mode = DIAG_MODE_MUTUAL_DELTA;
                USB_SendString("INFO: Default diag mode = Mutual Delta (0x10)\r\n");
            }
        }

        /* START1 强制切到 FRAME1，并开启每次采集前 CAL */
        if (start_frame1_mode) {
            g_diag_mode = DIAG_MODE_MUTUAL_REF;
            g_stream_pre_cal = 1;
            USB_SendString("INFO: START1 mode = FRAME1 + pre-CAL\r\n");
        } else {
            g_stream_pre_cal = 0;
        }

        /* 启动输出，重置计时、帧号；并关闭 CHG 消息输出 */
        MXT_EnableOutput(1);
        g_last_diag_time = HAL_GetTick();
        g_stream_frame_id = 0;
        /* 普通 START/MAP16 保持原有行为：关闭 CHG 文本输出
         * START CHGNO 模式：为了能够依赖 CHG 队列获取触点坐标，不强制关闭消息处理
         */
        if (g_stream_chgno) {
            g_chg_process_enabled = 1;   /* 强制打开 CHG 处理，用于更新触点信息 */
            g_msg_output_enabled = 0;    /* 不需要文本输出，只使用消息更新触点坐标 */
        } else {
            g_msg_output_enabled = 0;    /* 等效 MSG_OFF */
        }
        USB_SendString("OK: Diagnostic output started\r\n");
        if (g_stream_map16_hex) {
            USB_SendString("INFO: MAP16 binary stream (Mode3 packets) enabled\r\n");
        } else if (g_stream_map16_char) {
            USB_SendString("INFO: MAP16 text output enabled\r\n");
        }
    }
    /* Command: "FRAME1" - Mutual Reference (0x11)
     * 扩展语法：FRAME1 [AT16] [HEX|CHAR]
     */
    else if (strncmp(cmd_str, "FRAME1", 6) == 0 || strncmp(cmd_str, "frame1", 6) == 0) {
        char local[CMD_BUFFER_SIZE];
        strncpy(local, cmd_str, sizeof(local) - 1);
        local[sizeof(local) - 1] = '\0';

        strtok(local, " ");
        char *arg1 = strtok(NULL, " ");
        char *arg2 = strtok(NULL, " ");

        uint8_t use_map16 = 0;
        uint8_t out_hex = 0;

        if (arg1 && strcasecmp(arg1, "AT16") == 0) {
            use_map16 = 1;
            if (arg2 && strcasecmp(arg2, "HEX") == 0) out_hex = 1;
        }

        if (!g_touch_inited) MXT_InitTouchScreen();
        g_diag_mode = DIAG_MODE_MUTUAL_REF;
        if (!(use_map16 && out_hex)) {
            USB_SendString("INFO: Mutual Reference (0x11)\r\n");
        }
        uint8_t res = MXT_ReadCompleteDiagnosticFrame();
        if (res == 0) {
            if (use_map16) {
                if (out_hex) {
                    MXT_SendMode3Packets(0, 0, g_stream_frame_id++);
                } else {
                    MXT_OutputMap16();
                }
            } else {
                MXT_OutputDiagnosticData();
            }
        } else {
            char err[64];
            snprintf(err, sizeof(err), "ERR: FRAME read failed, code 0x%02X\r\n", res);
            USB_SendString(err);
        }
    }

    /* Command: "FRAME0" - Mutual Delta (0x10)
     * 扩展语法：FRAME0 [AT16] [HEX|CHAR]
     */
    else if (strncmp(cmd_str, "FRAME0", 6) == 0 || strncmp(cmd_str, "frame0", 6) == 0) {
        char local[CMD_BUFFER_SIZE];
        strncpy(local, cmd_str, sizeof(local) - 1);
        local[sizeof(local) - 1] = '\0';

        char *tok = strtok(local, " ");
        char *arg1 = strtok(NULL, " ");
        char *arg2 = strtok(NULL, " ");

        uint8_t use_map16 = 0;
        uint8_t out_hex = 0;  /* 0=CHAR(文本) */

        if (arg1 && strcasecmp(arg1, "AT16") == 0) {
            use_map16 = 1;
            if (arg2 && strcasecmp(arg2, "HEX") == 0) out_hex = 1;
            else out_hex = 0; /* 默认 CHAR */
        }

        (void)tok;
        if (!g_touch_inited) MXT_InitTouchScreen();
        g_diag_mode = DIAG_MODE_MUTUAL_DELTA;
        if (!(use_map16 && out_hex)) {
            USB_SendString("INFO: Mutual Delta (0x10)\r\n");
        }
        uint8_t res = MXT_ReadCompleteDiagnosticFrame();
        if (res == 0) {
            if (use_map16) {
                if (out_hex) {
                    MXT_SendMode3Packets(0, 0, g_stream_frame_id++);
                } else {
                    MXT_OutputMap16();
                }
            } else {
                MXT_OutputDiagnosticData();
            }
        } else {
            char err[64];
            snprintf(err, sizeof(err), "ERR: FRAME read failed, code 0x%02X\r\n", res);
            USB_SendString(err);
        }
    }
    /* Command: "FRAME3" - Self Delta (0xF7)
     * 扩展语法：FRAME3 [AT16] [HEX|CHAR]
     * - 默认(无参数)：文本输出（Y:/X:）
     * - MAP16：裁剪为 Y前16 + X前16
     *   - HEX：Mode3 二进制包（AA 10 33...），不裁剪发4包，裁剪发2包；line 字段作发送计数
     *   - CHAR：文本输出（当前实现仍输出完整 Y20/X32；若你需要裁剪文本也可再加）
     */
    else if (strncmp(cmd_str, "FRAME3", 6) == 0 || strncmp(cmd_str, "frame3", 6) == 0) {
        char local[CMD_BUFFER_SIZE];
        strncpy(local, cmd_str, sizeof(local) - 1);
        local[sizeof(local) - 1] = '\0';

        (void)strtok(local, " ");
        char *arg1 = strtok(NULL, " ");
        char *arg2 = strtok(NULL, " ");

        uint8_t use_map16 = 0;
        uint8_t out_hex = 0;
        if (arg1 && strcasecmp(arg1, "AT16") == 0) {
            use_map16 = 1;
            if (arg2 && strcasecmp(arg2, "HEX") == 0) out_hex = 1;
        }

        if (!g_touch_inited) MXT_InitTouchScreen();
        g_diag_mode = DIAG_MODE_SELF_DELTA;
        if (!(use_map16 && out_hex)) {
            USB_SendString("INFO: Self Delta (0xF7)\r\n");
        }
        uint8_t res = MXT_ReadCompleteDiagnosticFrame();
        if (res == 0) {
            if (out_hex) {
                MXT_SendSelfCapMode3Packets(use_map16, g_stream_frame_id++);
            } else {
                MXT_OutputDiagnosticData();
            }
        } else {
            char err[64];
            snprintf(err, sizeof(err), "ERR: FRAME read failed, code 0x%02X\r\n", res);
            USB_SendString(err);
        }
    }
    /* Command: "FRAME4" - Self Reference (0xF8)
     * 扩展语法：FRAME4 [AT16] [HEX|CHAR]
     */
    else if (strncmp(cmd_str, "FRAME4", 6) == 0 || strncmp(cmd_str, "frame4", 6) == 0) {
        char local[CMD_BUFFER_SIZE];
        strncpy(local, cmd_str, sizeof(local) - 1);
        local[sizeof(local) - 1] = '\0';

        (void)strtok(local, " ");
        char *arg1 = strtok(NULL, " ");
        char *arg2 = strtok(NULL, " ");

        uint8_t use_map16 = 0;
        uint8_t out_hex = 0;
        if (arg1 && strcasecmp(arg1, "AT16") == 0) {
            use_map16 = 1;
            if (arg2 && strcasecmp(arg2, "HEX") == 0) out_hex = 1;
        }

        if (!g_touch_inited) MXT_InitTouchScreen();
        g_diag_mode = DIAG_MODE_SELF_REF;
        if (!(use_map16 && out_hex)) {
            USB_SendString("INFO: Self Reference (0xF8)\r\n");
        }
        uint8_t res = MXT_ReadCompleteDiagnosticFrame();
        if (res == 0) {
            if (out_hex) {
                MXT_SendSelfCapMode3Packets(use_map16, g_stream_frame_id++);
            } else {
                MXT_OutputDiagnosticData();
            }
        } else {
            char err[64];
            snprintf(err, sizeof(err), "ERR: FRAME read failed, code 0x%02X\r\n", res);
            USB_SendString(err);
        }
    }
    /* Command: "FRAME5" - Read Key Array diagnostic data */
    else if (strcmp(cmd_str, "FRAME5") == 0 || strcmp(cmd_str, "frame5") == 0) {
        if (!g_touch_inited) {
            MXT_InitTouchScreen();
        }
        /* Key Array使用Self Signal模式 (0xF5) */
        g_diag_mode = DIAG_MODE_SELF_SIGNAL;
        USB_SendString("INFO: Key Array / Self Signal (0xF5)\r\n");
        uint8_t res = MXT_ReadCompleteDiagnosticFrame();
        if (res == 0) {
            MXT_OutputDiagnosticData();
        } else {
            char err[64];
            snprintf(err, sizeof(err), "ERR: FRAME read failed, code 0x%02X\r\n", res);
            USB_SendString(err);
        }
    }
    /* Command: "FRAME38" - Read Self DC level estimate (0x38) */
    else if (strcmp(cmd_str, "FRAME38") == 0 || strcmp(cmd_str, "frame38") == 0) {
        if (!g_touch_inited) {
            MXT_InitTouchScreen();
        }
        g_diag_mode = DIAG_MODE_SELF_DC;
        USB_SendString("INFO: Self DC level estimate (0x38)\r\n");
        uint8_t res = MXT_ReadCompleteDiagnosticFrame();
        if (res == 0) {
            MXT_OutputDiagnosticData();
        } else {
            char err[64];
            snprintf(err, sizeof(err), "ERR: FRAME read failed, code 0x%02X\r\n", res);
            USB_SendString(err);
        }
    }
    /* Command: "STOP" - Stop diagnostic output */
    else if (strcmp(cmd_str, "STOP") == 0 || strcmp(cmd_str, "stop") == 0) {
        /* STOP should fully stop all periodic output paths */
        g_spi_stream_enabled = 0U;
        SPIUSB_TryFlush();
        MXT_EnableOutput(0);
        g_stream_pre_cal = 0;
        g_stream_chgno = 0;
        USB_SendString("OK: Diagnostic output stopped\r\n");
    }
    /* Command: "MSG_ON" - Enable message output */
    else if (strcmp(cmd_str, "MSG_ON") == 0 || strcmp(cmd_str, "msg_on") == 0) {
        g_msg_output_enabled = 1;
        USB_SendString("OK: Message output enabled\r\n");
    }
    /* Command: "MSG_OFF" - Disable message output */
    else if (strcmp(cmd_str, "MSG_OFF") == 0 || strcmp(cmd_str, "msg_off") == 0) {
        g_msg_output_enabled = 0;
        USB_SendString("OK: Message output disabled\r\n");
    }
    /* Command: "CHGON" - Enable CHG processing and perform one CHG read
     * 注意：START 会将 g_msg_output_enabled 置 0 以关闭 CHG 消息输出；
     * 为了在 STOP 之后仅靠 CHGON 也能恢复 CHG 处理，这里同时重新打开消息输出。
     */
    else if (strcmp(cmd_str, "CHGON") == 0 || strcmp(cmd_str, "chgon") == 0) {
        g_msg_output_enabled = 1;       /* 确保 MXT_CheckAndProcessMessages() 不再被 MSG_OFF 拦住 */
        g_chg_process_enabled = 1;
        USB_SendString("CHG processing enabled\r\n");
        MXT_CheckAndProcessMessages();  /* 立即执行一次 CHG 读取 */
    }
    /* Command: "CHGOFF" - Disable CHG processing (default) */
    else if (strcmp(cmd_str, "CHGOFF") == 0 || strcmp(cmd_str, "chgoff") == 0) {
        g_chg_process_enabled = 0;
        USB_SendString("CHG processing disabled (default)\r\n");
    }
    /* Command: "MAPALL" - Read one frame and output full matrix */
    else if (strcmp(cmd_str, "MAPALL") == 0 || strcmp(cmd_str, "mapall") == 0) {
        if (!g_touch_inited) MXT_InitTouchScreen();
        if (g_diag_mode == DIAG_MODE_NONE) g_diag_mode = DIAG_MODE_MUTUAL_DELTA;
        if (MXT_ReadCompleteDiagnosticFrame() == 0) {
            MXT_OutputMapAll();
        } else {
            USB_SendString("ERR: MAPALL read frame failed\r\n");
        }
    }
    /* Command: "MAP16" - Read one frame and output 16x16 (index 0-15) */
    else if (strcmp(cmd_str, "MAP16") == 0 || strcmp(cmd_str, "map16") == 0) {
        if (!g_touch_inited) MXT_InitTouchScreen();
        if (g_diag_mode == DIAG_MODE_NONE) g_diag_mode = DIAG_MODE_MUTUAL_DELTA;
        if (MXT_ReadCompleteDiagnosticFrame() == 0) {
            MXT_OutputMap16();
        } else {
            USB_SendString("ERR: MAP16 read frame failed\r\n");
        }
    }
    /* Command: "RESET" - Reset the controller */
    else if (strcasecmp(cmd_str, "RESET") == 0) {
        if (!g_touch_inited) MXT_InitTouchScreen();
        if (g_t6_addr == 0) {
            USB_SendString("ERR: T6 not found\r\n");
        } else {
            uint8_t val = 0x01;
            if (MXT_I2C_Write(g_mxt_i2c_addr, g_t6_addr, &val, 1) == 0) {
                USB_SendString("OK: Reset command sent\r\n");
                g_touch_inited = 0; /* 重置后标记为未初始化 */
            } else {
                USB_SendString("ERR: Reset failed\r\n");
            }
        }
    }
    /* Command: "CALIBRATE" or "CAL" - Calibrate the controller */
    else if (strcasecmp(cmd_str, "CALIBRATE") == 0 || strcasecmp(cmd_str, "CAL") == 0) {
        if (!g_touch_inited) MXT_InitTouchScreen();
        if (g_t6_addr == 0) {
            USB_SendString("ERR: T6 not found\r\n");
        } else {
            uint8_t val = 0x01;
            if (MXT_I2C_Write(g_mxt_i2c_addr, g_t6_addr + 2, &val, 1) == 0) {
                USB_SendString("OK: Calibration command sent\r\n");
            } else {
                USB_SendString("ERR: Calibration failed\r\n");
            }
        }
    }
    /* Command: "BACKUP" - Backup config to NV memory */
    else if (strcasecmp(cmd_str, "BACKUP") == 0) {
        if (!g_touch_inited) MXT_InitTouchScreen();
        if (g_t6_addr == 0) {
            USB_SendString("ERR: T6 not found\r\n");
        } else {
            uint8_t val = 0x55;
            if (MXT_I2C_Write(g_mxt_i2c_addr, g_t6_addr + 1, &val, 1) == 0) {
                USB_SendString("OK: Backup command sent\r\n");
            } else {
                USB_SendString("ERR: Backup failed\r\n");
            }
        }
    }
    /* Command: "EXPORTTXT" - Export compact config text (summary only) */
    else if (strcasecmp(cmd_str, "EXPORTTXT") == 0 || strcasecmp(cmd_str, "DUMPTXT") == 0) {
        MXT_ExportConfigAsTxt();
    }
    /* Command: "EXPORTBIN" - Export configuration as compact binary stream */
    else if (strcasecmp(cmd_str, "EXPORTBIN") == 0 || strcasecmp(cmd_str, "DUMPBIN") == 0) {
        MXT_ExportConfigAsBin();
    }
    /* MAP16 组合指令：先旋转(R90/L90 可选)，再翻转(X/Y/XY 可选)
     * 支持：MAP16R90X / MAP16R90Y / MAP16R90XY / MAP16L90X / MAP16L90Y / MAP16L90XY
     * 以及原有：MAP16R90 / MAP16L90 / MAP16X / MAP16Y / MAP16XY
     */
    else if (strncmp(cmd_str, "MAP16", 5) == 0 || strncmp(cmd_str, "map16", 5) == 0) {
        uint8_t rot = 0;       /* 0=不旋转, 1=CW90, 2=CCW90 */
        uint8_t flip = 0;      /* bit0=X, bit1=Y */
        const char *p = cmd_str + 5; /* after MAP16 */
        /* 允许空后缀：MAP16 已在上面处理；这里处理带后缀的组合 */
        if (*p == 'R' || *p == 'r') {
            if ((p[1] == '9') && (p[2] == '0')) { rot = 1; p += 3; }
        } else if (*p == 'L' || *p == 'l') {
            if ((p[1] == '9') && (p[2] == '0')) { rot = 2; p += 3; }
        }
        /* 翻转后缀：X / Y / XY（顺序不敏感） */
        for (; *p; p++) {
            if (*p == 'X' || *p == 'x') flip |= 0x01;
            else if (*p == 'Y' || *p == 'y') flip |= 0x02;
        }

        /* 仅当确实是这些后缀之一才处理；否则让后续 UNKNOWN 去报错 */
        if (rot != 0 || flip != 0) {
            if (!g_touch_inited) MXT_InitTouchScreen();
            if (g_diag_mode == DIAG_MODE_NONE) g_diag_mode = DIAG_MODE_MUTUAL_DELTA;
            if (MXT_ReadCompleteDiagnosticFrame() == 0) {
                MXT_OutputMap16Transformed(rot, flip);
            } else {
                USB_SendString("ERR: MAP16* read frame failed\r\n");
            }
        } else {
            USB_SendString("ERROR: Unknown MAP16 variant\r\n");
        }
    }
    /* Command: "INFO" - Read Info Block (addr 0x0000, 7 bytes), ref maXTouch_Serial_Commands.md */
    else if (strcmp(cmd_str, "INFO") == 0 || strcmp(cmd_str, "info") == 0) {
        if (!g_touch_inited) MXT_InitTouchScreen();
        uint8_t id_info[7];
        if (MXT_I2C_Read(g_mxt_i2c_addr, 0x0000, id_info, 7) != 0) {
            USB_SendString("ERR: Read Info Block failed\r\n");
        } else {
            char line[128];
            snprintf(line, sizeof(line),
                     "Info Block: Family=0x%02X Variant=0x%02X Version=%d.%d Build=%d Matrix X=%d Y=%d NumObj=%d\r\n",
                     id_info[0], id_info[1], id_info[2] >> 4, id_info[2] & 0x0F, id_info[3],
                     id_info[4], id_info[5], id_info[6]);
            USB_SendString(line);
        }
    }
    /* Command: "OBJTBL" - Read object table (addr 0x0007, 6 bytes per object); read per-entry to avoid long I2C read */
    else if (strcmp(cmd_str, "OBJTBL") == 0 || strcmp(cmd_str, "objtbl") == 0 ||
             strcmp(cmd_str, "OBJ") == 0 || strcmp(cmd_str, "obj") == 0) {
        if (!g_touch_inited) MXT_InitTouchScreen();
        uint8_t n_obj = (g_num_objects > 0) ? g_num_objects : 40;
        if (n_obj > 64) n_obj = 64;
        USB_SendString("Object table:\r\n");
        uint8_t obj_entry[6];
        uint8_t fail = 0;
        for (uint8_t i = 0; i < n_obj && !fail; i++) {
            uint16_t obj_addr = 0x0007 + (uint16_t)i * 6;
            if (MXT_I2C_Read(g_mxt_i2c_addr, obj_addr, obj_entry, 6) != 0) {
                char line[64];
                snprintf(line, sizeof(line), "ERR: Read object %d at 0x%04X failed\r\n", i, obj_addr);
                USB_SendString(line);
                fail = 1;
                break;
            }
            uint8_t type = obj_entry[0];
            uint16_t addr = obj_entry[1] | (obj_entry[2] << 8);
            uint8_t size = obj_entry[3] + 1;
            uint8_t instances = obj_entry[4] + 1;
            uint8_t report_ids = obj_entry[5];
            char line[80];
            snprintf(line, sizeof(line), "  T%2d @ 0x%04X size=%2d inst=%2d rid=%2d\r\n",
                     type, addr, size, instances, report_ids);
            USB_SendString(line);
        }
    }
    /* Command: "MSGCNT" - Read message count (T44 addr from object table, 1 byte), ref maXTouch_Serial_Commands.md */
    else if (strcmp(cmd_str, "MSGCNT") == 0 || strcmp(cmd_str, "msgcnt") == 0) {
        if (!g_touch_inited) MXT_InitTouchScreen();
        uint16_t t44_addr = MXT_GetObjectAddr(44);
        if (t44_addr == 0) {
            USB_SendString("ERR: T44 not found in object table\r\n");
        } else {
            uint8_t cnt;
            char addr_str[64];
            snprintf(addr_str, sizeof(addr_str), "Reading T44 at addr=0x%04X...\r\n", t44_addr);
            USB_SendString(addr_str);
            if (MXT_I2C_Read(g_mxt_i2c_addr, t44_addr, &cnt, 1) != 0) {
                USB_SendString("ERR: Read message count failed\r\n");
            } else {
                char line[48];
                snprintf(line, sizeof(line), "Message count (T44): %d\r\n", cnt);
                USB_SendString(line);
            }
        }
    }
    /* Command: "MSG" - Read one message (T5 addr from object table, 11 bytes), ref maXTouch_Serial_Commands.md */
    else if (strcmp(cmd_str, "MSG") == 0 || strcmp(cmd_str, "msg") == 0) {
        if (!g_touch_inited) MXT_InitTouchScreen();
        uint16_t t5_addr = MXT_GetObjectAddr(5);
        if (t5_addr == 0) {
            USB_SendString("ERR: T5 not found in object table\r\n");
        } else {
            uint8_t msg_data[11];
            char addr_str[64];
            snprintf(addr_str, sizeof(addr_str), "Reading T5 at addr=0x%04X...\r\n", t5_addr);
            USB_SendString(addr_str);
            if (MXT_I2C_Read(g_mxt_i2c_addr, t5_addr, msg_data, 11) != 0) {
                USB_SendString("ERR: Read message failed\r\n");
            } else {
                char line[320];
                int pos = 0;
                pos += snprintf(line + pos, sizeof(line) - pos, "MSG raw: ");
                for (int j = 0; j < 11; j++)
                    pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", msg_data[j]);
                pos += snprintf(line + pos, sizeof(line) - pos, "\r\n");
                pos = MXT_ParseMessage(line, pos, (int)sizeof(line), msg_data[0], msg_data);
                pos += snprintf(line + pos, sizeof(line) - pos, "\r\n");
                USB_SendString(line);
            }
        }
    }
    /* Command: "T100CFG" - Read T100 object and print UNKNOWN[9]/UNKNOWN[20] */
    else if (strcmp(cmd_str, "T100CFG") == 0 || strcmp(cmd_str, "t100cfg") == 0) {
        if (!g_touch_inited) MXT_InitTouchScreen();
        MXT_ReadT100UnknownFields();
    }
    /* Command: "STATUS" - Query current status */
    else if (strcmp(cmd_str, "STATUS") == 0 || strcmp(cmd_str, "status") == 0) {
        char status_str[256];
        snprintf(status_str, sizeof(status_str),
                 "Mode: %d, Diag Output: %s, Msg Output: %s\r\n"
                 "Config: X=%d, Y=%d, DiagMode: 0x%02X\r\n",
                 g_bridge_mode,
                 MXT_IsOutputEnabled() ? "ENABLED" : "DISABLED",
                 g_msg_output_enabled ? "ENABLED" : "DISABLED",
                 g_matrix_x_size,
                 g_matrix_y_size,
                 g_diag_mode);
        USB_SendString(status_str);
    }
    /* Command: "SPI" - toggle SPI debug stream check */
    else if (strcmp(cmd_str, "SPI") == 0 || strcmp(cmd_str, "spi") == 0) {
        g_spi_stream_enabled = (uint8_t)!g_spi_stream_enabled;
        if (g_spi_stream_enabled) {
            SPIUSB_ResetState(0U);
            MXT_SPI_StartIT();
            USB_SendString("INFO: SPI stream START (raw hex)\r\n");
        } else {
            SPIUSB_TryFlush();
            USB_SendString("INFO: SPI stream STOP\r\n");
        }
    }
    /* Command: "HELP" - Show available commands */
    else if (strcmp(cmd_str, "HELP") == 0 || strcmp(cmd_str, "help") == 0) {
        USB_SendString("Available commands:\r\n");
        USB_SendString("  [Mode]\r\n");
        USB_SendString("  mode0 = binary bridge (CFGWRITE/CFGREAD/hex frames)\r\n");
        USB_SendString("  mode1 = string command mode (help/status/start...)\r\n");
        USB_SendString("  Switch to mode1 from mode0:\r\n");
        USB_SendString("    1) Send text command: MODE1 (when text path is available)\r\n");
        USB_SendString("    2) Send fixed 4-byte sequence: 02 01 10 20\r\n");
        USB_SendString("  Use STATUS to check current mode\r\n");
        USB_SendString("  u        - Enter diagnostic dump menu\r\n");
        USB_SendString("  MODE0    - Switch to I2C-USB bridge mode\r\n");
        USB_SendString("  MODE1    - Switch to string mode\r\n");
        USB_SendString("  START    - Start diagnostic output (FRAME0, 1000ms timer)\r\n");
        USB_SendString("  START1   - Start diagnostic output (FRAME1 + pre-CAL, 1000ms timer)\r\n");
        USB_SendString("  STOP     - Stop diagnostic output\r\n");
        USB_SendString("  FRAME0   - Mutual Delta (0x10)\r\n");
        USB_SendString("  FRAME1   - Mutual Reference (0x11)\r\n");
        USB_SendString("  FRAME3   - Self Delta (0xF7)\r\n");
        USB_SendString("  FRAME4   - Self Reference (0xF8)\r\n");
        USB_SendString("  FRAME5   - Key Array / Self Signal (0xF5)\r\n");
        USB_SendString("  FRAME38  - Self DC Level (0x38)\r\n");
        USB_SendString("  MSG_ON   - Enable CHG message output\r\n");
        USB_SendString("  MSG_OFF  - Disable CHG message output\r\n");
        USB_SendString("  CHGON    - Enable CHG processing (read messages on CHG)\r\n");
        USB_SendString("  CHGOFF   - Disable CHG processing (default)\r\n");
        USB_SendString("  MAPALL   - Read one frame, output full matrix\r\n");
        USB_SendString("  MAP16    - Read one frame, output 16x16 (index 0-15)\r\n");
        USB_SendString("  MAP16R90 - 16x16 clockwise 90\r\n");
        USB_SendString("  MAP16L90 - 16x16 counterclockwise 90\r\n");
        USB_SendString("  MAP16X   - 16x16 flip X\r\n");
        USB_SendString("  MAP16Y   - 16x16 flip Y\r\n");
        USB_SendString("  MAP16XY  - 16x16 flip X and Y\r\n");
        USB_SendString("  START CHGNO [X|Y|XY] ms - Mode3 stream with [touch_id,x,y,action] from CHG\r\n");
        USB_SendString("  INFO     - Read Info Block (0x0000, 7 bytes)\r\n");
        USB_SendString("  OBJTBL   - Read object table (0x0007, first 10 objects)\r\n");
        USB_SendString("  MSGCNT   - Read message count (T44 from object table)\r\n");
        USB_SendString("  MSG      - Read one message (T5 from object table)\r\n");
        USB_SendString("  T100CFG  - Read T100 UNKNOWN[9]/UNKNOWN[20]\r\n");
        USB_SendString("  STATUS   - Query current status\r\n");
        USB_SendString("  SPISTART  - SPI raw bytes as hex text\r\n");
        USB_SendString("  SPISTART1 - SPI crop 20x16 to 16x16 text rows\r\n");
        USB_SendString("  SPISTART3 - SPI crop 20x16 to 16x16 packets\r\n");
        USB_SendString("  SPISTOP   - Stop SPI stream\r\n");
        USB_SendString("  SPI       - Toggle SPISTART raw stream\r\n");
        USB_SendString("  EXPORTTXT - Export compact config summary text\r\n");
        USB_SendString("  EXPORTBIN - Export config as compact binary stream\r\n");
        USB_SendString("  HELP     - Show this help message\r\n");
    }
    /* Unknown command */
    else {
        USB_SendString("ERROR: Unknown command. Type HELP for available commands\r\n");
    }
}

static void MXT_ExportConfigAsTxt(void)
{
    if (!g_touch_inited) {
        MXT_InitTouchScreen();
    }
    if (!g_touch_inited) {
        USB_SendString("ERR: Touch not initialized\r\n");
        return;
    }

    uint8_t id_info[7] = {0};
    if (MXT_I2C_Read(g_mxt_i2c_addr, 0x0000, id_info, 7) != 0) {
        USB_SendString("ERR: Read Info Block failed\r\n");
        return;
    }

    uint8_t n_obj = id_info[6];
    if (n_obj == 0) {
        USB_SendString("ERR: Object count is 0\r\n");
        return;
    }
    if (n_obj > 96) n_obj = 96;

    char line[128];
    USB_SendString("=== EXPORTTXT COMPACT START ===\r\n");
    snprintf(line, sizeof(line), "ID family=%u variant=%u ver=%u build=%u objs=%u\r\n",
             id_info[0], id_info[1], id_info[2], id_info[3], n_obj);
    USB_SendString(line);

    uint8_t obj_entry[6];
    uint16_t obj_table_start = 0x0007;

    for (uint8_t i = 0; i < n_obj; i++) {
        uint16_t entry_addr = (uint16_t)(obj_table_start + i * 6);
        if (MXT_I2C_Read(g_mxt_i2c_addr, entry_addr, obj_entry, 6) != 0) {
            snprintf(line, sizeof(line), "ERR: Read object table index=%u failed\r\n", i);
            USB_SendString(line);
            break;
        }

        uint8_t obj_type = obj_entry[0];
        uint16_t obj_addr = (uint16_t)(obj_entry[1] | (obj_entry[2] << 8));
        uint16_t obj_size = (uint16_t)(obj_entry[3] + 1);
        uint8_t instances = (uint8_t)(obj_entry[4] + 1);
        uint8_t report_ids = obj_entry[5];

        snprintf(line, sizeof(line),
                 "T%u addr=0x%04X size=%u inst=%u rid=%u\r\n",
                 obj_type, obj_addr, obj_size, instances, report_ids);
        USB_SendString(line);

        /* 导出期间主动冲刷，减小环形缓冲积压 */
        MXT_FlushMessageBuffer();
    }

    USB_SendString("=== EXPORTTXT COMPACT END ===\r\n");
}

static void MXT_ExportConfigAsBin(void)
{
    if (!g_touch_inited) {
        MXT_InitTouchScreen();
    }
    if (!g_touch_inited) {
        USB_SendString("ERR: Touch not initialized\r\n");
        return;
    }

    uint8_t id_info[7] = {0};
    if (MXT_I2C_Read(g_mxt_i2c_addr, 0x0000, id_info, 7) != 0) {
        USB_SendString("ERR: Read Info Block failed\r\n");
        return;
    }

    uint8_t n_obj = id_info[6];
    if (n_obj == 0) {
        USB_SendString("ERR: Object count is 0\r\n");
        return;
    }
    if (n_obj > 96) n_obj = 96;

    /* 二进制导出帧格式：
     * START: [A0][family][variant][version][build][n_obj]
     * DATA : [A1][obj_type][inst][addr_l][addr_h][off_l][off_h][chunk_len][payload...]
     * END  : [A2][status]
     */
    uint8_t frame[64];
    frame[0] = 0xA0;
    frame[1] = id_info[0];
    frame[2] = id_info[1];
    frame[3] = id_info[2];
    frame[4] = id_info[3];
    frame[5] = n_obj;
    SendResponse(frame, 6);

    uint8_t obj_entry[6];
    uint16_t obj_table_start = 0x0007;
    uint8_t status = 0;

    for (uint8_t i = 0; i < n_obj; i++) {
        uint16_t entry_addr = (uint16_t)(obj_table_start + i * 6);
        if (MXT_I2C_Read(g_mxt_i2c_addr, entry_addr, obj_entry, 6) != 0) {
            status = 1;
            break;
        }

        uint8_t obj_type = obj_entry[0];
        uint16_t obj_addr = (uint16_t)(obj_entry[1] | (obj_entry[2] << 8));
        uint16_t obj_size = (uint16_t)(obj_entry[3] + 1);
        uint8_t instances = (uint8_t)(obj_entry[4] + 1);

        for (uint8_t inst = 0; inst < instances; inst++) {
            uint16_t inst_addr = (uint16_t)(obj_addr + (uint16_t)inst * obj_size);
            uint16_t offset = 0;
            while (offset < obj_size) {
                uint8_t chunk = (uint8_t)(obj_size - offset);
                if (chunk > 56) chunk = 56; /* 64B USB 包减去8B头 */

                if (MXT_I2C_Read(g_mxt_i2c_addr, (uint16_t)(inst_addr + offset), &frame[8], chunk) != 0) {
                    status = 2;
                    break;
                }

                frame[0] = 0xA1;
                frame[1] = obj_type;
                frame[2] = inst;
                frame[3] = (uint8_t)(inst_addr & 0xFF);
                frame[4] = (uint8_t)((inst_addr >> 8) & 0xFF);
                frame[5] = (uint8_t)(offset & 0xFF);
                frame[6] = (uint8_t)((offset >> 8) & 0xFF);
                frame[7] = chunk;
                SendResponse(frame, (uint16_t)(8 + chunk));
                offset = (uint16_t)(offset + chunk);
            }
            if (status != 0) break;
        }
        if (status != 0) break;
    }

    frame[0] = 0xA2;
    frame[1] = status;
    SendResponse(frame, 2);
}

/* ======================= CFG 协议辅助函数 ======================= */
static void CFG_SendResp(uint8_t resp_cmd, uint16_t seq, uint8_t status)
{
  uint8_t out[6];
  out[0] = resp_cmd;
  out[1] = (uint8_t)(seq & 0xFF);
  out[2] = (uint8_t)((seq >> 8) & 0xFF);
  out[3] = status;
  uint16_t crc = Map16_CalcCRC16(out, 4);
  out[4] = (uint8_t)(crc & 0xFF);
  out[5] = (uint8_t)((crc >> 8) & 0xFF);
  SendResponse(out, 6);
}

static uint8_t CFG_CheckCRC16LE(const uint8_t *frame, uint16_t frame_len)
{
  if (!frame || frame_len < 2) return 0;
  uint16_t crc_rx = frame[frame_len - 2] | ((uint16_t)frame[frame_len - 1] << 8);
  uint16_t crc_calc = Map16_CalcCRC16(frame, frame_len - 2);
  return (crc_rx == crc_calc) ? 1 : 0;
}

/**
  * @brief  处理符合 mxt-app usb_device.c 协议的数据包
  */
static void ProcessBridgePacket(uint8_t *buf, uint32_t len)
{
    static uint8_t resp_buf[APP_TX_DATA_SIZE];
    
    if (len < 1) return;

    /* mode0<->mode1：固定 4 字节序列 02 01 10 20
     *
     * 注意：USB CDC 可能把 4 字节拆成多次回调（len!=4）。
     * 旧逻辑要求 len==4 才能识别，可能导致切换失败，从而 help 在 mode0(binary) 下也看不到。
     * 这里改为“按字节连续匹配”，提高切换成功率。
     */
    static uint8_t mode_switch_state = 0;
    static const uint8_t mode_switch_seq[4] = {0x02, 0x01, 0x10, 0x20};
    uint8_t mode_switched = 0;
    if (g_cfg_rx_len == 0 && !g_cfgwrite_active && !g_cfgread_waiting_ack) {
        for (uint32_t i = 0; i < len; i++) {
            uint8_t b = buf[i];
            if (b == mode_switch_seq[mode_switch_state]) {
                mode_switch_state++;
            } else {
                mode_switch_state = (b == mode_switch_seq[0]) ? 1 : 0;
            }

            if (mode_switch_state >= 4) {
                mode_switch_state = 0;
                mode_switched = 1;

                if (g_bridge_mode == BRIDGE_MODE_BINARY) {
                    g_bridge_mode = BRIDGE_MODE_STRING;
                    g_menu_state = MENU_IDLE;
                    MXT_InitTouchScreen();

                    /* 切换后发送十六进制 02 01 10 20 */
                    uint8_t switch_hex[] = {0x02, 0x01, 0x10, 0x20};
                    SendResponse(switch_hex, 4);
                } else {
                    g_bridge_mode = BRIDGE_MODE_BINARY;
                    g_menu_state = MENU_IDLE;
                }
                break; /* 消耗序列，避免继续按本包其它协议解析 */
            }
        }
    }
    if (mode_switched) return;

    /* 默认字符串模式：仅当当前为桥模式且数据明显为二进制协议时才走二进制，否则优先按文本处理 */
    uint8_t as_binary = 0;
    if (g_bridge_mode == BRIDGE_MODE_BINARY) {
        /* mode0 下强制按二进制协议处理，不再识别字符串指令 (如 help, mode1 等)
         * 除非是前面已处理的特殊切换序列 02 01 10 20
         */
        as_binary = 1;
    } else {
        /* 字符串模式：仅明确多字节二进制包才走二进制（如 mxt-app 发来的 01 51...），单字节 0x82/0xE0 当文本 */
        if (buf[0] == REPORT_ID && len >= 2 && buf[1] == IIC_DATA_1) as_binary = 1;
        else if (buf[0] == IIC_DATA_1 && len >= 3) as_binary = 1;
        else if (buf[0] == CMD_CONFIG && len >= 3) as_binary = 1;
        /* CMD_READ_PINS(0x82)/CMD_FIND_IIC_ADDRESS(0xE0) 在字符串模式下不按二进制处理 */
    }

    if (!as_binary) {
        /* 仅在 mode1(BRIDGE_MODE_STRING) 下才会进入此分支 */
        uint8_t is_text = 1;
        for (uint32_t i = 0; i < len && i < 10; i++) {
            if (buf[i] < 0x20 && buf[i] != '\r' && buf[i] != '\n' && buf[i] != '\t') {
                is_text = 0;
                break;
            }
        }
        if (is_text) {
            ProcessStringCommand(buf, len);
            return;
        }
    }
    
    /* CFG 帧是流式传输（USB CDC 会拆包），这里先做重组再进入原有校验流程 */
    if (as_binary && len > 0) {
        uint8_t maybe_cfg = (buf[0] == CFGWRITE_START_CMD || buf[0] == CFGWRITE_CHUNK_CMD || buf[0] == CFGWRITE_END_CMD ||
                             buf[0] == CFG_RESP_ACK_CMD || buf[0] == CFG_RESP_NACK_CMD ||
                             buf[0] == FREEZE_COMMAND || buf[0] == UNFREEZE_COMMAND || buf[0] == BACKUPNV_COMMAND);
        if (g_cfg_rx_len > 0 || maybe_cfg) {
            uint32_t copy_len = len;
            uint32_t cap_left = (uint32_t)sizeof(g_cfg_rx_buf) - g_cfg_rx_len;
            if (copy_len > cap_left) {
                /* 重组溢出时丢弃本次缓存，等待下一帧重新同步 */
                g_cfg_rx_len = 0;
                return;
            }
            memcpy(&g_cfg_rx_buf[g_cfg_rx_len], buf, copy_len);
            g_cfg_rx_len = (uint16_t)(g_cfg_rx_len + copy_len);

            if (g_cfg_rx_len < 1) return;

            uint16_t expected_len = 0;
            uint8_t stream_cmd = g_cfg_rx_buf[0];
            if (stream_cmd == CFGWRITE_START_CMD) {
                if (g_cfg_rx_len < 12) return;
                uint16_t total_objects = g_cfg_rx_buf[2] | ((uint16_t)g_cfg_rx_buf[3] << 8);
                if (total_objects == 0 || total_objects > CFG_MAX_OBJECTS) {
                    g_cfg_rx_len = 0;
                    CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
                    return;
                }
                expected_len = (uint16_t)(12 + 4 * total_objects);
            } else if (stream_cmd == CFGWRITE_CHUNK_CMD) {
                if (g_cfg_rx_len < 11) return;
                uint16_t chunk_len = g_cfg_rx_buf[7] | ((uint16_t)g_cfg_rx_buf[8] << 8);
                if (chunk_len == 0 || chunk_len > CFG_MAX_DATA_PER_FRAME) {
                    g_cfg_rx_len = 0;
                    CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
                    return;
                }
                expected_len = (uint16_t)(11 + chunk_len);
            } else if (stream_cmd == CFGWRITE_END_CMD) {
                expected_len = 7;
            } else if (stream_cmd == CFG_RESP_ACK_CMD || stream_cmd == CFG_RESP_NACK_CMD) {
                expected_len = 6;
            } else if (stream_cmd == FREEZE_COMMAND || stream_cmd == UNFREEZE_COMMAND || stream_cmd == BACKUPNV_COMMAND) {
                expected_len = 3;
            } else {
                /* 非 CFG 帧，清空重组缓冲，走原始分支 */
                g_cfg_rx_len = 0;
            }

            if (expected_len > 0) {
                if (g_cfg_rx_len < expected_len) return;
                buf = g_cfg_rx_buf;
                len = expected_len;
                if (g_cfg_rx_len > expected_len) {
                    uint16_t remain = (uint16_t)(g_cfg_rx_len - expected_len);
                    memmove(g_cfg_rx_buf, &g_cfg_rx_buf[expected_len], remain);
                    g_cfg_rx_len = remain;
                } else {
                    g_cfg_rx_len = 0;
                }
            }
        }
    }

    uint8_t cmd = buf[0];

    /* ==============================================================
     * CFGREAD host ACK/NACK: 只在 MCU 正在等待读回确认时处理
     * ==============================================================
     */
    if (g_cfgread_waiting_ack && (cmd == CFG_RESP_ACK_CMD || cmd == CFG_RESP_NACK_CMD)) {
        if (len == 6 && CFG_CheckCRC16LE(buf, 6)) {
            uint16_t seq = buf[1] | ((uint16_t)buf[2] << 8);
            uint8_t status = buf[3];
            if (seq == g_cfgread_current_seq) {
                g_cfgread_last_ack_status = status;
                g_cfgread_waiting_ack = 0;
            }
        }
        return;
    }

    /* ==============================================================
     * CFGWRITE/CFGREAD: host -> MCU 配置写入协议
     * ==============================================================
     */
    if (cmd == CFGWRITE_START_CMD) {
        /* 最小: D0 + ver(1) + total_objects(2) + total_chunks(2) + total_bytes(4) + crc(2) = 12 */
        if (len < 12) {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
            return;
        }
        if (buf[1] != CFG_PROTOCOL_VERSION) {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
            return;
        }

        uint16_t total_objects = buf[2] | ((uint16_t)buf[3] << 8);
        uint16_t total_chunks  = buf[4] | ((uint16_t)buf[5] << 8);
        /* total_bytes 当前仅用于协议占位校验，暂不参与后续处理 */
        (void)(((uint32_t)buf[6]) | ((uint32_t)buf[7] << 8) | ((uint32_t)buf[8] << 16) | ((uint32_t)buf[9] << 24));

        if (total_objects == 0 || total_objects > CFG_MAX_OBJECTS || total_chunks == 0) {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
            return;
        }

        uint16_t expected_len = (uint16_t)(12 + 4 * total_objects);
        if (len != expected_len) {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
            return;
        }

        if (!CFG_CheckCRC16LE(buf, expected_len)) {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
            return;
        }

        /* 收到 START 前强制刷新 I2C 地址与对象表，避免默认地址不匹配导致 start 失败 */
        uint8_t found_addr = MXT_FindI2CAddress();
        if (found_addr == STATUS_NO_DEVICE) {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_NO_DEVICE);
            return;
        }
        g_touch_inited = 0;
        MXT_InitTouchScreen();
        if (g_t6_addr == 0) {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_NO_DEVICE);
            return;
        }

        /* 保存对象元数据（写入后用于读回导出） */
        for (uint16_t i = 0; i < total_objects; i++) {
            uint16_t p = (uint16_t)(10 + i * 4);
            uint16_t addr = buf[p] | ((uint16_t)buf[p + 1] << 8);
            uint16_t size = buf[p + 2] | ((uint16_t)buf[p + 3] << 8);
            g_cfgwrite_objects[i].addr = addr;
            g_cfgwrite_objects[i].size = size;
        }

        /* 冻结配置写入（等价 mxt-app FREEZE_COMMAND=0x22） */
        uint8_t freeze_cmd = FREEZE_COMMAND;
        uint8_t r = MXT_I2C_Write(g_mxt_i2c_addr, g_t6_addr + 1, &freeze_cmd, 1);
        if (r != 0) {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, r);
            return;
        }

        g_cfgwrite_active = 1;
        g_cfgwrite_total_objects = total_objects;
        g_cfgwrite_total_chunks = total_chunks;
        g_cfgwrite_next_seq = 1;

        g_cfgread_waiting_ack = 0;
        g_cfgread_current_seq = 0;

        CFG_SendResp(CFG_RESP_ACK_CMD, 0, STATUS_OK);
        return;
    }

    if (cmd == CFGWRITE_CHUNK_CMD) {
        if (!g_cfgwrite_active) {
            uint16_t seq = (len >= 3) ? (buf[1] | ((uint16_t)buf[2] << 8)) : 0;
            CFG_SendResp(CFG_RESP_NACK_CMD, seq, STATUS_ADDR_NACK);
            return;
        }
        if (len < 11) {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
            return;
        }

        uint16_t seq = buf[1] | ((uint16_t)buf[2] << 8);
        uint16_t obj_index = buf[3] | ((uint16_t)buf[4] << 8);
        uint16_t offset = buf[5] | ((uint16_t)buf[6] << 8);
        uint16_t chunk_len = buf[7] | ((uint16_t)buf[8] << 8);

        uint16_t expected_len = (uint16_t)(11 + chunk_len);
        if (chunk_len == 0 || expected_len != len || chunk_len > CFG_MAX_DATA_PER_FRAME) {
            CFG_SendResp(CFG_RESP_NACK_CMD, seq, STATUS_ADDR_NACK);
            return;
        }

        if (!CFG_CheckCRC16LE(buf, expected_len)) {
            CFG_SendResp(CFG_RESP_NACK_CMD, seq, STATUS_ADDR_NACK);
            return;
        }

        if (seq != g_cfgwrite_next_seq) {
            CFG_SendResp(CFG_RESP_NACK_CMD, seq, STATUS_ADDR_NACK);
            return;
        }
        if (obj_index >= g_cfgwrite_total_objects) {
            CFG_SendResp(CFG_RESP_NACK_CMD, seq, STATUS_ADDR_NACK);
            return;
        }
        if (offset + chunk_len > g_cfgwrite_objects[obj_index].size) {
            CFG_SendResp(CFG_RESP_NACK_CMD, seq, STATUS_ADDR_NACK);
            return;
        }

        uint16_t write_addr = (uint16_t)(g_cfgwrite_objects[obj_index].addr + offset);
        uint8_t i2c_res = MXT_I2C_Write(g_mxt_i2c_addr, write_addr, &buf[9], chunk_len);
        if (i2c_res == 0) {
            g_cfgwrite_next_seq++;
            /* 每个对象最后一包返回 STATUS_OBJ_DONE，供主机显示“对象写入完成” */
            if ((uint16_t)(offset + chunk_len) == g_cfgwrite_objects[obj_index].size) {
                CFG_SendResp(CFG_RESP_ACK_CMD, seq, STATUS_OBJ_DONE);
            } else {
                CFG_SendResp(CFG_RESP_ACK_CMD, seq, STATUS_OK);
            }
        } else {
            CFG_SendResp(CFG_RESP_NACK_CMD, seq, i2c_res);
        }
        return;
    }

    if (cmd == CFGWRITE_END_CMD) {
        /* 帧格式: D2 | end_seq(u16) | reserved(u16) | crc16  => 固定 7 字节 */
        if (len != 7) {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
            return;
        }
        if (!g_cfgwrite_active) {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
            return;
        }
        if (!CFG_CheckCRC16LE(buf, 7)) {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
            return;
        }

        uint16_t end_seq = buf[1] | ((uint16_t)buf[2] << 8);
        /* uint16_t reserved = buf[3] | (buf[4] << 8); */
        if (end_seq != (uint16_t)(g_cfgwrite_total_chunks + 1)) {
            CFG_SendResp(CFG_RESP_NACK_CMD, end_seq, STATUS_ADDR_NACK);
            return;
        }

        /* END: 仅确认写入完成，不做完整读回（避免占用链路并阻塞控制反馈） */
        CFG_SendResp(CFG_RESP_ACK_CMD, end_seq, STATUS_OK);
        g_cfgwrite_active = 0;
        g_cfgread_waiting_ack = 0;
        g_cfg_rx_len = 0;
        return;
    }

    /* ==============================================================
     * 控制命令反馈：FREEZE/UNFREEZE/BACKUPNV
     * 帧格式: [cmd][crc16]，固定 3 字节
     * 返回: D3/D4(seq=0, status)
     * ==============================================================
     */
    if ((cmd == FREEZE_COMMAND || cmd == UNFREEZE_COMMAND || cmd == BACKUPNV_COMMAND) && len >= 3) {
        if (!CFG_CheckCRC16LE(buf, 3)) {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_ADDR_NACK);
            return;
        }

        /* 确保地址和对象表有效，避免 T6 地址失效 */
        if (g_t6_addr == 0 || !g_touch_inited) {
            uint8_t found_addr = MXT_FindI2CAddress();
            if (found_addr == STATUS_NO_DEVICE) {
                CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_NO_DEVICE);
                return;
            }
            g_touch_inited = 0;
            MXT_InitTouchScreen();
            if (g_t6_addr == 0) {
                CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_NO_DEVICE);
                return;
            }
        }

        /* 如果正在执行 BACKUPNV（NVM 写入窗口），UNFREEZE 先延后到主循环处理
         * 避免在 USB 接收回调里做阻塞等待，也避免 NVM 还没写完就解除冻结导致异常状态。 */
        if (cmd == UNFREEZE_COMMAND && g_backup_busy && (HAL_GetTick() < g_backup_busy_until_ms)) {
            g_unfreeze_pending = 1;
            return; /* 不直接 ACK，由主循环到点后再写并 ACK */
        }

        uint8_t ctrl = cmd;
        uint8_t r = MXT_I2C_Write(g_mxt_i2c_addr, g_t6_addr + 1, &ctrl, 1);
        if (r == 0) {
            if (cmd == BACKUPNV_COMMAND) {
                /* BACKUPNV 触发后 NVM 写入可能需要时间 */
                g_backup_busy = 1;
                g_backup_busy_until_ms = HAL_GetTick() + 2000;
            }
            if (cmd == UNFREEZE_COMMAND) {
                /* UNFREEZE 后强制回字符串模式，确保 HELP/T100CFG 可直接响应 */
                g_bridge_mode = BRIDGE_MODE_STRING;
                g_menu_state = MENU_IDLE;
                g_cfg_rx_len = 0;
                g_backup_busy = 0;
                g_unfreeze_pending = 0;
            }
            CFG_SendResp(CFG_RESP_ACK_CMD, 0, STATUS_OK);
        } else {
            CFG_SendResp(CFG_RESP_NACK_CMD, 0, r);
        }
        return;
    }
    
    /*=========================================================================
     * 1. 带 REPORT_ID 的 I2C 读写命令: [01] [51] ...
     *    mxt-app 对非 bridge_chip 设备使用此格式
     *=========================================================================*/
    if (cmd == REPORT_ID && len >= 2 && buf[1] == IIC_DATA_1)
    {
        if (len >= 6) {
            uint16_t reg_addr = buf[4] | (buf[5] << 8);
            
            if (buf[2] == 2) {
                /* Read: [01] [51] [02] [count] [addr_l] [addr_h] */
                uint16_t count = buf[3];
                if (count > (APP_TX_DATA_SIZE - 3)) count = APP_TX_DATA_SIZE - 3;
                
                uint8_t i2c_res = MXT_I2C_Read(g_mxt_i2c_addr, reg_addr, &resp_buf[3], count);
                
                resp_buf[0] = REPORT_ID;
                resp_buf[1] = i2c_res ? STATUS_ADDR_NACK : STATUS_OK;
                resp_buf[2] = 0x00;
                
                SendResponse(resp_buf, count + 3);
            }
            else {
                /* Write: [01] [51] [2+count] [00] [addr_l] [addr_h] [data...] */
                uint16_t count = buf[2] - 2;
                if (count > (len - 6)) count = len - 6;
                
                uint8_t i2c_res = MXT_I2C_Write(g_mxt_i2c_addr, reg_addr, &buf[6], count);
                
                resp_buf[0] = REPORT_ID;
                resp_buf[1] = i2c_res ? STATUS_ADDR_NACK : STATUS_WRITE_OK;
                
                SendResponse(resp_buf, 2);
            }
        }
    }
    /*=========================================================================
     * 2. 不带 REPORT_ID 的 I2C 读写命令: [51] ...
     *    mxt-app 对 bridge_chip 设备使用此格式（也用于 Bootloader 模式）
     *=========================================================================*/
    else if (cmd == IIC_DATA_1 && len >= 3)
    {
        uint8_t write_len = buf[1];   /* 写入字节数 */
        uint16_t read_len = buf[2];   /* 读取字节数 */
        
        if (write_len == 2 && read_len > 0 && len >= 5) {
            /* Read with register: [51] [02] [count] [addr_l] [addr_h] */
            uint16_t reg_addr = buf[3] | (buf[4] << 8);
            if (read_len > (APP_TX_DATA_SIZE - 2)) read_len = (APP_TX_DATA_SIZE - 2);
            
            uint8_t i2c_res = MXT_I2C_Read(g_mxt_i2c_addr, reg_addr, &resp_buf[2], read_len);
            
            resp_buf[0] = i2c_res ? STATUS_ADDR_NACK : STATUS_OK;
            resp_buf[1] = 0x00;
            
            SendResponse(resp_buf, read_len + 2);
        }
        else if (write_len == 0 && read_len > 0) {
            /* Bootloader Read (no register): [51] [00] [count] */
            if (read_len > (APP_TX_DATA_SIZE - 2)) read_len = (APP_TX_DATA_SIZE - 2);
            
            uint8_t i2c_res = MXT_I2C_ReadNoReg(g_mxt_i2c_addr, &resp_buf[2], read_len);
            
            resp_buf[0] = i2c_res ? STATUS_ADDR_NACK : STATUS_OK;
            resp_buf[1] = 0x00;
            
            SendResponse(resp_buf, read_len + 2);
        }
        else if (write_len > 2 && read_len == 0 && len >= (3 + write_len)) {
            /* Write with register: [51] [2+count] [00] [addr_l] [addr_h] [data...] */
            uint16_t reg_addr = buf[3] | (buf[4] << 8);
            uint16_t count = write_len - 2;
            
            uint8_t i2c_res = MXT_I2C_Write(g_mxt_i2c_addr, reg_addr, &buf[5], count);
            
            resp_buf[0] = i2c_res ? STATUS_ADDR_NACK : STATUS_WRITE_OK;
            
            SendResponse(resp_buf, 1);
        }
        else if (write_len > 0 && read_len == 0 && len >= (3 + write_len)) {
            /* Bootloader Write (no register): [51] [count] [00] [data...] */
            uint8_t i2c_res = MXT_I2C_WriteNoReg(g_mxt_i2c_addr, &buf[3], write_len);
            
            resp_buf[0] = i2c_res ? STATUS_ADDR_NACK : STATUS_WRITE_OK;
            
            SendResponse(resp_buf, 1);
        }
    }
    /*=========================================================================
     * 3. 读取引脚状态: [82] (CMD_READ_PINS)
     *=========================================================================*/
    else if (cmd == CMD_READ_PINS)
    {
        resp_buf[0] = CMD_READ_PINS;
        resp_buf[1] = STATUS_OK;
        /* CHG 引脚状态在 bit 2 (0x04)，根据 mxt-app: chg = pkt[2] & 0x4 */
        resp_buf[2] = (HAL_GPIO_ReadPin(CHG_EXTI3_GPIO_Port, CHG_EXTI3_Pin) == GPIO_PIN_RESET) ? 0x00 : 0x04;
        
        SendResponse(resp_buf, 3);
    }
    /*=========================================================================
     * 4. 查找 I2C 地址: [E0] (CMD_FIND_IIC_ADDRESS)
     *=========================================================================*/
    else if (cmd == CMD_FIND_IIC_ADDRESS)
    {
        uint8_t found_addr = MXT_FindI2CAddress();
        
        resp_buf[0] = CMD_FIND_IIC_ADDRESS;
        resp_buf[1] = found_addr;  /* 找到的地址，或 0x81 表示未找到 */
        
        SendResponse(resp_buf, 2);
    }
    /*=========================================================================
     * 5. 配置命令: [80] (CMD_CONFIG)
     *    格式: [80] [speed] [i2c_addr|flags] [...]
     *=========================================================================*/
    else if (cmd == CMD_CONFIG && len >= 3)
    {
        /* 解析 I2C 地址配置（如果指定） */
        uint8_t addr_config = buf[2] & 0x7F;  /* 低 7 位是地址 */
        if (addr_config >= 0x24 && addr_config <= 0x4B) {
            g_mxt_i2c_addr = addr_config;
        }
        
        /* 检查是否设置了模式位 (最高位) */
        uint8_t mode_flag = buf[2] & 0x80;  /* 最高位作为模式标志 */
        if (mode_flag) {
            g_bridge_mode = BRIDGE_MODE_STRING;
            MXT_InitTouchScreen();
            USB_SendString("Bridge mode switched to STRING mode\r\n");
        } else {
            g_bridge_mode = BRIDGE_MODE_BINARY;
            USB_SendString("Bridge mode switched to I2C-USB mode\r\n");
        }
        
        /* 返回配置成功 */
        resp_buf[0] = CMD_CONFIG;
        resp_buf[1] = STATUS_OK;
        
        SendResponse(resp_buf, 2);
    }
    /*=========================================================================
     * 6. 模式切换命令: [FF] (自定义命令)
     *    格式: [FF] [mode] 
     *    mode: 0=桥模式, 1=字符串模式
     *=========================================================================*/
    else if (cmd == 0xFF && len >= 2)
    {
        uint8_t new_mode = buf[1];
        if(new_mode == 0) {
            g_bridge_mode = BRIDGE_MODE_BINARY;
            USB_SendString("Bridge Mode: I2C-USB bridge\r\n");
        } else if (new_mode == 1) {
            g_bridge_mode = BRIDGE_MODE_STRING;
            // 初始化触摸屏配置
            MXT_InitTouchScreen();
            USB_SendString("Bridge Mode: String mode\r\n");
            char config_str[64];
            snprintf(config_str, sizeof(config_str), "Config: X=%d, Y=%d\r\n", g_matrix_x_size, g_matrix_y_size);
            USB_SendString(config_str);
        }
        
        /* 返回模式切换成功 */
        resp_buf[0] = 0xFF;
        resp_buf[1] = g_bridge_mode;
        
        SendResponse(resp_buf, 2);
    }
    /*=========================================================================
     * 7. 输出控制命令: [FE] (自定义命令)
     *    格式: [FE] [control] 
     *    control: 0=停止输出, 1=启动输出
     *=========================================================================*/
    else if (cmd == 0xFE && len >= 2)
    {
        uint8_t control = buf[1];
        if (control == 0) {
            MXT_EnableOutput(0);  /* 停止输出 */
            USB_SendString("Output: STOPPED\r\n");
        } else if (control == 1) {
            MXT_EnableOutput(1);  /* 启动输出 */
            USB_SendString("Output: STARTED\r\n");
        }
            
        /* 返回输出控制成功 */
        resp_buf[0] = 0xFE;
        resp_buf[1] = MXT_IsOutputEnabled();
            
        SendResponse(resp_buf, 2);
    }
}

/**
  * @brief  获取当前 I2C 地址
  */
uint8_t MXT_GetI2CAddress(void)
{
    return g_mxt_i2c_addr;
}

/**
  * @brief  设置 I2C 地址
  */
void MXT_SetI2CAddress(uint8_t addr)
{
    g_mxt_i2c_addr = addr;
}

/**
  * @brief  获取对象表中的对象地址
  * @param  obj_type: 对象类型 (如 5=T5, 6=T6, 44=T44)
  * @retval 对象地址，0 表示未找到
  */
uint16_t MXT_GetObjectAddr(uint8_t obj_type)
{
    /* 如果地址为0，尝试重新读取对象表 */
    uint16_t addr = 0;
    switch (obj_type) {
        case 5:  addr = g_t5_addr; break;
        case 6:  addr = g_t6_addr; break;
        case 44: addr = g_t44_addr; break;
        case 37: addr = g_t37_addr; break;
        case 100: addr = g_t100_addr; break;
        default: addr = 0; break;
    }
    
    /* 如果地址为0且对象表已读取过，尝试重新读取对象表 */
    if (addr == 0 && g_num_objects > 0) {
        MXT_ReadObjectTable();
        switch (obj_type) {
            case 5:  addr = g_t5_addr; break;
            case 6:  addr = g_t6_addr; break;
            case 44: addr = g_t44_addr; break;
            case 37: addr = g_t37_addr; break;
            case 100: addr = g_t100_addr; break;
            default: addr = 0; break;
        }
    }
    
    return addr;
}

/**
  * @brief  检查对象表是否已初始化
  * @retval 1=已初始化, 0=未初始化
  */
uint8_t MXT_ObjectTableReady(void)
{
    return (g_t44_addr != 0 && g_t5_addr != 0) ? 1 : 0;
}

/**
  * @brief  Send string via USB CDC (使用缓冲区避免阻塞)
  */
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

/**
  * @brief  Send formatted string via USB CDC
  *         模式1下写入消息缓冲区，避免 CDC 忙时返回 USBD_BUSY 导致丢失
  */
uint8_t USB_Printf(const char *format, ...)
{
  char buffer[APP_TX_DATA_SIZE];
  va_list args;
  int len;

  if(hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) return USBD_FAIL;

  /* mode0(桥接/二进制) 下禁止输出任何字符串，避免干扰二进制通信 */
  if (g_bridge_mode == BRIDGE_MODE_BINARY) {
    return USBD_OK;
  }

  va_start(args, format);
  len = vsnprintf(buffer, sizeof(buffer) - 1, format, args);
  va_end(args);

  if (len > 0) {
    buffer[len] = '\0';
    MSG_BufferWrite(buffer);
    return USBD_OK;
  }
  return USBD_FAIL;
}

uint8_t USB_Flush(void) { return USBD_OK; }
/**
  * @brief  模式1下暂不缓冲，直接发送；若返回 USBD_BUSY 会丢包
  */
uint8_t USB_SendRaw(const uint8_t *data, uint16_t len)
{
  if (data == NULL) return USBD_FAIL;
  return CDC_Transmit_FS((uint8_t*)data, len);
}
uint8_t USB_IsReady(void) { return (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED); }
uint8_t USB_FlushNonBlocking(void) { return USBD_OK; }

/* USER CODE END PRIVATE_FUNCTIONS_IMPLEMENTATION */

/* 新增函数实现 */

/**
  * @brief  读取ID信息块
  * @retval 状态码
  */
static uint8_t MXT_ReadInfoBlock(void)
{
  uint8_t id_info[7];
  uint8_t result = MXT_I2C_Read(g_mxt_i2c_addr, 0x0000, id_info, 7);
  
  if(result == 0) {
    g_matrix_x_size = id_info[4];  // Matrix X通道数
    g_matrix_y_size = id_info[5];  // Matrix Y通道数
    g_num_objects = id_info[6];    // Object table elements
    
    // 发送信息到USB
    char info_str[64];
    snprintf(info_str, sizeof(info_str), "ID Info: Family=0x%02X, X=%d, Y=%d\r\n", 
             id_info[0], g_matrix_x_size, g_matrix_y_size);
    USB_SendString(info_str);
  }
  
  return result;
}

/**
  * @brief  读取对象表
  * @retval 状态码
  */
static uint8_t MXT_ReadObjectTable(void)
{
  uint8_t obj_entry[6];  // 每个对象表项为6字节
  uint16_t obj_table_start = 0x0007;  // 对象表起始地址
  uint8_t result;
  uint8_t current_report_id = 1;  // Report ID从1开始分配
  
  // 遍历所有对象
  uint8_t max_objs = g_num_objects;
  if (max_objs == 0) max_objs = 64; /* 默认值 */
  for (uint8_t i = 0; i < max_objs; i++) {
    result = MXT_I2C_Read(g_mxt_i2c_addr, obj_table_start + i * 6, obj_entry, 6);
    if (result != 0) return result;
    
    uint8_t obj_type = obj_entry[0];
    uint16_t obj_addr = obj_entry[1] | (obj_entry[2] << 8);
    uint8_t obj_size = obj_entry[3] + 1;  // size = (size_minus_one + 1)
    uint8_t instances = obj_entry[4] + 1; // instances = (instances_minus_one + 1)
    uint8_t num_report_ids = obj_entry[5]; // 每个实例的Report ID数量
    
    // T5 Message Processor
    if (obj_type == 5) {
      g_t5_addr = obj_addr;
      char info[80];
      snprintf(info, sizeof(info), "Found T5 at addr=0x%04X, size=%d\r\n", g_t5_addr, obj_size);
      USB_SendString(info);
    }
    
    // T6 Command Processor
    if (obj_type == 6) {
      g_t6_addr = obj_addr;
      g_t6_report_id = current_report_id;
      char info[80];
      snprintf(info, sizeof(info), "Found T6 at addr=0x%04X, RID=%d\r\n", g_t6_addr, g_t6_report_id);
      USB_SendString(info);
    }
    
    // T44 Message Count
    if (obj_type == 44) {
      g_t44_addr = obj_addr;
      char info[80];
      snprintf(info, sizeof(info), "Found T44 at addr=0x%04X, size=%d\r\n", g_t44_addr, obj_size);
      USB_SendString(info);
    }
    
    // T37 Debug Diagnostic
    if (obj_type == 37) {
      g_t37_addr = obj_addr;
      g_t37_size = obj_size;
      g_page_size = (g_t37_size >= 3) ? (g_t37_size - 2) : 1;  // 去掉 mode+page，至少 1 避免除零
      
      // 计算 pages_per_pass，确保始终设置（避免导出全 0）
      if (g_matrix_x_size > 0 && g_matrix_y_size > 0 && g_page_size > 0) {
        uint16_t data_values = g_matrix_x_size * g_matrix_y_size;
        g_pages_per_pass = (data_values * 2 + g_page_size - 1) / g_page_size;
      } else {
        g_pages_per_pass = 1;  // 至少读 1 页，避免 g_pages_per_pass==0 导致全零
      }
      
      char info[80];
      snprintf(info, sizeof(info), "Found T37 at addr=0x%04X, size=%d, pages=%d\r\n", 
               g_t37_addr, g_t37_size, g_pages_per_pass);
      USB_SendString(info);
    }
    
    // T100 Multiple Touch Touchscreen
    if (obj_type == 100) {
      g_t100_addr = obj_addr;
      g_t100_size = obj_size;
      g_t100_report_id = current_report_id;
      char info[80];
      snprintf(info, sizeof(info), "Found T100 at addr=0x%04X, size=%d, RID=%d-%d\r\n",
               g_t100_addr, g_t100_size,
               g_t100_report_id, g_t100_report_id + instances * num_report_ids - 1);
      USB_SendString(info);
    }
    
    // 累加Report ID
    current_report_id += instances * num_report_ids;
  }
  
  return 0;
}

/**
  * @brief  读取 T100 对象中 UNKNOWN[9]/UNKNOWN[20] 字段值
  */
static void MXT_ReadT100UnknownFields(void)
{
  uint16_t t100_addr = MXT_GetObjectAddr(100);
  uint8_t t100_size = g_t100_size;

  if (t100_addr == 0) {
    USB_SendString("ERR: T100 not found in object table\r\n");
    return;
  }
  if (t100_size < 21) {
    char line[96];
    snprintf(line, sizeof(line), "ERR: T100 size too small (%d), need >= 21\r\n", t100_size);
    USB_SendString(line);
    return;
  }
  if (t100_size > 128) {
    USB_SendString("ERR: T100 size too large to read\r\n");
    return;
  }

  uint8_t t100_data[128];
  if (MXT_I2C_Read(g_mxt_i2c_addr, t100_addr, t100_data, t100_size) != 0) {
    USB_SendString("ERR: Read T100 data failed\r\n");
    return;
  }

  char line[160];
  snprintf(line, sizeof(line),
           "T100: OBJECT_ADDRESS=%u OBJECT_SIZE=%u UNKNOWN[9]=%u UNKNOWN[20]=%u\r\n",
           t100_addr, t100_size, t100_data[9], t100_data[20]);
  USB_SendString(line);
}


/**
  * @brief  启用/禁用输出
  * @param  enable: 1=启用输出, 0=禁用输出
  */
static void MXT_EnableOutput(uint8_t enable)
{
  g_output_enabled = enable;
}

/**
  * @brief  检查输出是否启用
  * @retval 1=输出启用, 0=输出禁用
  */
static uint8_t MXT_IsOutputEnabled(void)
{
  return g_output_enabled;
}

/**
  * @brief  写入数据到消息缓冲区
  * @param  str: 字符串
  */
static void MSG_BufferWrite(const char *str)
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

/* 专用 TX 块缓冲：CDC 异步发送时仍从该缓冲读，避免环形缓冲被覆盖导致乱码 */
#define MSG_FLUSH_CHUNK  64
static uint8_t g_msg_tx_chunk[MSG_FLUSH_CHUNK];

/**
  * @brief  刷新消息缓冲区到USB (在主循环中调用)
  *         从环形缓冲拷贝到 g_msg_tx_chunk 再发送，避免 USB 发送未完成时环形缓冲被覆盖
  * @retval 1=本次已发送数据, 0=缓冲区空或USB忙未发送
  */
static uint8_t MSG_BufferFlush(void)
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


/**
  * @brief  初始化触摸屏配置
  */
void MXT_InitTouchScreen(void)
{
  // 读取ID信息块以获取X,Y尺寸
  if (MXT_ReadInfoBlock() != 0) return;
  
  // 读取对象表
  if (MXT_ReadObjectTable() != 0) return;
  
  // 诊断缓冲区使用静态内存，无需分配，直接标记初始化完成
  g_touch_inited = 1;
  g_debugctrl_applied = 0;
  (void)MXT_ApplyStartupDebugCtrl();
  
  // 不自动配置SPI，由SPI命令手动触发
  // MXT_EnableSPIMode();
}

static uint8_t MXT_ApplyStartupDebugCtrl(void)
{
  uint8_t debugctrl = MXT_STARTUP_DEBUGCTRL;

  if (!g_touch_inited || g_t6_addr == 0) {
    return 1;
  }

  if (g_debugctrl_applied) {
    return 0;
  }

  if (MXT_I2C_Write(g_mxt_i2c_addr, (uint16_t)(g_t6_addr + MXT_T6_DEBUGCTRL_OFFSET), &debugctrl, 1) != 0) {
    USB_SendString("ERR: Write DEBUGCTRL failed\r\n");
    return 2;
  }

  g_debugctrl_applied = 1;
  g_spi_check_requested = 1;
  USB_SendString("INFO: DEBUGCTRL enabled (Byte4=0x20 SIGNAL, DEBUGCTRL2 disabled)\r\n");
  return 0;
}




/**
  * @brief  读取单页T37数据
  * @param  mode: 诊断模式
  * @param  page: 页码 (0表示第一页,写入模式命令; >0表示后续页,写入PAGE_UP)
  * @retval 状态码
  */
static uint8_t MXT_ReadT37Page(uint8_t mode, uint8_t page)
{
  uint8_t cmd;
  uint8_t read_cmd;
  uint8_t result;
  uint16_t timeout;
  
  // Step 1: 写入命令到 T6+5（增加重试和小延时，避免瞬间 NACK）
  if (page == 0) {
    cmd = mode;  // 第一页: 写入诊断模式
  } else {
    cmd = 0x01;  // 后续页: PAGE_UP
  }
  
  for (uint8_t tries = 0; tries < 10; tries++) {
    result = MXT_I2C_Write(g_mxt_i2c_addr, g_t6_addr + 5, &cmd, 1);
    if (result == STATUS_ADDR_NACK) {
      MXT_DelayUs(100);; /* 1ms 再试 */
      continue;
    }
    break;
  }
  if (result != 0) {
    USB_SendString("ERR: T37 write cmd failed\r\n");
    return result;
  }
  
  // Step 2: 轮询等待命令完成 (延长至 1s，1ms 步进)
  timeout = 0;
  /* 写命令后先等待 1ms 再开始轮询，避免立即读返回 NACK */
  while (timeout < 2000) { /* 1000 * 1ms = 1s */
    MXT_DelayUs(100);
    result = MXT_I2C_Read(g_mxt_i2c_addr, g_t6_addr + 5, &read_cmd, 1);
    /* 如果是 NACK，认为设备还没准备好，继续轮询 */
    if (result == STATUS_ADDR_NACK) {
      timeout++;
      continue;
    } else if (result != 0) {
      USB_SendString("ERR: T37 read cmd status failed\r\n");
      return result;
    }
    
    if (read_cmd == 0) break;  // 命令已执行完成
    timeout++;
  }
  if (timeout >= 1000) {
    USB_SendString("ERR: T37 cmd timeout\r\n");
    return 0xFF;  // 超时
  }
  
  
  // Step 3: 读取T37数据并等待mode/page匹配
  /* 命令清零后T37数据可能还未更新，需要轮询等待mode和page字段匹配 */
  for (uint16_t tries = 0; tries < 500; tries++) { /* 最长 500ms */
    result = MXT_I2C_Read(g_mxt_i2c_addr, g_t37_addr, g_t37_data, g_t37_size);
    if (result == STATUS_ADDR_NACK) {
      MXT_DelayUs(100); /* 1ms */
      continue;
    }
    if (result != 0) {
      USB_SendString("ERR: T37 read page failed\r\n");
      return result;
    }
    
    // 检查mode和page是否匹配
    if (g_t37_data[0] == mode && g_t37_data[1] == page) {
      return 0;  // 成功
    }   
      // 数据还未准备好，等待1ms后重试
      HAL_Delay(1);
  }
  
  // 超时，输出调试信息
  char dbg[80];
  snprintf(dbg, sizeof(dbg), "ERR: T37 mismatch (exp=0x%02X/%d, got=0x%02X/%d)\r\n", 
           mode, page, g_t37_data[0], g_t37_data[1]);
  USB_SendString(dbg);
  return 0xFE;  // 模式不匹配
}

/**
  * @brief  读取完整的诊断数据帧
  * @retval 状态码
  */
static uint8_t MXT_ReadCompleteDiagnosticFrame(void)
{
  uint8_t result;
  uint16_t data_offset = 0;
  
  if (g_diag_mode == DIAG_MODE_NONE || g_diag_buffer == NULL) {
    return 0xFF;
  }
  
  /* 若 T37 未配置（从未找到或 pages=0），先刷新对象表再检查，避免导出全 0 */
  if (g_pages_per_pass == 0 || g_t37_addr == 0) {
    if (g_num_objects > 0) {
      MXT_ReadObjectTable();
    }
    if (g_pages_per_pass == 0 || g_t37_addr == 0) {
      USB_SendString("ERR: T37 not configured (run INFO/OBJTBL first or check device)\r\n");
      return 0xFD;
    }
  }
  
  // 暂停CHG消息处理，防止I2C冲突
  g_t37_reading = 1;
  
  // 清空缓冲区
  memset(g_diag_buffer, 0, g_matrix_x_size * g_matrix_y_size * sizeof(uint16_t));
  
  // 读取所有页
  for (uint8_t page = 0; page < g_pages_per_pass; page++) {
    result = MXT_ReadT37Page(g_diag_mode, page);
    if (result != 0) {
      char err[64];
      snprintf(err, sizeof(err), "ERR: T37 read failed page %d, code 0x%02X\r\n", page, result);
      USB_SendString(err);
      // 恢复CHG消息处理
      g_t37_reading = 0;
      return result;
    }
    
    // 提取数据 (跳过前2字节: mode和page)
    uint8_t *data = &g_t37_data[2];
    uint8_t data_len = g_page_size;
    
    // 复制到缓冲区
    for (uint8_t i = 0; i < data_len && data_offset < (g_matrix_x_size * g_matrix_y_size); i += 2) {
      g_diag_buffer[data_offset] = (uint16_t)(data[i]) | ((uint16_t)(data[i+1]) << 8);
      data_offset++;
    }
  }
  
  // 恢复CHG消息处理
  g_t37_reading = 0;
  
  // T37读取完成后，检查CHG引脚，若拉低则触发消息处理
  if (HAL_GPIO_ReadPin(CHG_EXTI3_GPIO_Port, CHG_EXTI3_Pin) == GPIO_PIN_RESET) {
    MXT_CheckAndProcessMessages();
  }
  
  return 0;
}

/**
  * @brief  输出诊断数据：先输出原始32x20，再输出映射16x16
  */
static void MXT_OutputDiagnosticData(void)
{
  if (g_diag_buffer == NULL || g_matrix_x_size == 0 || g_matrix_y_size == 0) {
    return;
  }

  /*
   * 自电容输出格式需求：
   * - 数据顺序：前 20 个为 Y0-Y19，后 32 个为 X0-X31（线性列表）
   * - 输出格式：
   *   Y: v0,v1,...,v19\r\n
   *   X: v0,v1,...,v31\r\n
   * 其他模式保持原矩阵输出。
   */

  if (g_diag_mode == DIAG_MODE_SELF_DELTA || g_diag_mode == DIAG_MODE_SELF_REF || g_diag_mode == DIAG_MODE_SELF_SIGNAL) {
    char line_buf[768];
    int pos = 0;

    /* Y: 取前 20 个 */
    USB_SendString("Y:");
    pos = 0;
    for (uint8_t i = 0; i < 20; i++) {
      int16_t v = (int16_t)g_diag_buffer[i];
      pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "%d", (int)v);
      if (i < 19) pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, ",");
    }
    pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "\r\n");
    USB_SendString(line_buf);

    /* X: 紧接 32 个 */
    USB_SendString("X:");
    pos = 0;
    for (uint8_t i = 0; i < 32; i++) {
      int16_t v = (int16_t)g_diag_buffer[20 + i];
      pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "%d", (int)v);
      if (i < 31) pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, ",");
    }
    pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "\r\n\r\n");
    USB_SendString(line_buf);
    return;
  }

  char line_buf[512];

  /* 默认：互容等仍输出原始矩阵 */
  USB_SendString("=== RAW 32x20 ===\r\n");
  for (uint8_t y = 0; y < g_matrix_y_size; y++) {
    int pos = 0;

    for (uint8_t x = 0; x < g_matrix_x_size; x++) {
      uint16_t offset = y + x * g_matrix_y_size;
      int16_t value = (int16_t)g_diag_buffer[offset];

      pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "%6d", value);

      if (x < g_matrix_x_size - 1) {
        pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, ",");
      }
    }

    pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "\r\n");
    USB_SendString(line_buf);
  }

  USB_SendString("\r\n");
}

/**
  * @brief  输出完整矩阵 (mapall)：全部 X*Y，不裁剪
  */
static void MXT_OutputMapAll(void)
{
  if (g_diag_buffer == NULL || g_matrix_x_size == 0 || g_matrix_y_size == 0) {
    return;
  }
  char line_buf[512];
  MSG_BufferWrite("=== MAPALL ===\r\n");
  for (uint8_t y = 0; y < g_matrix_y_size; y++) {
    int pos = 0;
    for (uint8_t x = 0; x < g_matrix_x_size; x++) {
      uint16_t offset = y + x * g_matrix_y_size;
      int16_t value = (int16_t)g_diag_buffer[offset];
      pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "%6d", value);
      if (x < g_matrix_x_size - 1) {
        pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, ",");
      }
    }
    pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "\r\n");
    MSG_BufferWrite(line_buf);
  }
  MSG_BufferWrite("=== FRAME_END ===\r\n\r\n");
  /* 尽量排空：确保这一帧完整发出 */
  MXT_FlushMessageBuffer();
}

/**
  * @brief  输出 16x16 矩阵 (map16)：取原始数据位置顺序 0-15 行、0-15 列
  */
static void MXT_OutputMap16(void)
{
  if (g_diag_buffer == NULL || g_matrix_x_size == 0 || g_matrix_y_size == 0) {
    return;
  }
  /* 取前 16 列、前 16 行，超出则按实际范围 */
  uint8_t cols = (g_matrix_x_size >= 16) ? 16 : g_matrix_x_size;
  uint8_t rows = (g_matrix_y_size >= 16) ? 16 : g_matrix_y_size;
  char line_buf[512];
  MSG_BufferWrite("=== MAP16 (16x16, index 0-15) ===\r\n");
  for (uint8_t out_y = 0; out_y < rows; out_y++) {
    int pos = 0;
    for (uint8_t out_x = 0; out_x < cols; out_x++) {
      uint16_t offset = out_y + out_x * g_matrix_y_size;
      int16_t value = (int16_t)g_diag_buffer[offset];
      pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "%6d", value);
      if (out_x < cols - 1) {
        pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, ",");
      }
    }
    pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "\r\n");
    MSG_BufferWrite(line_buf);
  }
  MSG_BufferWrite("=== FRAME_END ===\r\n\r\n");
  /* 尽量排空：确保这一帧完整发出 */
  MXT_FlushMessageBuffer();
}

/**
  * @brief  16x16 通用输出：先旋转再翻转
  * @param  rot: 0=不旋转, 1=顺时针90°, 2=逆时针90°
  * @param  flip_mask: bit0=X(左右镜像), bit1=Y(上下镜像)；在旋转之后再应用
  */
static void MXT_OutputMap16Transformed(uint8_t rot, uint8_t flip_mask)
{
  if (g_diag_buffer == NULL || g_matrix_x_size < 16 || g_matrix_y_size < 16) {
    return;
  }

  char line_buf[512];
  char title[96];
  const char *rot_str = (rot == 1) ? "R90" : (rot == 2) ? "L90" : "";
  const char *fx = (flip_mask & 0x01) ? "X" : "";
  const char *fy = (flip_mask & 0x02) ? "Y" : "";
  if (rot_str[0] || fx[0] || fy[0]) {
    snprintf(title, sizeof(title), "=== MAP16%s%s%s (rotate then flip) ===", rot_str, fx, fy);
  } else {
    snprintf(title, sizeof(title), "=== MAP16 (rotate then flip) ===");
  }
  MSG_BufferWrite(title);
  MSG_BufferWrite("\r\n");

  for (uint8_t out_y = 0; out_y < 16; out_y++) {
    int pos = 0;
    for (uint8_t out_x = 0; out_x < 16; out_x++) {
      /* output coords */
      uint8_t x = out_x;
      uint8_t y = out_y;

      /* 先翻转（因为我们做的是逆向映射：output -> source；翻转是自逆） */
      if (flip_mask & 0x01) x = (uint8_t)(15 - x); /* flip X: 左右镜像 */
      if (flip_mask & 0x02) y = (uint8_t)(15 - y); /* flip Y: 上下镜像 */

      /* 再反旋转（因为 forward: rotate then flip；inverse: unflip then unrotate） */
      uint8_t src_x = x;
      uint8_t src_y = y;
      if (rot == 1) {
        /* forward CW90: new(x,y)=old(y,15-x) -> inverse: old_x=y, old_y=15-x */
        src_x = y;
        src_y = (uint8_t)(15 - x);
      } else if (rot == 2) {
        /* forward CCW90: new(x,y)=old(15-y,x) -> inverse: old_x=15-y, old_y=x */
        src_x = (uint8_t)(15 - y);
        src_y = x;
      }

      uint16_t offset = src_y + src_x * g_matrix_y_size;
      int16_t value = (int16_t)g_diag_buffer[offset];
      pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "%6d", value);
      if (out_x < 15) {
        pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, ",");
      }
    }
    pos += snprintf(line_buf + pos, sizeof(line_buf) - pos, "\r\n");
    MSG_BufferWrite(line_buf);
  }
  MSG_BufferWrite("=== FRAME_END ===\r\n\r\n");
  /* 尽量排空：确保这一帧完整发出 */
  MXT_FlushMessageBuffer();
}

/* 触点事件队列操作：简单无锁环形队列
 * - Push: 将最新触点事件写入尾部；队列满时丢弃最旧的一条，保证最新事件不会丢失
 * - Pop : 从头部取出最早的一条；若为空返回 0
 */
static void MXT_TouchQueuePush(uint8_t id, uint16_t x, uint16_t y, TouchAction_t action)
{
  uint8_t head = g_touch_q_head;
  uint8_t tail = g_touch_q_tail;
  uint8_t next_tail = (uint8_t)((tail + 1) % TOUCH_QUEUE_SIZE);

  /* 队列满：丢弃最旧一条（head 向前移动一位），避免覆盖未定义状态 */
  if (next_tail == head) {
    g_touch_q_head = (uint8_t)((head + 1) % TOUCH_QUEUE_SIZE);
  }

  g_touch_queue[tail].id     = id;
  g_touch_queue[tail].x      = x;
  g_touch_queue[tail].y      = y;
  g_touch_queue[tail].action = action;
  g_touch_q_tail = next_tail;
}

static uint8_t MXT_TouchQueuePop(TouchInfo_t *out)
{
  if (out == NULL) return 0;

  uint8_t head = g_touch_q_head;
  uint8_t tail = g_touch_q_tail;
  if (head == tail) {
    return 0; /* 队列空 */
  }

  *out = g_touch_queue[head];
  g_touch_q_head = (uint8_t)((head + 1) % TOUCH_QUEUE_SIZE);
  return 1;
}

/* CRC16 (Modbus/IBM，多项式0xA001，初值0xFFFF) */
static uint16_t Map16_CalcCRC16(const uint8_t *data, uint16_t length)
{
  if (data == NULL || length == 0) return 0;
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
      else crc >>= 1;
    }
  }
  return crc;
}

/* CRC-16/CCITT-FALSE（poly=0x1021，init=0xFFFF，MSB first，xorout=0）；按字节覆盖 AA 10 33…帧内 CRC 之前所有字节 */
static uint16_t CRC16_CCITT_FALSE(const uint8_t *data, uint16_t length)
{
  uint16_t wCRCin = 0xFFFFU;
  const uint16_t wCPoly = 0x1021U;

  if (data == NULL || length == 0U) {
    return 0U;
  }

  while (length--) {
    wCRCin ^= (uint16_t)(*data++) << 8;
    for (uint8_t i = 0U; i < 8U; i++) {
      if (wCRCin & 0x8000U) {
        wCRCin = (uint16_t)((wCRCin << 1) ^ wCPoly);
      } else {
        wCRCin <<= 1;
      }
    }
  }
  return wCRCin;
}

/**
  * @brief  发送 Mode3 风格二进制包：AA 10 33 | LEN | frame | line | 32byte | CRC16
  * @note   直接走 CDC_Transmit_FS；使用 us 级别短延时等待 USB 空闲，尽量避免丢包
  */
static void MXT_SendMode3Packets(uint8_t rot, uint8_t flip_mask, uint8_t frame_id)
{
  if (g_diag_buffer == NULL) return;

  /* 自动适应矩阵大小，最多取 16x16 */
  uint8_t max_x = (g_matrix_x_size > 16) ? 16 : g_matrix_x_size;
  uint8_t max_y = (g_matrix_y_size > 16) ? 16 : g_matrix_y_size;

  const uint8_t header_size = 3; /* AA 10 33 */
  const uint8_t data_count = 16;
  const uint8_t base_data_bytes = data_count * 2;
  /* CHGNO 模式: 在 16*2 字节后增加 [触点号(1) | x(2) | y(2) | 动作(1)] 共 6 字节 */
  const uint8_t extra_touch_bytes = g_stream_chgno ? 6 : 0;
  const uint8_t packet_len = header_size + 1 /*len*/ + 1 /*frame*/ + 1 /*line*/
                             + base_data_bytes + extra_touch_bytes + 2 /*crc*/;
  uint8_t buffer[3 + 1 + 1 + 1 + 32 + 6 + 2];

  buffer[0] = 0xAA;
  buffer[1] = 0x10;
  buffer[2] = 0x33;
  buffer[3] = packet_len;
  buffer[4] = frame_id;

  for (uint8_t out_y = 0; out_y < max_y; out_y++) {
    /* 先准备一行 16 个值（变换后） */
    uint16_t vals[16];
    memset(vals, 0, sizeof(vals));

    for (uint8_t out_x = 0; out_x < max_x; out_x++) {
      uint8_t x = out_x;
      uint8_t y = out_y;
      if (flip_mask & 0x01) x = (uint8_t)(max_x - 1 - x); 
      if (flip_mask & 0x02) y = (uint8_t)(max_y - 1 - y); 
      
      uint8_t src_x = x, src_y = y;
      if (rot == 1) { 
        src_x = y;
        src_y = (uint8_t)(max_x - 1 - x);
      } else if (rot == 2) {
        src_x = (uint8_t)(max_y - 1 - y);
        src_y = x;
      }

      if (src_x < g_matrix_x_size && src_y < g_matrix_y_size) {
        vals[out_x] = (uint16_t)g_diag_buffer[src_y + src_x * g_matrix_y_size];
      }
    }

    buffer[5] = out_y; /* line id */
    for (uint8_t i = 0; i < data_count; i++) {
      buffer[6 + i * 2] = (uint8_t)((vals[i] >> 8) & 0xFF);
      buffer[7 + i * 2] = (uint8_t)(vals[i] & 0xFF);
    }

        /* CHGNO 模式: 在 CRC16 之前增加 [触点号 | x | y | 动作类型]
         * 注意：触点坐标翻转仅受 g_stream_touch_flip 控制，与 MAP16 矩阵翻转 (flip_mask) 解耦，
         * 这样 CHGNOX / CHGNOY / CHGNOXY 与 MAP16L90X 等组合时才会有可见差异。
         */
    if (g_stream_chgno) {
      uint8_t  touch_id = 0xFF;
      uint16_t tx = 0;
      uint16_t ty = 0;
      uint8_t  action = TOUCH_ACTION_NONE;

      /* 优先从队列中取出最早的一条触点事件，确保多点触摸 (TCH0/TCH1/...) 按顺序上传。
       * 若队列为空，则退回到最后一条(g_last_touch)以保持兼容旧行为。
       */
      TouchInfo_t queued;
      if (MXT_TouchQueuePop(&queued)) {
        touch_id = queued.id;
        tx = queued.x;
        ty = queued.y;
        action = (uint8_t)queued.action;
      } else if (g_last_touch_valid) {
        touch_id = g_last_touch.id;
        tx = g_last_touch.x;
        ty = g_last_touch.y;
        action = (uint8_t)g_last_touch.action;
      }

      if (touch_id != 0xFF) {
        /* 坐标翻转:
         * X翻转: x' = 830 - x
         * Y翻转: y' = 940 - y
         * 仅由 g_stream_touch_flip 控制，与 MAP16 矩阵翻转解耦。
         */
        if (g_stream_touch_flip & 0x01) {
          if (tx <= TOUCH_MAX_X) tx = (uint16_t)(TOUCH_MAX_X - tx);
        }
        if (g_stream_touch_flip & 0x02) {
          if (ty <= TOUCH_MAX_Y) ty = (uint16_t)(TOUCH_MAX_Y - ty);
        }

        /* 防止越界，限定在 [0, MAX] */
        if (tx > TOUCH_MAX_X) tx = TOUCH_MAX_X;
        if (ty > TOUCH_MAX_Y) ty = TOUCH_MAX_Y;
      }

      uint8_t idx = 6 + base_data_bytes;
      buffer[idx++] = touch_id;
      buffer[idx++] = (uint8_t)(tx & 0xFF);
      buffer[idx++] = (uint8_t)((tx >> 8) & 0xFF);
      buffer[idx++] = (uint8_t)(ty & 0xFF);
      buffer[idx++] = (uint8_t)((ty >> 8) & 0xFF);
      buffer[idx++] = action;
    }

    uint16_t crc = CRC16_CCITT_FALSE(buffer, (uint16_t)(packet_len - 2U));
    buffer[packet_len - 2] = (uint8_t)((crc >> 8) & 0xFF);
    buffer[packet_len - 1] = (uint8_t)(crc & 0xFF);

    /* 直接二进制发送；忙则使用 us 级短延时反复等待，最多等待约 20ms */
    uint32_t remaining_us = 20000; /* 每行最长等待时间，防止死等 */
    while (remaining_us > 0) {
      if (CDC_Transmit_FS(buffer, packet_len) == USBD_OK) {
        break;
      }
      /* USB 正忙，等待一小段时间再试 */
      MXT_DelayUs(50);
      if (remaining_us > 50) {
        remaining_us -= 50;
      } else {
        remaining_us = 0;
      }
    }
  }
}

/**
  * @brief  自电容专用 Mode3 分包发送
  * @note   数据线性顺序：前 20 个 Y，再后 32 个 X，共 52 个 int16
  *         - use_map16=0：发送 4 包（每包 16 个值，最后不足补 0），line=0..3
  *         - use_map16=1：裁剪发送 2 包：Y 前16 + X 前16，共 32 个值，line=0..1
  *         包格式：AA 10 33 | LEN | 帧号 | 行号 | 16*2字节数据 | CRC16
  */
static void MXT_SendSelfCapMode3Packets(uint8_t use_map16, uint8_t frame_id)
{
  if (g_diag_buffer == NULL) return;

  const uint8_t header_size = 3; /* AA 10 33 */
  const uint8_t data_count = 16;
  const uint8_t payload_bytes = data_count * 2;
  const uint8_t packet_len = header_size + 1 /*len*/ + 1 /*frame*/ + 1 /*line*/ + payload_bytes + 2 /*crc*/;

  uint8_t buffer[3 + 1 + 1 + 1 + 32 + 2];
  buffer[0] = 0xAA;
  buffer[1] = 0x10;
  buffer[2] = 0x33;
  buffer[3] = packet_len;
  buffer[4] = frame_id;

  /* 组装一个线性数组（最多 52；裁剪时最多 32） */
  int16_t linear[52];
  uint8_t linear_len = 0;

  if (use_map16) {
    /* Y[0..15] */
    for (uint8_t i = 0; i < 16; i++) {
      linear[linear_len++] = (int16_t)g_diag_buffer[i];
    }
    /* X[0..15] 位于原始 [20..35] */
    for (uint8_t i = 0; i < 16; i++) {
      linear[linear_len++] = (int16_t)g_diag_buffer[20 + i];
    }
  } else {
    /* Y[0..19] */
    for (uint8_t i = 0; i < 20; i++) {
      linear[linear_len++] = (int16_t)g_diag_buffer[i];
    }
    /* X[0..31] */
    for (uint8_t i = 0; i < 32; i++) {
      linear[linear_len++] = (int16_t)g_diag_buffer[20 + i];
    }
  }

  uint8_t total_packets = use_map16 ? 2 : 4;

  for (uint8_t pkt = 0; pkt < total_packets; pkt++) {
    buffer[5] = pkt; /* line 作为发送计数 */

    for (uint8_t i = 0; i < data_count; i++) {
      uint8_t idx = (uint8_t)(pkt * data_count + i);
      int16_t v = 0;
      if (idx < linear_len) v = linear[idx];
      /* 按 Mode3 现有约定：高字节在前 */
      buffer[6 + i * 2] = (uint8_t)(((uint16_t)v >> 8) & 0xFF);
      buffer[7 + i * 2] = (uint8_t)((uint16_t)v & 0xFF);
    }

    uint16_t crc = CRC16_CCITT_FALSE(buffer, (uint16_t)(packet_len - 2U));
    buffer[packet_len - 2] = (uint8_t)((crc >> 8) & 0xFF);
    buffer[packet_len - 1] = (uint8_t)(crc & 0xFF);

    uint32_t remaining_us = 20000;
    while (remaining_us > 0) {
      if (CDC_Transmit_FS(buffer, packet_len) == USBD_OK) {
        break;
      }
      MXT_DelayUs(50);
      remaining_us = (remaining_us > 50) ? (remaining_us - 50) : 0;
    }
  }
}

/**
  * @brief  定时读取诊断数据 (在SysTick或定时器中调用)
  */
void MXT_TimerDiagnosticRead(void)
{
  /* SPI 流模式下让出总线，不做 I2C 诊断读取。 */
  if (g_spi_stream_enabled != 0U) {
    return;
  }

  // 只在模式1且输出使能时工作
  if (g_bridge_mode != BRIDGE_MODE_STRING || !MXT_IsOutputEnabled()) {
    return;
  }
  
  // 检查是否到达设定间隔
  uint32_t current_time = HAL_GetTick();
  if ((current_time - g_last_diag_time) >= g_diag_interval_ms) {
    g_last_diag_time = current_time;

    /* START1 模式：每次采集前先执行一次 CAL (T6+2=0x01) */
    if (g_stream_pre_cal) {
      if (!g_touch_inited) {
        MXT_InitTouchScreen();
      }
      if (g_t6_addr != 0) {
        uint8_t cal = 0x01;
        (void)MXT_I2C_Write(g_mxt_i2c_addr, g_t6_addr + 2, &cal, 1);
      }
    }
    
    // 读取完整帧
    if (MXT_ReadCompleteDiagnosticFrame() == 0) {
      if (g_stream_map16_hex) {
        MXT_SendMode3Packets(g_stream_rot, g_stream_flip, g_stream_frame_id++);
      } else if (g_stream_map16_char) {
        MXT_OutputMap16Transformed(g_stream_rot, g_stream_flip);
      } else {
        MXT_OutputDiagnosticData();
      }
    }
  }
}

/**
  * @brief  解析并输出消息事件说明
  * @param  msg_str: 输出缓冲区
  * @param  pos: 当前位置
  * @param  max_len: 最大长度
  * @param  report_id: 报告ID
  * @param  msg_data: 消息数据(11字节)
  * @retval 新位置
  */
static int MXT_ParseMessage(char *msg_str, int pos, int max_len, uint8_t report_id, uint8_t *msg_data)
{
  uint8_t status = msg_data[1];
  
  /* T6 命令处理器消息 */
  if (report_id == g_t6_report_id) {
    pos += snprintf(msg_str + pos, max_len - pos, "  [T6 CMD]");
    if (status & 0x80) pos += snprintf(msg_str + pos, max_len - pos, " RESET");
    if (status & 0x40) pos += snprintf(msg_str + pos, max_len - pos, " OFL");
    if (status & 0x20) pos += snprintf(msg_str + pos, max_len - pos, " SIGERR");
    if (status & 0x10) pos += snprintf(msg_str + pos, max_len - pos, " CAL");
    if (status & 0x08) pos += snprintf(msg_str + pos, max_len - pos, " CFGERR");
    if (status & 0x04) pos += snprintf(msg_str + pos, max_len - pos, " COMSERR");
    /* 配置校验和 */
    uint32_t cks = msg_data[2] | (msg_data[3] << 8) | (msg_data[4] << 16);
    pos += snprintf(msg_str + pos, max_len - pos, " CKS=0x%06X", (unsigned int)cks);
  }
  /* T100 多点触控屏幕消息 (18个Report ID: 1屏幕状态 + 1保留 + 16触摸) */
  else if (report_id >= g_t100_report_id && report_id < g_t100_report_id + 18) {
    uint8_t rid_offset = report_id - g_t100_report_id;
    
    /* 第一个Report ID 是屏幕状态消息 */
    if (rid_offset == 0) {
      pos += snprintf(msg_str + pos, max_len - pos, "  [T100 SCR]");
      if (status & 0x80) pos += snprintf(msg_str + pos, max_len - pos, " DETECT");
      if (status & 0x40) pos += snprintf(msg_str + pos, max_len - pos, " SUP");
    }
    /* 第二个Report ID 是保留 */
    else if (rid_offset == 1) {
      pos += snprintf(msg_str + pos, max_len - pos, "  [T100 RSV]");
    }
    /* 后续 Report ID 是触摸状态消息 (0-15) */
    else {
      uint8_t touch_id = rid_offset - 2;  /* 触摸ID 0-15 */
      uint8_t detect = (status >> 7) & 0x01;
      uint8_t type = (status >> 4) & 0x07;
      uint8_t event = status & 0x0F;
      uint16_t x_pos = msg_data[2] | (msg_data[3] << 8);
      uint16_t y_pos = msg_data[4] | (msg_data[5] << 8);
      
      /* 触摸类型字符串 */
      const char *type_str = "???";
      switch (type) {
        case 0: type_str = "RSV"; break;
        case 1: type_str = "FNG"; break;  /* FINGER */
        case 2: type_str = "STY"; break;  /* PASSIVE STYLUS */
        case 5: type_str = "GLV"; break;  /* GLOVE */
        case 6: type_str = "LRG"; break;  /* LARGE TOUCH */
      }
      
      /* 事件字符串 */
      const char *event_str = "???";
      switch (event) {
        case 0: event_str = "NONE"; break;
        case 1: event_str = "MOVE"; break;
        case 2: event_str = "UNSUP"; break;
        case 3: event_str = "SUP"; break;
        case 4: event_str = "DOWN"; break;
        case 5: event_str = "UP"; break;
        case 6: event_str = "UNSUPSUP"; break;
        case 7: event_str = "UNSUPUP"; break;
        case 8: event_str = "DOWNSUP"; break;
        case 9: event_str = "DOWNUP"; break;
      }
      
      /* 输出格式调整为:
       * T100  [TCH1]  X=516 Y=660  STY MOVE
       * 即: "T100  [TCHn]  X=%d Y=%d  <TYPE> <EVENT>"
       */
      pos += snprintf(msg_str + pos, max_len - pos, 
                      "T100  [TCH%d]  X=%d Y=%d  %s %s%s",
                      touch_id, x_pos, y_pos,
                      type_str, event_str,
                      detect ? "" : " (no detect)");
    }
  }
  /* 其他未知消息 - 尝试解析为触摸格式 (根据状态字节特征) */
  else {
    /* 检查是否符合触摸消息特征: EVENT(0-9) 和 TYPE(0,1,2,5,6) */
    uint8_t type = (status >> 4) & 0x07;
    uint8_t event = status & 0x0F;
    
    if (event <= 9 && (type == 0 || type == 1 || type == 2 || type == 5 || type == 6)) {
      /* 可能是触摸消息 */
      uint8_t detect = (status >> 7) & 0x01;
      uint16_t x_pos = msg_data[2] | (msg_data[3] << 8);
      uint16_t y_pos = msg_data[4] | (msg_data[5] << 8);
      
      const char *type_str = "???";
      switch (type) {
        case 0: type_str = "RSV"; break;
        case 1: type_str = "FNG"; break;
        case 2: type_str = "STY"; break;
        case 5: type_str = "GLV"; break;
        case 6: type_str = "LRG"; break;
      }
      
      const char *event_str = "???";
      switch (event) {
        case 0: event_str = "NONE"; break;
        case 1: event_str = "MOVE"; break;
        case 2: event_str = "UNSUP"; break;
        case 3: event_str = "SUP"; break;
        case 4: event_str = "DOWN"; break;
        case 5: event_str = "UP"; break;
        case 6: event_str = "UNSUPSUP"; break;
        case 7: event_str = "UNSUPUP"; break;
        case 8: event_str = "DOWNSUP"; break;
        case 9: event_str = "DOWNUP"; break;
      }
      
      pos += snprintf(msg_str + pos, max_len - pos, 
                      "TCH? X=%d Y=%d  %s %s%s",
                      x_pos, y_pos,
                      type_str, event_str,
                      detect ? "" : " (no detect)");
    } else {
      /* 未知消息 */
      pos += snprintf(msg_str + pos, max_len - pos, "  [RID%d] st=0x%02X", report_id, status);
    }
  }
  
  return pos;
}

void MXT_SetChgPending(void)
{
  g_chg_pending = 1;
}

/**
  * @brief  检查并处理CHG引脚消息 (在主循环中调用；CHG 中断仅置 g_chg_pending，不在此做 I2C)
  */
void MXT_CheckAndProcessMessages(void)
{
  /* SPI 流模式下不处理 CHG，也不触发 I2C 读消息。 */
  if (g_spi_stream_enabled != 0U) {
    g_chg_pending = 0;
    return;
  }

  // 只在模式1工作；消息输出开关仅影响文本输出，不影响 CHGNO 模式下的触点更新
  if (g_bridge_mode != BRIDGE_MODE_STRING) return;
  
  // 未使能 CHG 处理且未处于 CHGNO 流模式时跳过（默认不处理；CHGON 或 START CHGNO 后才会处理）
  if (!g_chg_process_enabled && !g_stream_chgno) return;
  
  // T37读取进行中，跳过CHG消息处理
  if (g_t37_reading) return;
  
  uint32_t current_time = HAL_GetTick();
  // 防抖: 无中断置位时 50ms 内不重复处理；有 g_chg_pending 时立即处理（响应 CHG 拉低）
  if (!g_chg_pending && (current_time - g_last_msg_time) < 50) {
    return;
  }
  
  // 检查CHG引脚状态 (低电平表示有消息)；若有中断置位或引脚为低则处理
  if (g_chg_pending || HAL_GPIO_ReadPin(CHG_EXTI3_GPIO_Port, CHG_EXTI3_Pin) == GPIO_PIN_RESET) {
    g_chg_pending = 0;
    g_last_msg_time = current_time;
    
    // 循环读取所有消息，但限制单批次最多10条
    uint8_t max_msgs = 10;  // 限制为10条，避免缓冲区过载
    uint8_t msg_count_total = 0;
    
    // 获取动态地址
    uint16_t t44_addr = MXT_GetObjectAddr(44);
    uint16_t t5_addr = MXT_GetObjectAddr(5);
    
    // 检查地址是否有效
    if (t44_addr == 0 || t5_addr == 0) {
      // 地址无效，尝试重新读取对象表
      if (g_num_objects > 0) {
        MXT_ReadObjectTable();
        t44_addr = MXT_GetObjectAddr(44);
        t5_addr = MXT_GetObjectAddr(5);
      }
      if (t44_addr == 0 || t5_addr == 0) {
        return;  // 无法获取有效地址
      }
    }
    
    while (max_msgs-- > 0) {
      // 读取消息计数 (T44动态地址)
      uint8_t msg_count = 0;
      if (MXT_I2C_Read(g_mxt_i2c_addr, t44_addr, &msg_count, 1) != 0) {
        break;  // 读取失败
      }
      
      if (msg_count == 0) {
        break;  // 无消息，退出
      }
      
      // 读取一条消息 (T5动态地址)
      uint8_t msg_data[11];
      if (MXT_I2C_Read(g_mxt_i2c_addr, t5_addr, msg_data, 11) == 0) {
        msg_count_total++;
        
        uint8_t report_id = msg_data[0];
        // 跳过无效消息 (Report ID = 0xFF 表示队列已空)
        if (report_id == 0xFF) {
          break;
        }

        /* 若处于 CHGNO 模式，则从 T100 消息中提取触点信息，供 Mode3 包附加使用 */
        if (g_stream_chgno &&
            report_id >= g_t100_report_id &&
            report_id < (uint8_t)(g_t100_report_id + 18)) {
          uint8_t rid_offset = (uint8_t)(report_id - g_t100_report_id);
          if (rid_offset >= 2) {
            uint8_t touch_id = (uint8_t)(rid_offset - 2);  /* 触点号 0-16 */
            uint8_t status = msg_data[1];
            uint8_t event  = (uint8_t)(status & 0x0F);
            uint16_t x_pos = (uint16_t)(msg_data[2] | (msg_data[3] << 8));
            uint16_t y_pos = (uint16_t)(msg_data[4] | (msg_data[5] << 8));

            TouchAction_t action = TOUCH_ACTION_NONE;
            switch (event) {
              case 1: action = TOUCH_ACTION_MOVE;   break; /* MOVE */
              case 4: action = TOUCH_ACTION_DOWN;   break; /* DOWN: 起点 */
              case 5: action = TOUCH_ACTION_UP;     break; /* UP: 抬起/离开 */
              case 9: action = TOUCH_ACTION_DOWNUP; break; /* DOWNUP: 快速点按 */
              default: break;
            }

            /* 更新最新触点信息（用于队列为空时兜底） */
            g_last_touch.id     = touch_id;
            g_last_touch.x      = x_pos;
            g_last_touch.y      = y_pos;
            g_last_touch.action = action;
            g_last_touch_valid  = 1;

            /* 将触点事件压入队列，供 MXT_SendMode3Packets 依次取出上传 */
            MXT_TouchQueuePush(touch_id, x_pos, y_pos, action);
          }
        }

        /* 文本输出仅在消息输出使能时进行，避免在 CHGNO 模式下占用带宽 */
        if (g_msg_output_enabled) {
          // 仅输出解析后的简洁格式，不再打印 MSG[...] / 十六进制原始数据
          char msg_str[256];
          int pos = 0;

          // 直接由 MXT_ParseMessage 生成目标格式，例如：
          // T100  [TCH1]  X=516 Y=660  STY MOVE
          pos = MXT_ParseMessage(msg_str, pos, sizeof(msg_str), report_id, msg_data);
          pos += snprintf(msg_str + pos, sizeof(msg_str) - pos, "\r\n");
          MSG_BufferWrite(msg_str);
        }
      }
      

      
      // 再次检查CHG引脚，如果已经拉高则退出
      if (HAL_GPIO_ReadPin(CHG_EXTI3_GPIO_Port, CHG_EXTI3_Pin) == GPIO_PIN_SET) {
        break;
      }
    }
    
    // 取消总数汇总行，只保留每条消息本身
  }
}

/**
  * @}
  */

/**
  * @}
  */

/**
  * @brief  刷新消息缓冲区到USB (导出给main.c调用)
  *         循环尝试刷新直到缓冲区空或USB忙，确保结尾数据尽快发出、减少残留
  */
void MXT_FlushMessageBuffer(void)
{
  for (int i = 0; i < 256; i++) {
    if (MSG_BufferFlush() == 0) break;  /* 缓冲区空或USB忙则停止 */
  }
}

/**
  * @brief  处理待处理的命令 (导出给main.c调用)
  */
void MXT_ProcessCommand(void)
{
  ProcessPendingCommand();
}

/**
  * @brief 处理 CFG 控制命令的异步部分
  *        例如：BACKUPNV 期间推迟 UNFREEZE，避免 USB 接收回调阻塞导致 ACK 超时。
  */
void MXT_ProcessControlPending(void)
{
  /* SPI 流模式优先，暂停控制面的 I2C 操作。 */
  if (g_spi_stream_enabled != 0U) {
    return;
  }

  if (!g_unfreeze_pending) return;

  /* 等 BACKUPNV 结束时间窗到点后再真正执行 UNFREEZE */
  if (g_backup_busy && (HAL_GetTick() < g_backup_busy_until_ms)) return;

  /* 确保 T6 地址与触摸对象表有效 */
  if (g_t6_addr == 0 || !g_touch_inited) {
    uint8_t found_addr = MXT_FindI2CAddress();
    if (found_addr == STATUS_NO_DEVICE) {
      g_unfreeze_pending = 0;
      g_backup_busy = 0;
      CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_NO_DEVICE);
      return;
    }
    g_touch_inited = 0;
    MXT_InitTouchScreen();
    if (g_t6_addr == 0) {
      g_unfreeze_pending = 0;
      g_backup_busy = 0;
      CFG_SendResp(CFG_RESP_NACK_CMD, 0, STATUS_NO_DEVICE);
      return;
    }
  }

  uint8_t ctrl = UNFREEZE_COMMAND;
  uint8_t r = MXT_I2C_Write(g_mxt_i2c_addr, (uint16_t)(g_t6_addr + 1), &ctrl, 1);
  if (r == 0) {
    g_bridge_mode = BRIDGE_MODE_STRING;
    g_menu_state = MENU_IDLE;
    g_cfg_rx_len = 0; /* 清理 CFG 重组缓存，避免残片影响后续字符串/协议 */
    g_backup_busy = 0;
    g_unfreeze_pending = 0;
    CFG_SendResp(CFG_RESP_ACK_CMD, 0, STATUS_OK);
  } else {
    g_backup_busy = 0;
    g_unfreeze_pending = 0;
    CFG_SendResp(CFG_RESP_NACK_CMD, 0, r);
  }
}

static void MXT_SPI_StartIT(void)
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
    /* 停止后再次启动时，HAL 可能仍残留 BUSY/OVR；主动中止并重试一次。 */
    (void)HAL_SPI_Abort(&hspi1);
    __HAL_SPI_CLEAR_OVRFLAG(&hspi1);
    if (HAL_SPI_Receive_IT(&hspi1, g_spi_it_rx_buf, SPI_IT_CHUNK_LEN) == HAL_OK) {
      g_spi_it_active = 1;
    }
  }
}

static void MXT_SPI_StopIT(void)
{
  (void)HAL_SPI_Abort(&hspi1);
  __HAL_SPI_CLEAR_OVRFLAG(&hspi1);
  g_spi_it_active = 0;
  g_spi_last_irq_ms = 0U;
}

static void MXT_SPI_USBFlush(void)
{
  SPIUSB_TryFlush();
}

void MXT_ProcessSPICheck(void)
{
  uint16_t local_head;
  uint16_t local_tail;
  uint8_t *chunk;
  uint8_t nss_sample;
  uint8_t b;

  if (!g_spi_it_active) {
    MXT_SPI_StartIT();
    if (!g_spi_it_active) return;
  }

  /* 如果中断长时间没回调，自动重启接收链路 */
  if ((HAL_GetTick() - g_spi_last_irq_ms > 100U)) {
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

    if ((g_spi_stream_enabled != 0U) && (g_spi_stream_mode != 0U) && (g_spi_nss_prev != 0U) && (nss_sample == 0U)) {
      /* 仅 START1/START3 使用 88 88 作为页面标记；raw 模式保持原始字节流。 */
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
    }

    local_tail = (uint16_t)((local_tail + 1U) % SPI_RX_QUEUE_DEPTH);
    g_spi_rx_q_tail = local_tail;
    local_head = g_spi_rx_q_head;
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

  /* 清 OVR，避免后续接收持续卡死 */
  __HAL_SPI_CLEAR_OVRFLAG(hspi);

  if (g_spi_check_requested) {
    MXT_SPI_StartIT();
  }
}

static void SPIUSB_ResetState(uint8_t mode)
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
}

static void SPIUSB_LineEnqueue(const char *s)
{
  while ((*s != '\0') && (g_spi_hex_tx_len < (uint16_t)(sizeof(g_spi_hex_tx_buf) - 1U))) {
    g_spi_hex_tx_buf[g_spi_hex_tx_len++] = *s++;
  }
}

static void SPIUSB_HexEnqueueByte(uint8_t b)
{
  int n;
  char tmp[4];

  n = snprintf(tmp, sizeof(tmp), "%02X ", b);
  if (n <= 0) {
    return;
  }

  if ((g_spi_hex_tx_len + (uint16_t)n) >= (uint16_t)(sizeof(g_spi_hex_tx_buf) - 2U)) {
    SPIUSB_TryFlush();
  }

  if ((g_spi_hex_tx_len + (uint16_t)n) < (uint16_t)sizeof(g_spi_hex_tx_buf)) {
    memcpy(&g_spi_hex_tx_buf[g_spi_hex_tx_len], tmp, (size_t)n);
    g_spi_hex_tx_len += (uint16_t)n;
  }
}

static void SPIUSB_ByteEnqueue(const uint8_t *data, uint16_t len)
{
  if ((data == NULL) || (len == 0U)) {
    return;
  }

  if ((g_spi_hex_tx_len + len) >= (uint16_t)(sizeof(g_spi_hex_tx_buf) - 2U)) {
    SPIUSB_TryFlush();
  }

  if ((g_spi_hex_tx_len + len) < (uint16_t)sizeof(g_spi_hex_tx_buf)) {
    memcpy(&g_spi_hex_tx_buf[g_spi_hex_tx_len], data, len);
    g_spi_hex_tx_len = (uint16_t)(g_spi_hex_tx_len + len);
  }
}

static void SPIUSB_HexEnqueueBytesWithNewline(const uint8_t *data, uint16_t len)
{
  for (uint16_t i = 0U; i < len; i++) {
    SPIUSB_HexEnqueueByte(data[i]);
  }
  SPIUSB_LineEnqueue("\r\n");
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

  if (g_spi_start1_payload_bytes >= 640U) {
    return;
  }

  g_spi_start1_payload_bytes++;
  g_spi_start1_src_row_bytes++;

  /* 源数据每行 40B：跳过每行首 1B，再取后续 32B（byte 2..33）。
   * 这样可去掉行前导填充值(常见为 0x80)，避免整行右移一字节。 */
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
    g_spi_start1_collecting = 0U;
    if ((g_spi_stream_mode == 1U) && (g_spi_start1_row_bytes != 0U)) {
      SPIUSB_LineEnqueue("\r\n");
      g_spi_start1_row_bytes = 0U;
    }
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

  /* 每个像素点为 16bit，按高字节在前、低字节在后输出。 */
  for (uint8_t i = 0U; i < 32U; i += 2U) {
    packet[6U + i] = g_spi_start3_row_buf[i + 1U];
    packet[6U + i + 1U] = g_spi_start3_row_buf[i];
  }

  crc = CRC16_CCITT_FALSE(packet, 38U);
  packet[38] = (uint8_t)((crc >> 8) & 0xFFU);
  packet[39] = (uint8_t)(crc & 0xFFU);

  SPIUSB_ByteEnqueue(packet, 40U);
}

static void SPIUSB_TryFlush(void)
{
  if (g_spi_hex_tx_len == 0U) {
    return;
  }

  if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) {
    return;
  }

  if (CDC_Transmit_FS((uint8_t *)g_spi_hex_tx_buf, g_spi_hex_tx_len) == USBD_OK) {
    g_spi_hex_tx_len = 0U;
  }
}

/**
  * @brief  处理SPI接收完成 (导出给main.c调用) - 暂时禁用
  */
/*
void MXT_ProcessSPIData(void)
{
  if (!g_spi_receiving) {
    return;  // SPI未启动
  }
  
  if (SPI_IsReceiveComplete()) {
    uint8_t data[128];
    uint16_t len = SPI_GetReceivedData(data, 128);
    
    if (len > 0) {
      // 输出SPI数据到USB
      char info[256];
      int pos = snprintf(info, sizeof(info), "SPI RX (%d bytes): ", len);
      
      // 输出十六进制
      for (uint16_t i = 0; i < len && i < 64; i++) {
        pos += snprintf(info + pos, sizeof(info) - pos, "%02X ", data[i]);
      }
      if (len > 64) {
        pos += snprintf(info + pos, sizeof(info) - pos, "...");
      }
      pos += snprintf(info + pos, sizeof(info) - pos, "\r\n");
      
      MSG_BufferWrite(info);
    }
    
    // 重新启动接收
    if (g_spi_receiving) {
      SPI_StartReceive(128);
    }
  }
}
*/
