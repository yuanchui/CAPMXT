/**
 * Mode3 (AA 10 33) 帧组装器 — 主进程与渲染进程共用
 */

export const MATRIX_SIZE = 16;
export const MODE3_HEADER_0 = 0xaa;
export const MODE3_HEADER_1 = 0x10;
export const MODE3_HEADER_2 = 0x33;
export const MODE3_MIN_PACKET_SIZE = 40;
export const MODE3_MAX_RX_BUFFER = 8192;

/** 与 mxt_spi_stream.c / mxt_touch.c 一致的 Mode3 行包格式说明（40 字节/行） */
export const MODE3_ROW_PACKET_FORMAT =
  'AA 10 33 | LEN=40(0x28) | FRAME_ID | ROW_ID(0~15) | DATA[16×int16 BE] | CRC16-CCITT-FALSE(BE,[0..37])';

export const MODE3_ROW_PACKET_DESC =
  `Mode3 行包: ${MODE3_ROW_PACKET_FORMAT}。DATA 为有符号 int16 大端；整帧 16 行收齐后刷新 16×16 矩阵。`;

export const MODE3_MUTUAL_DELTA_DESC =
  `Mode3 互容差值(0x10): ${MODE3_ROW_PACKET_FORMAT}。有符号 int16 直显，不缩放。`;

export const MODE3_SCALED_DESC =
  `Mode3 帧: ${MODE3_ROW_PACKET_FORMAT}。有符号 int16 解析后 ÷100 显示。`;

export interface MatrixFramePayload {
  frameId: number;
  timestamp: number;
  values: Int16Array;
  stats: { min: number; max: number; mean: number; stddev: number };
}

export interface Mode3ParserOptions {
  onFrame?: (frame: MatrixFramePayload) => void;
  onRow?: (frameId: number, lineId: number) => void;
}

function u16ToS16(u16: number): number {
  const v = u16 & 0xffff;
  return v & 0x8000 ? v - 0x10000 : v;
}

export function computeBasicStats(values: Int16Array | Float32Array): {
  min: number;
  max: number;
  mean: number;
  stddev: number;
} {
  let min = Number.POSITIVE_INFINITY;
  let max = Number.NEGATIVE_INFINITY;
  let sum = 0;
  for (let i = 0; i < values.length; i++) {
    const v = values[i];
    if (v < min) min = v;
    if (v > max) max = v;
    sum += v;
  }
  const mean = sum / values.length;
  let varSum = 0;
  for (let i = 0; i < values.length; i++) {
    const d = values[i] - mean;
    varSum += d * d;
  }
  return { min, max, mean, stddev: Math.sqrt(varSum / values.length) };
}

export class Mode3FrameAssembler {
  private rxBuffer = new Uint8Array(0);
  private matrix = new Int16Array(MATRIX_SIZE * MATRIX_SIZE);
  private rowMask = 0;
  private currentFrameId = -1;
  private paused = false;
  private readonly onFrame?: (frame: MatrixFramePayload) => void;
  private readonly onRow?: (frameId: number, lineId: number) => void;

  constructor(options: Mode3ParserOptions = {}) {
    this.onFrame = options.onFrame;
    this.onRow = options.onRow;
  }

  isPaused(): boolean {
    return this.paused;
  }

  pauseAndFlush(): void {
    this.paused = true;
    this.rxBuffer = new Uint8Array(0);
    this.rowMask = 0;
    this.currentFrameId = -1;
    this.matrix.fill(0);
  }

  resume(): void {
    this.paused = false;
    this.rxBuffer = new Uint8Array(0);
    this.rowMask = 0;
    this.currentFrameId = -1;
  }

  push(chunk: Uint8Array): void {
    if (this.paused || !chunk.length) return;

    const combined = new Uint8Array(this.rxBuffer.length + chunk.length);
    combined.set(this.rxBuffer, 0);
    combined.set(chunk, this.rxBuffer.length);

    if (combined.length > MODE3_MAX_RX_BUFFER) {
      this.rxBuffer = combined.slice(-MODE3_MAX_RX_BUFFER);
    } else {
      this.rxBuffer = combined;
    }

    this.parseBuffer();
  }

  private emitFrame(frameId: number): void {
    if (!this.onFrame) return;
    const copy = new Int16Array(this.matrix);
    this.onFrame({
      frameId,
      timestamp: Date.now(),
      values: copy,
      stats: computeBasicStats(copy),
    });
  }

  private writeRow(lineId: number, buf: Uint8Array, headerPos: number): void {
    const base = lineId * MATRIX_SIZE;
    for (let j = 0; j < MATRIX_SIZE; j++) {
      const off = headerPos + 6 + j * 2;
      const hi = buf[off];
      const lo = buf[off + 1];
      this.matrix[base + j] = u16ToS16((hi << 8) | lo);
    }
  }

  private parseBuffer(): void {
    let searchStart = 0;

    while (searchStart <= this.rxBuffer.length - MODE3_MIN_PACKET_SIZE) {
      let headerPos = -1;
      for (let i = searchStart; i <= this.rxBuffer.length - 3; i++) {
        if (
          this.rxBuffer[i] === MODE3_HEADER_0 &&
          this.rxBuffer[i + 1] === MODE3_HEADER_1 &&
          this.rxBuffer[i + 2] === MODE3_HEADER_2
        ) {
          headerPos = i;
          break;
        }
      }

      if (headerPos < 0) {
        if (this.rxBuffer.length > 3) this.rxBuffer = this.rxBuffer.slice(-3);
        break;
      }

      if (headerPos + 3 >= this.rxBuffer.length) {
        this.rxBuffer = this.rxBuffer.slice(headerPos);
        break;
      }

      const packetLen = this.rxBuffer[headerPos + 3];
      if (packetLen < MODE3_MIN_PACKET_SIZE || packetLen > 255) {
        searchStart = headerPos + 1;
        continue;
      }

      if (headerPos + packetLen > this.rxBuffer.length) {
        this.rxBuffer = this.rxBuffer.slice(headerPos);
        break;
      }

      const frameId = this.rxBuffer[headerPos + 4];
      const lineId = this.rxBuffer[headerPos + 5];

      if (lineId >= MATRIX_SIZE) {
        searchStart = headerPos + packetLen;
        continue;
      }

      if (frameId !== this.currentFrameId) {
        this.rowMask = 0;
        this.currentFrameId = frameId;
      }

      if (lineId === 0 && this.rowMask !== 0 && this.rowMask !== 0xffff) {
        this.rowMask = 0;
      }

      this.writeRow(lineId, this.rxBuffer, headerPos);
      this.rowMask |= 1 << lineId;
      this.onRow?.(frameId, lineId);

      if (this.rowMask === 0xffff) {
        this.emitFrame(frameId);
        this.rowMask = 0;
      }

      searchStart = headerPos + packetLen;
    }

    if (searchStart > 0 && searchStart < this.rxBuffer.length) {
      this.rxBuffer = this.rxBuffer.slice(searchStart);
    } else if (searchStart >= this.rxBuffer.length) {
      this.rxBuffer = new Uint8Array(0);
    }
  }
}
