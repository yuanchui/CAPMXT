#ifndef MXT_I2C_H
#define MXT_I2C_H
#include <stdint.h>
void MXT_DelayUs(uint32_t us);
uint8_t MXT_I2C_Write(uint8_t addr, uint16_t reg, const uint8_t *data, uint16_t len);
uint8_t MXT_I2C_Read(uint8_t addr, uint16_t reg, uint8_t *data, uint16_t len);
uint8_t MXT_I2C_WriteNoReg(uint8_t addr, const uint8_t *data, uint16_t len);
uint8_t MXT_I2C_ReadNoReg(uint8_t addr, uint8_t *data, uint16_t len);
uint8_t MXT_I2C_Probe(uint8_t addr);
uint8_t MXT_FindI2CAddress(void);
uint8_t MXT_GetI2CAddress(void);
void MXT_SetI2CAddress(uint8_t addr);
#endif
