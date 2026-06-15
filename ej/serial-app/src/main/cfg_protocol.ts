/**
 * CFGWRITE/CFGREAD 协议常量 — 须与 USB_DEVICE/App/mxt/mxt_config.h 保持一致。
 *
 * CFG_MAX_OBJECTS_MCU：MCU RAM 中对象元数据表预留槽位上限。
 * 上位机按各设备 xcfg 实际解析出的对象数发送（可少于上限），以兼容不同矩阵/对象表规模。
 */
export const CFG_PROTOCOL_VERSION = 0x01;
export const CFG_MAX_OBJECTS_MCU = 128;
export const CFG_MAX_CHUNK = 256;

export const CFGWRITE_START_CMD = 0xD0;
export const CFGWRITE_CHUNK_CMD = 0xD1;
export const CFGWRITE_END_CMD = 0xD2;
export const CFG_RESP_ACK_CMD = 0xD3;
export const CFG_RESP_NACK_CMD = 0xD4;
export const CFGREAD_DATA_CMD = 0xE1;
export const CFGREAD_END_CMD = 0xE2;

export const STATUS_OK = 0x00;
export const STATUS_OBJ_DONE = 0x10;
export const STATUS_ADDR_NACK = 0x01;
export const STATUS_NO_DEVICE = 0x81;

export const UNFREEZE_COMMAND = 0x11;
export const BACKUPNV_COMMAND = 0x55;

/** START 帧最大字节数（含 CRC）：12 + 4*N + 2 */
export function cfgStartFrameMaxBytes(objectCount: number): number {
  return 12 + objectCount * 4 + 2;
}

export function assertObjectCountWithinMcuLimit(objectCount: number): void {
  if (objectCount <= 0) {
    throw new Error('xcfg 中未找到可写入的对象');
  }
  if (objectCount > CFG_MAX_OBJECTS_MCU) {
    throw new Error(
      `xcfg 对象数 ${objectCount} 超过 MCU 预留上限 ${CFG_MAX_OBJECTS_MCU}（请升级固件或精简配置）`
    );
  }
}
