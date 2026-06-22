/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LED_Pin GPIO_PIN_13
#define LED_GPIO_Port GPIOC
#define CHG_EXTI3_Pin GPIO_PIN_3
#define CHG_EXTI3_GPIO_Port GPIOA
#define SSN_Pin GPIO_PIN_4
#define SSN_GPIO_Port GPIOA
#define SPI1_SCK_Pin GPIO_PIN_5
#define SPI1_SCK_GPIO_Port GPIOA
#define SPI_MOSI_Pin GPIO_PIN_7
#define SPI_MOSI_GPIO_Port GPIOA
#define RST_Pin GPIO_PIN_1
#define RST_GPIO_Port GPIOB
#define IIC_SCL_Pin GPIO_PIN_10
#define IIC_SCL_GPIO_Port GPIOB
#define IIC_SDA_Pin GPIO_PIN_11
#define IIC_SDA_GPIO_Port GPIOB
#define ADDSEL_Pin GPIO_PIN_12
#define ADDSEL_GPIO_Port GPIOB
#define IICMODE_Pin GPIO_PIN_13
#define IICMODE_GPIO_Port GPIOB
#define SYNC_Pin GPIO_PIN_14
#define SYNC_GPIO_Port GPIOB
#define NOISE_IN_Pin GPIO_PIN_15
#define NOISE_IN_GPIO_Port GPIOB
#define USB_EN_Pin GPIO_PIN_15
#define USB_EN_GPIO_Port GPIOA
#define IN_SW_Pin GPIO_PIN_9
#define IN_SW_GPIO_Port GPIOB
#define SSN_OUT_Pin GPIO_PIN_9
#define SSN_OUT_GPIO_Port GPIOA
#define CLK_MON_Pin GPIO_PIN_10
#define CLK_MON_GPIO_Port GPIOA

/* USER CODE BEGIN Private defines */

/*
 * MXT_SSN_PA9_GPIO_OUT：PA9 虚拟 SSN 是否落脚到 GPIO
 *   0（默认）：仅更新 g_ssn_in_gap 等软件变量，PA9 高阻输入
 *   1：PA9 推挽输出，帧间高 / 帧内低（逻辑分析仪可抓波形）
 */
#ifndef MXT_SSN_PA9_GPIO_OUT
#define MXT_SSN_PA9_GPIO_OUT  1
#endif

/* 虚拟 SSN 时序（us）；修改后需全量编译 gpio.o / tim.o */
#ifndef SSN_HOLD_LOW_US
#define SSN_HOLD_LOW_US         6251U  /* 帧内 active 宽度：SPISTART 流模式 PA10 进帧，TIM1 单次定时 */
#endif
#ifndef SSN_GAP_POLL_US
#define SSN_GAP_POLL_US         100U   /* 帧间 GAP 轮询周期（非流模式） */
#endif
#ifndef SSN_LOW_PULL_US
#define SSN_LOW_PULL_US         20U    /* 帧内 MISO 低过此值则提前结束（非流模式，无 TIM 保持时） */
#endif
#ifndef SSN_SPI_IDLE_US
#define SSN_SPI_IDLE_US         2500U  /* 帧内 SPI 空闲过此值则结束（非流模式） */
#endif
#ifndef SSN_STOP_PULLUP_US
#define SSN_STOP_PULLUP_US      250U   /* SPISTOP 队列排空后再等此时间拉高 */
#endif
#ifndef SSN_GAP_MIN_US
#define SSN_GAP_MIN_US          500U   /* 帧间 MISO 低脉宽下限（非流模式进帧） */
#endif
#ifndef SSN_PA10_LOW_MIN_US
#define SSN_PA10_LOW_MIN_US     1000U  /* PA10 低电平最短宽度才触发进帧（流模式） */
#endif

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
