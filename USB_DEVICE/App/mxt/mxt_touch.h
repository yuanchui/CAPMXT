#ifndef MXT_TOUCH_H
#define MXT_TOUCH_H
#include <stdint.h>
#include "mxt_config.h"
void MXT_InitTouchScreen(void);
uint8_t MXT_EnableDebugCtrl2(void);
uint8_t MXT_DisableDebugCtrl2(void);
uint16_t MXT_GetObjectAddr(uint8_t obj_type);
uint8_t MXT_ObjectTableReady(void);
void MXT_EnableOutput(uint8_t enable);
uint8_t MXT_IsOutputEnabled(void);
uint8_t MXT_ReadObjectTable(void);
uint8_t MXT_ReadCompleteDiagnosticFrame(void);
void MXT_OutputDiagnosticData(void);
void MXT_OutputMapAll(void);
void MXT_OutputMap16(void);
void MXT_OutputMap16Transformed(uint8_t rot, uint8_t flip_mask);
void MXT_SendMode3Packets(uint8_t rot, uint8_t flip_mask, uint8_t frame_id);
void MXT_SendSelfCapMode3Packets(uint8_t use_map16, uint8_t frame_id);
void MXT_ReadT100UnknownFields(void);
uint16_t Map16_CalcCRC16(const uint8_t *data, uint16_t length);
uint16_t CRC16_CCITT_FALSE(const uint8_t *data, uint16_t length);
void MXT_TouchQueuePush(uint8_t id, uint16_t x, uint16_t y, TouchAction_t action);
uint8_t MXT_TouchQueuePop(TouchInfo_t *out);
void MXT_TimerDiagnosticRead(void);
#endif
