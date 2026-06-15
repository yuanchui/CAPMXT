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
