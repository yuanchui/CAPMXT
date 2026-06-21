/** MCU 桥接模式切换 — 与 mxt_cmd.c / mxt-app serial_device.c 一致
 *
 * 协议分层:
 *   mode1 (字符串) — PC 发 "mode0\\r\\n" / "mode1\\r\\n"，MCU 回 "OK: ..." 等文本
 *   mode0 (二进制) — mxt-app 纯 I2C 桥协议 (0xE0 / 0x01 0x51 ...)，MCU 转 I2C 触摸芯片
 *
 * 串口 + xcfg/备份/读写寄存器: serial-app → mxt-app.exe → TCP 串口代理 → mode0 → 二进制 → MCU → I2C
 * 勿在应用层重复实现 mxt-app 的 I2C 协议；MCU 字符串调试仍用 mode1。
 */

export const MODE0_TEXT = 'mode0\r\n';
export const MODE1_TEXT = 'mode1\r\n';
export const BRIDGEBIN_TEXT = 'BRIDGEBIN\r\n';

export function bytesSwitchToMode0(): Uint8Array {
  return Uint8Array.from(Buffer.from(MODE0_TEXT, 'ascii'));
}

export function bytesSwitchToMode1(): Uint8Array {
  return Uint8Array.from(Buffer.from(MODE1_TEXT, 'ascii'));
}

/** 字符串命令切换二进制桥（与 mode0 等效，日志更清晰） */
export function bytesSwitchToBinaryBridge(): Uint8Array {
  return Uint8Array.from(Buffer.from(BRIDGEBIN_TEXT, 'ascii'));
}

/** 二进制桥 → 字符串模式（与 mxt_bridge.c 中 02 01 10 20 序列一致） */
export const MODE_SWITCH_TO_STRING_BYTES = Uint8Array.from([0x02, 0x01, 0x10, 0x20]);
