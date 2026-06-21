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
#include "core_cm3.h"
#include "tim.h"
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

  /* PA9 SSN 推挽输出；PA10 MISO 输入监视 */
  MXT_TIM1_SsnPinGpioEnable();

}

/* USER CODE BEGIN 2 */

/* PA9 GPIO(BSRR); PA10 EXTI+DWT enter frame; DWT 6208us 帧内低电平；GAP 监听 TIM 停止 */
#define SSN_GAP_POLL_US           100U
#define SSN_LOW_PULL_US           20U
#define SSN_SPI_IDLE_US           2500U
#define SSN_HOLD_LOW_US           6210U
#define SSN_STOP_PULLUP_US        250U
#define SSN_GAP_MIN_US            500U
#define SSN_PA10_LOW_MIN_US       1000U

#define SSN_MISO_HIGH()           (((GPIOA->IDR) & CLK_MON_Pin) != 0U)
#define SSN_OUT_HIGH()            (GPIOA->BSRR = (uint32_t)SSN_OUT_Pin)
#define SSN_OUT_LOW()             (GPIOA->BSRR = (uint32_t)SSN_OUT_Pin << 16U)

static uint8_t MXT_SSN_UseStreamTiming(void)
{
  return (uint8_t)((g_spi_stream_enabled != 0U) || (g_spi_check_requested != 0U));
}

static volatile uint8_t  g_ssn_in_gap;
static volatile uint8_t  g_ssn_gap_evt;
static volatile uint8_t  g_ssn_active_evt;
static volatile uint16_t g_ssn_low_us;
static volatile uint16_t g_ssn_gap_low_us;
static volatile uint8_t  g_ssn_gap_ready;
static volatile uint16_t g_ssn_no_spi_us;
static volatile uint8_t  g_ssn_stop_pullup_armed;
static volatile uint32_t g_ssn_frame_enter_cnt;
static volatile uint32_t g_ssn_frame_exit_cnt;
static volatile uint16_t g_ssn_last_dma_pos;
static volatile uint8_t  g_ssn_gap_seen_high;
static volatile uint8_t  g_ssn_tim_running;
static volatile uint8_t  g_ssn_frame_timer;
static volatile uint32_t g_ssn_fall_cyc;
static volatile uint32_t g_ssn_hold_end_cyc;
static uint32_t          g_ssn_cyc_per_us;

static void MXT_SSN_FrameHoldService(void);

static void MXT_SSN_DwtInit(void)
{
  g_ssn_cyc_per_us = SystemCoreClock / 1000000U;
  if (g_ssn_cyc_per_us == 0U) {
    g_ssn_cyc_per_us = 1U;
  }
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void MXT_SSN_MisoExtiEnable(uint8_t on)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Pin = CLK_MON_Pin;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  if (on != 0U) {
    gpio.Mode = GPIO_MODE_IT_RISING_FALLING;
    HAL_GPIO_Init(CLK_MON_GPIO_Port, &gpio);
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
    __HAL_GPIO_EXTI_CLEAR_IT(CLK_MON_Pin);
  } else {
    gpio.Mode = GPIO_MODE_INPUT;
    HAL_GPIO_Init(CLK_MON_GPIO_Port, &gpio);
    __HAL_GPIO_EXTI_CLEAR_IT(CLK_MON_Pin);
  }
}

static void MXT_SSN_DelayUs(uint32_t us)
{
  uint32_t cycles = (SystemCoreClock / 1000000U) * us;

  while (cycles-- > 0U) {
    __NOP();
  }
}

static void MXT_SSN_GapReset(void)
{
  g_ssn_gap_low_us = 0U;
  g_ssn_gap_ready = 0U;
  g_ssn_gap_seen_high = 0U;
}

static void MXT_SSN_ListenBegin(void)
{
  MXT_SSN_GapReset();
  g_ssn_fall_cyc = DWT->CYCCNT;
  MXT_TIM1_SsnListenStart();
  MXT_SSN_MisoExtiEnable(1U);
}

static void MXT_SSN_GapPollBegin(void)
{
  MXT_TIM1_SsnGapPollStop();
  MXT_SSN_GapReset();
  MXT_TIM1_SsnGapPollStart();
}

static void MXT_SSN_EndActive(void)
{
  if (g_ssn_in_gap != 0U) {
    return;
  }

  SSN_OUT_HIGH();
  g_ssn_frame_timer = 0U;
  g_ssn_in_gap = 1U;
  g_ssn_low_us = 0U;
  g_ssn_no_spi_us = 0U;
  g_ssn_gap_evt = 1U;
  g_ssn_frame_exit_cnt++;
  MXT_SPI_OnSsnGap();
  if (g_ssn_tim_running != 0U) {
    if (MXT_SSN_UseStreamTiming() != 0U) {
      MXT_SSN_ListenBegin();
    } else {
      MXT_SSN_GapPollBegin();
    }
  }
}

static void MXT_SSN_EnterActive(void)
{
  if (g_ssn_in_gap == 0U) {
    return;
  }

  SSN_OUT_LOW();
  g_ssn_in_gap = 0U;
  g_ssn_low_us = 0U;
  g_ssn_no_spi_us = 0U;
  MXT_SSN_GapReset();
  MXT_SSN_MisoExtiEnable(0U);
  if (MXT_SSN_UseStreamTiming() == 0U) {
    MXT_TIM1_SsnGapPollStart();
  } else {
    MXT_TIM1_SsnGapPollStop();
  }
  g_ssn_last_dma_pos = MXT_SPI_GetDmaWritePos();
  MXT_SPI_OnSsnActive();
  if ((g_spi_stream_enabled != 0U) && (g_spi_stream_mode != 0U)) {
    MXT_SPI_QueueStartMarker();
  }
  g_ssn_active_evt = 1U;
  g_ssn_frame_enter_cnt++;
}

static void MXT_SSN_StartFrameLow(void)
{
  if (g_ssn_in_gap == 0U) {
    return;
  }

  SSN_OUT_LOW();
  g_ssn_in_gap = 0U;
  g_ssn_low_us = 0U;
  g_ssn_no_spi_us = 0U;
  MXT_SSN_GapReset();
  MXT_SSN_MisoExtiEnable(0U);
  MXT_TIM1_SsnGapPollStop();
  g_ssn_last_dma_pos = MXT_SPI_GetDmaWritePos();
  MXT_SPI_OnSsnActive();
  if ((g_spi_stream_enabled != 0U) && (g_spi_stream_mode != 0U)) {
    MXT_SPI_QueueStartMarker();
  }
  g_ssn_active_evt = 1U;
  g_ssn_frame_enter_cnt++;
  g_ssn_frame_timer = 1U;
  g_ssn_hold_end_cyc = DWT->CYCCNT + (uint32_t)SSN_HOLD_LOW_US * g_ssn_cyc_per_us;
}

void MXT_SSN_OnMisoEdge(void)
{
  uint32_t low_us;

  if (g_ssn_tim_running == 0U) {
    return;
  }

  if (SSN_MISO_HIGH()) {
    if ((g_ssn_in_gap == 0U) || (MXT_SSN_UseStreamTiming() == 0U)) {
      return;
    }
    low_us = (DWT->CYCCNT - g_ssn_fall_cyc) / g_ssn_cyc_per_us;
    if (low_us >= SSN_PA10_LOW_MIN_US) {
      MXT_SSN_StartFrameLow();
    }
  } else {
    g_ssn_fall_cyc = DWT->CYCCNT;
  }
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
    return;
  }

  MXT_SSN_EnterActive();
}

static void MXT_SSN_ActivePollTick(void)
{
  uint8_t miso;

  if (MXT_SSN_UseStreamTiming() != 0U) {
    return;
  }

  miso = SSN_MISO_HIGH() ? 1U : 0U;
  if (MXT_SSN_SpiWireActive() != 0U) {
    g_ssn_no_spi_us = 0U;
  } else if (g_ssn_no_spi_us < 60000U) {
    g_ssn_no_spi_us = (uint16_t)(g_ssn_no_spi_us + SSN_GAP_POLL_US);
  }
  if (g_ssn_no_spi_us > SSN_SPI_IDLE_US) {
    MXT_SSN_EndActive();
    return;
  }

  if (miso == 0U) {
    if (g_ssn_low_us < 60000U) {
      g_ssn_low_us = (uint16_t)(g_ssn_low_us + SSN_GAP_POLL_US);
    }
    if (g_ssn_low_us > SSN_LOW_PULL_US) {
      MXT_SSN_EndActive();
    }
  } else {
    g_ssn_low_us = 0U;
  }
}

static void MXT_SSN_GapPollTick(void)
{
  uint8_t miso;

  if (g_ssn_in_gap == 0U) {
    return;
  }
  miso = SSN_MISO_HIGH() ? 1U : 0U;

  if (MXT_SSN_UseStreamTiming() != 0U) {
    return;
  }

  if (miso == 0U) {
    if (g_ssn_gap_low_us < 60000U) {
      g_ssn_gap_low_us = (uint16_t)(g_ssn_gap_low_us + SSN_GAP_POLL_US);
    }
    if (g_ssn_gap_low_us >= SSN_GAP_MIN_US) {
      g_ssn_gap_ready = 1U;
    }
  } else {
    if (g_ssn_gap_ready != 0U) {
      g_ssn_gap_seen_high = 1U;
    }
    g_ssn_gap_low_us = 0U;
    MXT_SSN_TryEnterFromGap(miso);
  }
}

void MXT_SSN_Init(void)
{
  g_ssn_in_gap = 1U;
  g_ssn_gap_evt = 0U;
  g_ssn_active_evt = 0U;
  g_ssn_low_us = 0U;
  g_ssn_no_spi_us = 0U;
  g_ssn_frame_enter_cnt = 0U;
  g_ssn_frame_exit_cnt = 0U;
  g_ssn_tim_running = 0U;
  g_ssn_frame_timer = 0U;
  MXT_SSN_GapReset();
  MXT_SSN_DwtInit();
  MXT_TIM1_SsnHwInit();
  MXT_TIM1_SsnPinGpioEnable();
  SSN_OUT_HIGH();
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
  g_ssn_no_spi_us = 0U;
  g_ssn_frame_timer = 0U;
  MXT_SSN_GapReset();
  g_ssn_last_dma_pos = MXT_SPI_GetDmaWritePos();
  MXT_TIM1_SsnActiveWindowStop();
  MXT_TIM1_SsnGapPollStop();
  MXT_SSN_MisoExtiEnable(0U);
  SSN_OUT_HIGH();
  if (MXT_SSN_UseStreamTiming() != 0U) {
    MXT_SSN_ListenBegin();
  } else {
    MXT_SSN_GapPollBegin();
  }
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

void MXT_SSN_TimStart(void)
{
  MXT_TIM1_SsnPinGpioEnable();
  SSN_OUT_HIGH();
  g_ssn_tim_running = 1U;
  if (MXT_SSN_UseStreamTiming() != 0U) {
    MXT_SSN_ListenBegin();
  } else {
    MXT_SSN_GapPollBegin();
  }
}

void MXT_SSN_TimStop(void)
{
  g_ssn_tim_running = 0U;
  g_ssn_frame_timer = 0U;
  MXT_TIM1_SsnActiveWindowStop();
  MXT_TIM1_SsnGapPollStop();
  MXT_SSN_MisoExtiEnable(0U);
  MXT_TIM1_SsnCounterStop();
  MXT_TIM1_SsnPinGpioEnable();
  SSN_OUT_HIGH();
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance != TIM1) {
    return;
  }

  if (g_ssn_in_gap != 0U) {
    MXT_SSN_GapPollTick();
  } else {
    MXT_SSN_ActivePollTick();
  }
}

static void MXT_SSN_FrameHoldService(void)
{
  if (g_ssn_frame_timer == 0U) {
    return;
  }
  if (g_ssn_in_gap != 0U) {
    return;
  }
  if ((int32_t)(DWT->CYCCNT - g_ssn_hold_end_cyc) >= 0) {
    MXT_SSN_EndActive();
  }
}

void MXT_SSN_Poll(void)
{
  MXT_SSN_FrameHoldService();

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
