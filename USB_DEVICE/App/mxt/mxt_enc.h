#ifndef MXT_ENC_H
#define MXT_ENC_H

#include <stdint.h>

uint8_t MXT_ENC_Start(uint8_t bl_addr_hint, uint8_t flags, uint16_t total_frames);
uint8_t MXT_ENC_SendFrame(const uint8_t *frame, uint16_t len, uint16_t seq);
uint8_t MXT_ENC_End(uint16_t end_seq);
void MXT_ENC_Abort(void);

#endif /* MXT_ENC_H */
