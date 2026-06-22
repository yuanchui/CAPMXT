#ifndef MXT_BRIDGE_H
#define MXT_BRIDGE_H
#include <stdint.h>
#include <stddef.h>

/** 是否应按 mxt-app / mode0 二进制桥处理（含 0xE0/0x82/0x51/0x80 等） */
uint8_t MXT_PacketUseBridgeBinary(const uint8_t *buf, uint32_t len);
void ProcessBridgePacket(uint8_t *buf, uint32_t len);
void MXT_ProcessControlPending(void);
void MXT_ExportConfigAsTxt(void);
void MXT_ExportConfigAsBin(void);
#endif
