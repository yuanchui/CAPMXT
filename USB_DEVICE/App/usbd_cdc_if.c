/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usbd_cdc_if.c
  * @brief          : Usb device for Virtual Com Port.
  ******************************************************************************
  */
/* USER CODE END Header */

#include "usbd_cdc_if.h"
#include "mxt/mxt_config.h"
#include "mxt/mxt_state.h"
#include "mxt/mxt_i2c.h"
#include "mxt/mxt_usb_io.h"
#include "mxt/mxt_bridge.h"
#include "mxt/mxt_cmd.h"
#include "mxt/mxt_touch.h"
#include "mxt/mxt_work.h"
#include <stdio.h>

uint8_t UserRxBufferFS[APP_RX_DATA_SIZE];
uint8_t UserTxBufferFS[APP_TX_DATA_SIZE];

uint8_t g_matrix_x_size = 0;
uint8_t g_matrix_y_size = 0;

extern USBD_HandleTypeDef hUsbDeviceFS;

static int8_t CDC_Init_FS(void);
static int8_t CDC_DeInit_FS(void);
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length);
static int8_t CDC_Receive_FS(uint8_t* pbuf, uint32_t *Len);

USBD_CDC_ItfTypeDef USBD_Interface_fops_FS =
{
  CDC_Init_FS,
  CDC_DeInit_FS,
  CDC_Control_FS,
  CDC_Receive_FS
};

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
    snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "Found device at I2C address: 0x%02X\r\n", found_addr);
    MSG_BufferWrite(MXT_WORK_STR);

    /* 读取设备信息 */
    MSG_BufferWrite("Reading device info...\r\n");
    MXT_InitTouchScreen();
  } else {
    MSG_BufferWrite("ERROR: No maXTouch device found!\r\n");
    MSG_BufferWrite("Please check I2C connections and power.\r\n");
  }

  /* 显示配置信息 */
  if (g_matrix_x_size > 0 && g_matrix_y_size > 0) {
    snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "Matrix Size: X=%d, Y=%d\r\n", g_matrix_x_size, g_matrix_y_size);
    MSG_BufferWrite(MXT_WORK_STR);
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


static int8_t CDC_DeInit_FS(void)
{
  /* USER CODE BEGIN 4 */
  return (USBD_OK);
  /* USER CODE END 4 */
}


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


static int8_t CDC_Receive_FS(uint8_t* Buf, uint32_t *Len)
{
  /* USER CODE BEGIN 6 */
  if(Buf != NULL && *Len > 0)
  {
    if (g_bridge_mode == BRIDGE_MODE_BINARY) {
      ProcessBridgePacket(Buf, *Len);
    } else {
      ProcessStringCommand(Buf, *Len);
    }
  }
  
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, &Buf[0]);
  USBD_CDC_ReceivePacket(&hUsbDeviceFS);
  return (USBD_OK);
  /* USER CODE END 6 */
}


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

