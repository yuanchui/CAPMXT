#include "tim.h"

/* TIM1 1MHz：仅用于 SSN 采样/帧定时；PA9 由普通 GPIO 推拉 */
#define SSN_GAP_POLL_US        100U

TIM_HandleTypeDef htim1;

static TIM_IC_InitTypeDef s_ic3;

void MX_TIM1_Init(void)
{
  uint32_t tim_clk;
  uint32_t psc;

  tim_clk = HAL_RCC_GetPCLK2Freq();
  psc = tim_clk / 1000000U;
  if (psc == 0U) {
    psc = 1U;
  }
  psc -= 1U;

  htim1.Instance = TIM1;
  htim1.Init.Prescaler = (uint16_t)psc;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 0xFFFFU;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK) {
    Error_Handler();
  }
}

void MXT_TIM1_SsnHwInit(void)
{
  s_ic3.ICPolarity = TIM_ICPOLARITY_FALLING;
  s_ic3.ICSelection = TIM_ICSELECTION_DIRECTTI;
  s_ic3.ICPrescaler = TIM_ICPSC_DIV1;
  s_ic3.ICFilter = 0U;
  if (HAL_TIM_IC_Init(&htim1) != HAL_OK) {
    Error_Handler();
  }
  if (HAL_TIM_IC_ConfigChannel(&htim1, &s_ic3, TIM_CHANNEL_3) != HAL_OK) {
    Error_Handler();
  }
}

void MXT_TIM1_SsnGapPollStart(void)
{
  (void)HAL_TIM_Base_Stop(&htim1);
  __HAL_TIM_DISABLE_IT(&htim1, TIM_IT_CC2);
  __HAL_TIM_SET_AUTORELOAD(&htim1, (uint16_t)(SSN_GAP_POLL_US - 1U));
  __HAL_TIM_SET_COUNTER(&htim1, 0U);
  __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_UPDATE);
  __HAL_TIM_ENABLE_IT(&htim1, TIM_IT_UPDATE);
  (void)HAL_TIM_Base_Start_IT(&htim1);
}

void MXT_TIM1_SsnActiveWindowStart(uint32_t hold_us)
{
  uint32_t ticks;

  /* 单次 UPDATE：帧内低电平维持 hold_us，到期 1 次中断 */
  if (hold_us == 0U) {
    hold_us = 1U;
  }
  if (hold_us > 65000U) {
    hold_us = 65000U;
  }
  ticks = hold_us - 1U;

  (void)HAL_TIM_Base_Stop_IT(&htim1);
  __HAL_TIM_DISABLE_IT(&htim1, TIM_IT_CC2);
  __HAL_TIM_SET_PRESCALER(&htim1, (uint32_t)(HAL_RCC_GetPCLK2Freq() / 1000000U) - 1U);
  __HAL_TIM_SET_AUTORELOAD(&htim1, (uint16_t)ticks);
  __HAL_TIM_SET_COUNTER(&htim1, 0U);
  __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_UPDATE);
  __HAL_TIM_ENABLE_IT(&htim1, TIM_IT_UPDATE);
  (void)HAL_TIM_Base_Start_IT(&htim1);
}

void MXT_TIM1_SsnActiveWindowStop(void)
{
  (void)HAL_TIM_Base_Stop_IT(&htim1);
  __HAL_TIM_SET_AUTORELOAD(&htim1, 0xFFFFU);
}

void MXT_TIM1_SsnListenStart(void)
{
  MXT_TIM1_SsnGapPollStop();
  (void)HAL_TIM_Base_Stop(&htim1);
}

void MXT_TIM1_SsnPinGpioEnable(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  HAL_GPIO_WritePin(SSN_OUT_GPIO_Port, SSN_OUT_Pin, GPIO_PIN_SET);
  gpio.Pin = SSN_OUT_Pin;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(SSN_OUT_GPIO_Port, &gpio);

  gpio.Pin = CLK_MON_Pin;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(CLK_MON_GPIO_Port, &gpio);
}

void MXT_TIM1_SsnGapPollStop(void)
{
  (void)HAL_TIM_Base_Stop_IT(&htim1);
  __HAL_TIM_SET_AUTORELOAD(&htim1, 0xFFFFU);
}

void MXT_TIM1_SsnCounterStop(void)
{
  MXT_TIM1_SsnGapPollStop();
  (void)HAL_TIM_Base_Stop(&htim1);
  __HAL_TIM_SET_COUNTER(&htim1, 0U);
}

void MXT_TIM1_SsnIcArmFalling(void)
{
  TIM1->CCER = (TIM1->CCER & ~TIM_CCER_CC3P) | TIM_CCER_CC3P;
}

void MXT_TIM1_SsnIcArmRising(void)
{
  TIM1->CCER &= ~TIM_CCER_CC3P;
}

uint8_t MXT_TIM1_SsnIcIsFalling(void)
{
  return ((TIM1->CCER & TIM_CCER_CC3P) != 0U) ? 1U : 0U;
}

void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *htim_base)
{
  if (htim_base->Instance == TIM1) {
    __HAL_RCC_TIM1_CLK_ENABLE();
    HAL_NVIC_SetPriority(TIM1_UP_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM1_UP_IRQn);
    HAL_NVIC_SetPriority(TIM1_CC_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM1_CC_IRQn);
  }
}

void HAL_TIM_Base_MspDeInit(TIM_HandleTypeDef *htim_base)
{
  if (htim_base->Instance == TIM1) {
    __HAL_RCC_TIM1_CLK_DISABLE();
    HAL_NVIC_DisableIRQ(TIM1_UP_IRQn);
    HAL_NVIC_DisableIRQ(TIM1_CC_IRQn);
  }
}

void HAL_TIM_IC_MspInit(TIM_HandleTypeDef *htim_ic)
{
  if (htim_ic->Instance == TIM1) {
    __HAL_RCC_TIM1_CLK_ENABLE();
  }
}
