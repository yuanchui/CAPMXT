/** 将 mode0/mode1 与 I2C 桥二进制帧解析为终端可读字符串（与 MCU mxt_bridge.c 一致） */

export type BridgeLogDirection = 'tx' | 'rx' | 'info';

const REPORT_ID = 0x01;
const IIC_DATA_1 = 0x51;
const CMD_FIND_IIC_ADDRESS = 0xe0;
const CMD_READ_PINS = 0x82;

function isMostlyText(buf: Buffer): boolean {
  if (buf.length === 0) return true;
  let printable = 0;
  for (let i = 0; i < buf.length; i++) {
    const b = buf[i];
    if (b === 0x0d || b === 0x0a || b === 0x09 || (b >= 0x20 && b <= 0x7e)) printable++;
  }
  return printable >= buf.length * 0.9;
}

function statusName(code: number): string {
  switch (code) {
    case 0x00: return 'OK';
    case 0x01: return 'ADDR_NACK';
    case 0x04: return 'WRITE_OK';
    case 0x10: return 'OBJ_DONE';
    case 0x81: return 'NO_DEVICE';
    default: return `0x${code.toString(16).padStart(2, '0').toUpperCase()}`;
  }
}

function hexPreview(buf: Buffer, maxBytes = 16): string {
  const slice = buf.subarray(0, Math.min(buf.length, maxBytes));
  const hex = [...slice].map((b) => b.toString(16).padStart(2, '0')).join(' ');
  return buf.length > maxBytes ? `${hex} ... (+${buf.length - maxBytes}B)` : hex;
}

function formatTxPacket(buf: Buffer): string[] {
  if (isMostlyText(buf)) {
    return buf
      .toString('utf8')
      .split(/\r?\n/)
      .map((s) => s.trim())
      .filter(Boolean);
  }
  if (buf.length === 1 && buf[0] === CMD_FIND_IIC_ADDRESS) {
    return ['FIND_IIC_ADDRESS'];
  }
  if (buf[0] === REPORT_ID && buf[1] === IIC_DATA_1 && buf.length >= 6) {
    const reg = buf[4] | (buf[5] << 8);
    if (buf[2] === 2) {
      return [`I2C READ reg=0x${reg.toString(16).padStart(4, '0')} count=${buf[3]}`];
    }
    const wlen = buf[2] > 2 ? buf[2] - 2 : 0;
    return [`I2C WRITE reg=0x${reg.toString(16).padStart(4, '0')} len=${wlen}`];
  }
  if (buf[0] === IIC_DATA_1 && buf.length >= 5) {
    const reg = buf[3] | (buf[4] << 8);
    if (buf[1] === 2 && buf[2] > 0) {
      return [`I2C READ reg=0x${reg.toString(16).padStart(4, '0')} count=${buf[2]}`];
    }
    if (buf[1] > 2 && buf[2] === 0) {
      return [`I2C WRITE reg=0x${reg.toString(16).padStart(4, '0')} len=${buf[1] - 2}`];
    }
  }
  if (buf.length === 1 && buf[0] === CMD_READ_PINS) {
    return ['READ_PINS'];
  }
  return [`HEX ${hexPreview(buf)}`];
}

function formatRxPacket(buf: Buffer): string[] {
  if (isMostlyText(buf)) {
    return buf
      .toString('utf8')
      .split(/\r?\n/)
      .map((s) => s.trim())
      .filter(Boolean);
  }
  if (buf.length >= 2 && buf[0] === CMD_FIND_IIC_ADDRESS) {
    if (buf[1] === 0x81) return ['FIND_IIC NO_DEVICE'];
    return [`FIND_IIC OK addr=0x${buf[1].toString(16).padStart(2, '0')}`];
  }
  if (buf[0] === REPORT_ID && buf.length >= 2) {
    const st = statusName(buf[1]);
    if (buf[1] === 0x04) return [`I2C WRITE ${st}`];
    const dataLen = Math.max(0, buf.length - 3);
    if (dataLen > 0) return [`I2C READ ${st} (+${dataLen} bytes)`];
    return [`I2C ${st}`];
  }
  if (buf.length >= 1 && (buf[0] === 0x00 || buf[0] === 0x04 || buf[0] === 0x01)) {
    return [`I2C ${statusName(buf[0])}`];
  }
  if (buf.length >= 3 && buf[0] === CMD_READ_PINS) {
    const chg = buf[2] & 0x04 ? 'HIGH' : 'LOW';
    return [`READ_PINS OK CHG=${chg}`];
  }
  return [`HEX ${hexPreview(buf)}`];
}

export function formatBridgeTraffic(buf: Buffer, direction: 'tx' | 'rx'): string[] {
  if (!buf || buf.length === 0) return [];
  return direction === 'tx' ? formatTxPacket(buf) : formatRxPacket(buf);
}

export interface BridgeLogEntry {
  direction: BridgeLogDirection;
  text: string;
}

export function bridgeLogFromBuffer(buf: Buffer, direction: 'tx' | 'rx'): BridgeLogEntry[] {
  return formatBridgeTraffic(buf, direction).map((text) => ({ direction, text }));
}

interface ConsumeResult {
  consumed: number;
  messages: string[];
  pendingRxDataBytes?: number;
}

function txFrameLength(buf: Buffer): number {
  if (buf.length === 0) return 0;

  if (buf[0] >= 0x20 && buf[0] <= 0x7e) {
    const nl = buf.indexOf(0x0a);
    if (nl < 0) return 0;
    return nl + 1;
  }

  if (buf[0] === CMD_FIND_IIC_ADDRESS || buf[0] === CMD_READ_PINS) {
    return 1;
  }

  if (buf[0] === REPORT_ID && buf.length >= 2 && buf[1] === IIC_DATA_1) {
    if (buf.length < 6) return 0;
    if (buf[2] === 2) return 6;
    const dataLen = buf[2] > 2 ? buf[2] - 2 : 0;
    const total = 6 + dataLen;
    return buf.length >= total ? total : 0;
  }

  if (buf[0] === IIC_DATA_1 && buf.length >= 3) {
    const wlen = buf[1];
    const rlen = buf[2];
    let total = 0;
    if (wlen === 2 && rlen > 0) total = 5;
    else if (wlen === 0 && rlen > 0) total = 3;
    else if (wlen > 2 && rlen === 0) total = 3 + wlen;
    else if (wlen > 0 && rlen === 0) total = 3 + wlen;
    else return 1;
    return buf.length >= total ? total : 0;
  }

  return 1;
}

function consumeTxFrame(buf: Buffer): ConsumeResult {
  const frameLen = txFrameLength(buf);
  if (frameLen <= 0) {
    return { consumed: 0, messages: [] };
  }

  const frame = buf.subarray(0, frameLen);
  const messages = formatTxPacket(frame);
  let pendingRxDataBytes: number | undefined;

  if (frame[0] === REPORT_ID && frame[1] === IIC_DATA_1 && frame[2] === 2 && frame.length >= 6) {
    pendingRxDataBytes = frame[3];
  } else if (frame[0] === IIC_DATA_1 && frame[1] === 2 && frame[2] > 0) {
    pendingRxDataBytes = frame[2];
  } else if (frame.length === 1 && frame[0] === CMD_FIND_IIC_ADDRESS) {
    pendingRxDataBytes = -1;
  } else {
    pendingRxDataBytes = 0;
  }

  return { consumed: frameLen, messages, pendingRxDataBytes };
}

function rxFrameLength(buf: Buffer, pendingRxDataBytes: number): number {
  if (buf.length === 0) return 0;

  if (buf[0] >= 0x20 && buf[0] <= 0x7e) {
    const nl = buf.indexOf(0x0a);
    if (nl < 0) return 0;
    return nl + 1;
  }

  if (pendingRxDataBytes === -1) {
    return buf.length >= 2 && buf[0] === CMD_FIND_IIC_ADDRESS ? 2 : 0;
  }

  if (buf[0] === REPORT_ID) {
    if (pendingRxDataBytes > 0) {
      const total = 3 + pendingRxDataBytes;
      return buf.length >= total ? total : 0;
    }
    return buf.length >= 2 ? 2 : 0;
  }

  if (buf[0] === CMD_READ_PINS) {
    return buf.length >= 3 ? 3 : 0;
  }

  if (buf.length >= 1 && (buf[0] === 0x00 || buf[0] === 0x04 || buf[0] === 0x01)) {
    return 1;
  }

  return 1;
}

function consumeRxFrame(buf: Buffer, pendingRxDataBytes: number): ConsumeResult {
  const frameLen = rxFrameLength(buf, pendingRxDataBytes);
  if (frameLen <= 0) {
    return { consumed: 0, messages: [] };
  }

  const frame = buf.subarray(0, frameLen);
  return { consumed: frameLen, messages: formatRxPacket(frame), pendingRxDataBytes: 0 };
}

/** TCP 代理会话：按帧边界拆分 mode0 文本与 I2C 二进制包 */
export class BridgeLogSessionParser {
  private txPending = Buffer.alloc(0);
  private rxPending = Buffer.alloc(0);
  private pendingRxDataBytes = 0;

  reset(): void {
    this.txPending = Buffer.alloc(0);
    this.rxPending = Buffer.alloc(0);
    this.pendingRxDataBytes = 0;
  }

  push(chunk: Buffer, direction: 'tx' | 'rx'): BridgeLogEntry[] {
    if (!chunk.length) return [];

    if (direction === 'tx') {
      this.txPending = Buffer.concat([this.txPending, chunk]);
      return this.drainTx();
    }

    this.rxPending = Buffer.concat([this.rxPending, chunk]);
    return this.drainRx();
  }

  private drainTx(): BridgeLogEntry[] {
    const out: BridgeLogEntry[] = [];
    while (this.txPending.length > 0) {
      const result = consumeTxFrame(this.txPending);
      if (result.consumed <= 0) break;
      if (result.pendingRxDataBytes !== undefined) {
        this.pendingRxDataBytes = result.pendingRxDataBytes;
      }
      for (const text of result.messages) {
        out.push({ direction: 'tx', text });
      }
      this.txPending = this.txPending.subarray(result.consumed);
    }
    return out;
  }

  private drainRx(): BridgeLogEntry[] {
    const out: BridgeLogEntry[] = [];
    while (this.rxPending.length > 0) {
      const result = consumeRxFrame(this.rxPending, this.pendingRxDataBytes);
      if (result.consumed <= 0) break;
      this.pendingRxDataBytes = 0;
      for (const text of result.messages) {
        out.push({ direction: 'rx', text });
      }
      this.rxPending = this.rxPending.subarray(result.consumed);
    }
    return out;
  }
}
