#ifndef MXT_CMD_H
#define MXT_CMD_H
#include <stdint.h>
void ProcessStringCommand(uint8_t *buf, uint32_t len);
void ProcessPendingCommand(void);
void MXT_ProcessCommand(void);
#endif
