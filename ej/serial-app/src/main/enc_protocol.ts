/**
 * ENCWRITE 协议常量 — 须与 USB_DEVICE/App/mxt/mxt_config.h 保持一致。
 */
export const ENC_PROTOCOL_VERSION = 0x01;
export const ENC_START_CMD = 0xB0;
export const ENC_FRAME_CMD = 0xB1;
export const ENC_END_CMD = 0xB2;
export const ENC_RESP_ACK_CMD = 0xB3;
export const ENC_RESP_NACK_CMD = 0xB4;
export const ENC_MAX_FRAME_BYTES = 276;

/** 默认 Bootloader 7-bit I2C（mXT640UD ADDR_SEL=High → 0x27）；0 表示 MCU 自动探测 */
export const ENC_DEFAULT_BL_ADDR = 0x27;

export const ENC_FLAG_SKIP_ENTER_BOOTLOADER = 0x01;

/** 普通 ENC 帧 ACK 等待（ms） */
export const ENC_ACK_TIMEOUT_MS = 60_000;
/** 大收尾帧 L≥210（258B 包体需多笔 I2C + CRC 轮询） */
export const ENC_ACK_TIMEOUT_TAIL_LARGE_MS = 90_000;
/** 最后一帧（内嵌复位，enc.txt 阶段 F：0x04 后 0x27 NAK，不等 0xA0） */
export const ENC_ACK_TIMEOUT_LAST_FRAME_MS = 120_000;
/** ENC END / 芯片复位后回字符串模式 */
export const ENC_END_ACK_TIMEOUT_MS = 45_000;
/** 末帧触发复位后，等待应用模式 0x4B 就绪（enc.txt 阶段 F） */
export const ENC_POST_RESET_SETTLE_MS = 3500;
export const ENC_POST_VERIFY_TIMEOUT_MS = 45_000;
/** 末帧复位后字符串命令/USB 写入超时（避免 3s 误判） */
export const ENC_POST_STRING_IO_TIMEOUT_MS = 20_000;

/** 按 enc.txt 帧长与序号选择 Host ACK 超时 */
export function encFrameAckTimeoutMs(frameLen: number, seq: number, totalFrames: number): number {
  if (totalFrames > 0 && seq >= totalFrames) return ENC_ACK_TIMEOUT_LAST_FRAME_MS;
  if (frameLen >= 210) return ENC_ACK_TIMEOUT_TAIL_LARGE_MS;
  return ENC_ACK_TIMEOUT_MS;
}
