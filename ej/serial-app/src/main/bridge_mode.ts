/** MCU 桥接模式切换 — 使用 mode1 字符串命令封装（与 mxt_cmd.c MODE0/MODE1 一致） */

export const MODE0_TEXT = 'mode0\r\n';
export const MODE1_TEXT = 'mode1\r\n';

export function bytesSwitchToMode0(): Uint8Array {
  return Uint8Array.from(Buffer.from(MODE0_TEXT, 'ascii'));
}

export function bytesSwitchToMode1(): Uint8Array {
  return Uint8Array.from(Buffer.from(MODE1_TEXT, 'ascii'));
}
