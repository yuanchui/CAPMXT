#include "mxt_i2c.h"
#include "mxt_state.h"
#include "mxt_config.h"
#include "main.h"
#include "i2c.h"
#include "stm32f1xx_hal_i2c.h"

void MXT_DelayUs(uint32_t us)
{
  /* 估算每个空循环的大致周期数，这里给一个保守系数 */
  uint32_t cycles = (SystemCoreClock / 1000000U) * us / 5U;
  while (cycles--) {
    __NOP();
  }
}


uint8_t MXT_I2C_Probe(uint8_t addr)
{
    return HAL_I2C_IsDeviceReady(&hi2c2, (uint16_t)(addr << 1), 3, I2C_TIMEOUT_MS) == HAL_OK ? 0 : 1;
}


uint8_t MXT_FindI2CAddress(void)
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
    if (MXT_I2C_Probe(MXT_I2C_ADDR_BL_MXT640) == 0) {
        g_mxt_i2c_addr = MXT_I2C_ADDR_BL_MXT640;
        return MXT_I2C_ADDR_BL_MXT640;
    }
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


uint8_t MXT_I2C_Write(uint8_t addr, uint16_t reg, const uint8_t *data, uint16_t len)
{
    if (HAL_I2C_Mem_Write(&hi2c2, addr << 1, MXT_MEM_ADD(reg), I2C_MEMADD_SIZE_16BIT, (uint8_t*)data, len, I2C_TIMEOUT_MS) != HAL_OK) {
        return STATUS_ADDR_NACK;
    }
    return 0;
}


uint8_t MXT_I2C_Read(uint8_t addr, uint16_t reg, uint8_t *data, uint16_t len)
{
    if (HAL_I2C_Mem_Read(&hi2c2, addr << 1, MXT_MEM_ADD(reg), I2C_MEMADD_SIZE_16BIT, data, len, I2C_TIMEOUT_MS) != HAL_OK) {
        return STATUS_ADDR_NACK;
    }
    return 0;
}


uint8_t MXT_I2C_WriteNoReg(uint8_t addr, const uint8_t *data, uint16_t len)
{
    if (HAL_I2C_Master_Transmit(&hi2c2, (uint16_t)(addr << 1), (uint8_t*)data, len, I2C_TIMEOUT_MS) != HAL_OK) {
        return STATUS_ADDR_NACK;
    }
    return 0;
}


uint8_t MXT_I2C_ReadNoReg(uint8_t addr, uint8_t *data, uint16_t len)
{
    if (HAL_I2C_Master_Receive(&hi2c2, (uint16_t)(addr << 1), data, len, I2C_TIMEOUT_MS) != HAL_OK) {
        return STATUS_ADDR_NACK;
    }
    return 0;
}


uint8_t MXT_GetI2CAddress(void)
{
    return g_mxt_i2c_addr;
}


void MXT_SetI2CAddress(uint8_t addr)
{
    g_mxt_i2c_addr = addr;
}

