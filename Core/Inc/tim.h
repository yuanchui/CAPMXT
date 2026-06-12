#ifndef __TIM_H__
#define __TIM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

extern TIM_HandleTypeDef htim1;

void MX_TIM1_Init(void);
void MXT_TIM1_SsnHwInit(void);
void MXT_TIM1_SsnPinGpioEnable(void);
void MXT_TIM1_SsnListenStart(void);
void MXT_TIM1_SsnGapPollStart(void);
void MXT_TIM1_SsnActiveWindowStart(uint32_t low_us);
void MXT_TIM1_SsnActiveWindowStop(void);
void MXT_TIM1_SsnGapPollStop(void);
void MXT_TIM1_SsnCounterStop(void);
void MXT_TIM1_SsnIcArmFalling(void);
void MXT_TIM1_SsnIcArmRising(void);
uint8_t MXT_TIM1_SsnIcIsFalling(void);

#ifdef __cplusplus
}
#endif

#endif /* __TIM_H__ */
