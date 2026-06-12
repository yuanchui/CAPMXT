#include "dma.h"

/**
  * @brief  DMA controller clock + NVIC（参考 aaaa/Core/Src/dma.c）
  */
void MX_DMA_Init(void)
{
  __HAL_RCC_DMA1_CLK_ENABLE();

  HAL_NVIC_SetPriority(DMA1_Channel2_IRQn, 3, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel2_IRQn);
}
