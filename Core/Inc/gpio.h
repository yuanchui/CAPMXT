/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.h
  * @brief   This file contains all the function prototypes for
  *          the gpio.c file
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
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __GPIO_H__
#define __GPIO_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

void MX_GPIO_Init(void);

/* USER CODE BEGIN Prototypes */
void MXT_SSN_Init(void);
void MXT_SSN_ResetForStream(void);
void MXT_SSN_StopPullup(void);
uint8_t MXT_SSN_StopPullupPending(void);
uint8_t MXT_SSN_IsSelected(void);
void MXT_SSN_NotifySpiRx(void);
void MXT_SSN_OnMisoEdge(void);
uint8_t MXT_SSN_TakeGapEvent(void);
uint8_t MXT_SSN_TakeActiveEvent(void);
void MXT_SSN_TimUpIsr(void);
void MXT_SSN_TimStart(void);
void MXT_SSN_TimStop(void);
void MXT_SSN_Poll(void);
void MXT_SSN_GetDebug(uint8_t *in_gap, uint16_t *no_spi_us, uint16_t *low_us,
                      uint32_t *enter_cnt, uint32_t *exit_cnt);
/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif
#endif /*__ GPIO_H__ */

