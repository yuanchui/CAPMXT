#ifndef MXT_ENC_H
#define MXT_ENC_H

#include <stdint.h>

uint8_t MXT_ENC_Start(uint8_t bl_addr_hint, uint8_t flags, uint16_t total_frames);
uint8_t MXT_ENC_SendFrame(const uint8_t *frame, uint16_t len, uint16_t seq);
uint8_t MXT_ENC_End(uint16_t end_seq);
void MXT_ENC_Abort(void);

/* 字符串命令阶段：T6 复位 → 扫描 BL 地址 → 解锁，再走二进制 ENC_FRAME */
uint8_t MXT_ENC_ForceResetToBootloader(uint8_t *out_app, uint16_t *out_t6, uint8_t *out_bl, uint8_t *out_status);
uint8_t MXT_ENC_FindBootloader(uint8_t bl_hint, uint8_t *out_bl, uint8_t *out_status);
uint8_t MXT_ENC_PrepareEnterBootloader(uint8_t bl_hint, uint8_t *out_app, uint8_t *out_bl, uint8_t *out_status);
uint8_t MXT_ENC_PrepareUnlock(uint8_t *out_status);
uint8_t MXT_ENC_IsPrepared(void);
void MXT_ENC_ClearPrepared(void);

#endif /* MXT_ENC_H */
