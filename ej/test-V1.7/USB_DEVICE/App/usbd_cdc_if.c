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
  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, UserTxBufferFS, 0);
  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);

  MSG_BufferWrite("\r\n======================================\r\n");
  MSG_BufferWrite("    maXTouch640 USB CDC Interface\r\n");
  MSG_BufferWrite("======================================\r\n");

  MSG_BufferWrite("Scanning I2C address...\r\n");
  uint8_t found_addr = MXT_FindI2CAddress();
  if (found_addr != STATUS_NO_DEVICE) {
    snprintf(MXT_WORK_STR, MXT_WORK_BUF_SIZE, "Found device at I2C address: 0x%02X\r\n", found_addr);
    MSG_BufferWrite(MXT_WORK_STR);
    MSG_BufferWrite("Reading device info...\r\n");
    MXT_InitTouchScreen();
  } else {
    MSG_BufferWrite("ERROR: No maXTouch device found!\r\n");
    MSG_BufferWrite("Please check I2C connections and power.\r\n");
  }

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
}

static int8_t CDC_DeInit_FS(void)
{
  return (USBD_OK);
}

static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length)
{
  (void)pbuf;
  (void)length;
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
}

static int8_t CDC_Receive_FS(uint8_t* Buf, uint32_t *Len)
{
  if(Buf != NULL && *Len > 0)
  {
    if (MXT_PacketUseBridgeBinary(Buf, *Len)) {
      ProcessBridgePacket(Buf, *Len);
    } else {
      ProcessStringCommand(Buf, *Len);
    }
  }

  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, &Buf[0]);
  USBD_CDC_ReceivePacket(&hUsbDeviceFS);
  return (USBD_OK);
}

uint8_t CDC_Transmit_FS(uint8_t* Buf, uint16_t Len)
{
  uint8_t result = USBD_OK;
  USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassData;
  if (hcdc->TxState != 0) {
    return USBD_BUSY;
  }
  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, Buf, Len);
  result = USBD_CDC_TransmitPacket(&hUsbDeviceFS);
  return result;
}
