#ifndef MXT_BRIDGE_H
#define MXT_BRIDGE_H
#include <stdint.h>
void ProcessBridgePacket(uint8_t *buf, uint32_t len);
void MXT_ProcessControlPending(void);
void MXT_ExportConfigAsTxt(void);
void MXT_ExportConfigAsBin(void);
#endif
