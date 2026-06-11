/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
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
#include "gpio.h"
#include "spi.h"
#include "mxt/mxt_spi_stream.h"
#include "mxt/mxt_state.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins as
        * Analog
        * Input
        * Output
        * EVENT_OUT
        * EXTI
*/
void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  /* USB_EN 默认拉高（枚举前为高，主程序会在100ms后拉低） */
  HAL_GPIO_WritePin(GPIOA, USB_EN_Pin, GPIO_PIN_SET);

  /* RESET 默认高电平；ADDSEL/IICMODE 上拉；IN_SW 默认低 */
  HAL_GPIO_WritePin(GPIOB, RST_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOB, ADDSEL_Pin|IICMODE_Pin, GPIO_PIN_RESET);

  /* ADDSEL and IICMODE set to HIGH for maXTouch640 */
  HAL_GPIO_WritePin(ADDSEL_GPIO_Port, ADDSEL_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(IICMODE_GPIO_Port, IICMODE_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : LED_Pin */
  GPIO_InitStruct.Pin = LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : CHG_EXTI3_Pin - 双边沿中断，跟随CHG电平控制LED */
  GPIO_InitStruct.Pin = CHG_EXTI3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;  /* 双边沿触发 */
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(CHG_EXTI3_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init */
  HAL_NVIC_SetPriority(EXTI3_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI3_IRQn);

  /*Configure GPIO pins :USB_EN_Pin */
  GPIO_InitStruct.Pin = USB_EN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : RST_Pin ADDSEL_Pin IICMODE_Pin IN_SW_Pin */
  GPIO_InitStruct.Pin = RST_Pin|ADDSEL_Pin|IICMODE_Pin|IN_SW_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : SYNC_Pin NOISE_IN_Pin */
  GPIO_InitStruct.Pin = SYNC_Pin|NOISE_IN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* PA9: 虚拟 SSN 输出 — 推挽、无上下拉、高速(50MHz)；帧间 idle 为高 */
  HAL_GPIO_WritePin(SSN_OUT_GPIO_Port, SSN_OUT_Pin, GPIO_PIN_SET);
  GPIO_InitStruct.Pin = SSN_OUT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(SSN_OUT_GPIO_Port, &GPIO_InitStruct);

  /* PA10: MISO 监视 — 双边沿 EXTI + TIM1 判帧；初始化在 MXT_SSN_Init() */
  GPIO_InitStruct.Pin = CLK_MON_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(CLK_MON_GPIO_Port, &GPIO_InitStruct);

}

/* USER CODE BEGIN 2 */

/* PA10(MISO) → PA9 虚拟 SSN（双边沿 EXTI + TIM1 2us 计时）
 *
 *  帧间拉高 PA9：帧间 MISO 低电平 > SSN_GAP_MIN_US(500us) 后变高并确认
 *  帧内拉低 PA9：SPI 停振 > SSN_SPI_IDLE_US，或 MISO 低电平 > SSN_LOW_PULL_US
 */
#define SSN_SAMPLE_US           2U
#define SSN_LOW_PULL_US         20U
#define SSN_SPI_IDLE_US         2500U
#define SSN_STOP_PULLUP_US      250U
#define SSN_GAP_MIN_US          500U
#define SSN_GAP_HIGH_CONFIRM_US 4U

#define SSN_OUT_HIGH()          (GPIOA->BSRR = (uint32_t)SSN_OUT_Pin)
#define SSN_OUT_LOW()           (GPIOA->BSRR = (uint32_t)SSN_OUT_Pin << 16U)
#define SSN_MISO_HIGH()         (((GPIOA->IDR) & CLK_MON_Pin) != 0U)

static volatile uint8_t  g_ssn_in_gap;
static volatile uint8_t  g_ssn_gap_evt;
static volatile uint8_t  g_ssn_active_evt;
static volatile uint8_t  g_ssn_prev_miso;
static volatile uint16_t g_ssn_low_us;
static volatile uint16_t g_ssn_gap_low_us;
static volatile uint8_t  g_ssn_gap_ready;
static volatile uint16_t g_ssn_gap_high_us;
static volatile uint16_t g_ssn_no_spi_us;
static volatile uint8_t  g_ssn_stop_pullup_armed;
static volatile uint32_t g_ssn_frame_enter_cnt;
static volatile uint32_t g_ssn_frame_exit_cnt;

static void MXT_SSN_DelayUs(uint32_t us)
{
  uint32_t cycles = (SystemCoreClock / 1000000U) * us;

  while (cycles-- > 0U) {
    __NOP();
  }
}

static void MXT_SSN_EndActive(void)
{
  if (g_ssn_in_gap != 0U) {
    return;
  }

  SSN_OUT_HIGH();
  g_ssn_in_gap = 1U;
  g_ssn_low_us = 0U;
  g_ssn_gap_low_us = 0U;
  g_ssn_gap_ready = 0U;
  g_ssn_gap_high_us = 0U;
  g_ssn_no_spi_us = 0U;
  g_ssn_gap_evt = 1U;
  g_ssn_frame_exit_cnt++;
  MXT_SPI_QueueGapMarker();
}

static void MXT_SSN_EnterActive(void)
{
  if (g_ssn_in_gap == 0U) {
    return;
  }

  g_ssn_in_gap = 0U;
  g_ssn_low_us = 0U;
  g_ssn_gap_low_us = 0U;
  g_ssn_gap_ready = 0U;
  g_ssn_gap_high_us = 0U;
  SSN_OUT_LOW();
  g_ssn_no_spi_us = 0U;
  g_ssn_active_evt = 1U;
  g_ssn_frame_enter_cnt++;
  MXT_SPI_OnSsnActive();
}

static uint32_t MXT_SSN_Tim1Hz(void)
{
  return HAL_RCC_GetPCLK2Freq();
}

static void MXT_SSN_Tim1Init(void)
{
  uint32_t tim_hz;
  uint32_t psc;

  __HAL_RCC_TIM1_CLK_ENABLE();

  tim_hz = MXT_SSN_Tim1Hz();
  psc = (tim_hz / 1000000U);
  if (psc == 0U) {
    psc = 1U;
  }
  psc -= 1U;

  TIM1->CR1 = 0U;
  TIM1->PSC = (uint16_t)psc;
  TIM1->ARR = (uint16_t)(SSN_SAMPLE_US - 1U);
  TIM1->CNT = 0U;
  TIM1->CCMR1 = 0U;
  TIM1->CCMR2 = 0U;
  TIM1->CCER = 0U;
  TIM1->DIER = TIM_DIER_UIE;
  TIM1->EGR = TIM_EGR_UG;
  TIM1->SR = 0U;

  HAL_NVIC_SetPriority(TIM1_UP_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(TIM1_UP_IRQn);
}

static uint8_t MXT_SSN_SpiWireActive(void)
{
  if (__HAL_SPI_GET_FLAG(&hspi1, SPI_FLAG_RXNE) != RESET) {
    return 1U;
  }
  if (hspi1.State == HAL_SPI_STATE_BUSY_RX) {
    return 1U;
  }
  return 0U;
}

static void MXT_SSN_TryEnterFromGap(uint8_t miso)
{
  if ((g_ssn_in_gap == 0U) || (g_ssn_gap_ready == 0U) || (miso == 0U)) {
    g_ssn_gap_high_us = 0U;
    return;
  }

  if (g_ssn_gap_high_us < 60000U) {
    g_ssn_gap_high_us = (uint16_t)(g_ssn_gap_high_us + SSN_SAMPLE_US);
  }

  if (g_ssn_gap_high_us <= SSN_GAP_HIGH_CONFIRM_US) {
    return;
  }

  MXT_SSN_EnterActive();
}

static void MXT_SSN_Sample(void)
{
  uint8_t miso;

  miso = SSN_MISO_HIGH() ? 1U : 0U;

  if (g_ssn_in_gap != 0U) {
    if (miso == 0U) {
      g_ssn_gap_high_us = 0U;
      if (g_ssn_gap_low_us < 60000U) {
        g_ssn_gap_low_us = (uint16_t)(g_ssn_gap_low_us + SSN_SAMPLE_US);
      }
      if (g_ssn_gap_low_us > SSN_GAP_MIN_US) {
        g_ssn_gap_ready = 1U;
      }
      g_ssn_prev_miso = 0U;
    } else {
      MXT_SSN_TryEnterFromGap(miso);
      if (g_ssn_in_gap != 0U) {
        g_ssn_gap_low_us = 0U;
      }
      g_ssn_prev_miso = 1U;
    }
    return;
  }

  g_ssn_gap_high_us = 0U;

  if (MXT_SSN_SpiWireActive() != 0U) {
    g_ssn_no_spi_us = 0U;
  } else if (g_ssn_no_spi_us < 60000U) {
    g_ssn_no_spi_us = (uint16_t)(g_ssn_no_spi_us + SSN_SAMPLE_US);
  }

  if (g_ssn_no_spi_us > SSN_SPI_IDLE_US) {
    MXT_SSN_EndActive();
    return;
  }

  if (miso != 0U) {
    g_ssn_low_us = 0U;
    return;
  }

  if (g_ssn_low_us < 60000U) {
    g_ssn_low_us = (uint16_t)(g_ssn_low_us + SSN_SAMPLE_US);
  }

  if (g_ssn_low_us > SSN_LOW_PULL_US) {
    MXT_SSN_EndActive();
  }
}

void MXT_SSN_OnMisoEdge(void)
{
  uint8_t miso;

  miso = SSN_MISO_HIGH() ? 1U : 0U;

  if (miso == 0U) {
    if (g_ssn_in_gap == 0U) {
      g_ssn_low_us = 0U;
    }
  } else {
    if ((g_ssn_prev_miso == 0U) && (g_ssn_in_gap != 0U)) {
      MXT_SSN_TryEnterFromGap(miso);
    } else if (g_ssn_in_gap == 0U) {
      g_ssn_low_us = 0U;
    }
  }

  g_ssn_prev_miso = miso;
}

void MXT_SSN_Init(void)
{
  MXT_SSN_Tim1Init();
  g_ssn_in_gap = 1U;
  g_ssn_gap_evt = 0U;
  g_ssn_active_evt = 0U;
  g_ssn_prev_miso = SSN_MISO_HIGH() ? 1U : 0U;
  g_ssn_low_us = 0U;
  g_ssn_gap_low_us = 0U;
  g_ssn_gap_ready = 0U;
  g_ssn_gap_high_us = 0U;
  g_ssn_no_spi_us = 0U;
  g_ssn_frame_enter_cnt = 0U;
  g_ssn_frame_exit_cnt = 0U;
  SSN_OUT_HIGH();

  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

void MXT_SSN_NotifySpiRx(void)
{
  g_ssn_no_spi_us = 0U;
}

void MXT_SSN_ResetForStream(void)
{
  g_ssn_stop_pullup_armed = 0U;
  g_ssn_in_gap = 1U;
  g_ssn_gap_evt = 0U;
  g_ssn_active_evt = 0U;
  g_ssn_low_us = 0U;
  g_ssn_gap_low_us = 0U;
  g_ssn_gap_ready = 0U;
  g_ssn_gap_high_us = 0U;
  g_ssn_no_spi_us = 0U;
  g_ssn_prev_miso = SSN_MISO_HIGH() ? 1U : 0U;
  SSN_OUT_HIGH();
}

void MXT_SSN_GetDebug(uint8_t *in_gap, uint16_t *no_spi_us, uint16_t *low_us,
                      uint32_t *enter_cnt, uint32_t *exit_cnt)
{
  if (in_gap != NULL) {
    *in_gap = g_ssn_in_gap;
  }
  if (no_spi_us != NULL) {
    *no_spi_us = g_ssn_no_spi_us;
  }
  if (low_us != NULL) {
    *low_us = g_ssn_low_us;
  }
  if (enter_cnt != NULL) {
    *enter_cnt = g_ssn_frame_enter_cnt;
  }
  if (exit_cnt != NULL) {
    *exit_cnt = g_ssn_frame_exit_cnt;
  }
}

void MXT_SSN_StopPullup(void)
{
  g_ssn_stop_pullup_armed = 1U;
}

uint8_t MXT_SSN_StopPullupPending(void)
{
  return g_ssn_stop_pullup_armed;
}

uint8_t MXT_SSN_IsSelected(void)
{
  return (g_ssn_in_gap == 0U) ? 1U : 0U;
}

uint8_t MXT_SSN_TakeGapEvent(void)
{
  if (g_ssn_gap_evt == 0U) {
    return 0U;
  }
  g_ssn_gap_evt = 0U;
  return 1U;
}

uint8_t MXT_SSN_TakeActiveEvent(void)
{
  if (g_ssn_active_evt == 0U) {
    return 0U;
  }
  g_ssn_active_evt = 0U;
  return 1U;
}

void MXT_SSN_TimUpIsr(void)
{
  if ((TIM1->SR & TIM_SR_UIF) == 0U) {
    return;
  }
  TIM1->SR = (uint16_t)(TIM1->SR & (uint16_t)(~TIM_SR_UIF));
  MXT_SSN_Sample();
}

void MXT_SSN_TimStart(void)
{
  TIM1->CR1 &= (uint16_t)(~TIM_CR1_CEN);
  TIM1->CNT = 0U;
  TIM1->SR = 0U;
  g_ssn_prev_miso = SSN_MISO_HIGH() ? 1U : 0U;
  TIM1->CR1 |= TIM_CR1_CEN;
}

void MXT_SSN_TimStop(void)
{
  TIM1->CR1 &= (uint16_t)(~TIM_CR1_CEN);
}

void MXT_SSN_Poll(void)
{
  if (g_ssn_stop_pullup_armed == 0U) {
    return;
  }

  if (g_ssn_in_gap != 0U) {
    g_ssn_stop_pullup_armed = 0U;
    return;
  }

  if (g_spi_rx_q_head != g_spi_rx_q_tail) {
    return;
  }

  MXT_SSN_DelayUs(SSN_STOP_PULLUP_US);
  MXT_SSN_EndActive();
  g_ssn_stop_pullup_armed = 0U;
}

/* USER CODE END 2 */
