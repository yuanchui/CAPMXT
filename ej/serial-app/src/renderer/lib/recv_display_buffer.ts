/**
 * 接收区高效缓冲：环形字节缓冲 + 批量转 hex/text，避免每 chunk 字符串拼接
 */

const HEX_LOOKUP = Array.from({ length: 256 }, (_, i) =>
  i.toString(16).toUpperCase().padStart(2, '0')
);

export class RecvDisplayBuffer {
  private ring: Uint8Array;
  private writePos = 0;
  private count = 0;
  private paused = false;
  private readonly capacity: number;
  private readonly maxDisplayChars: number;
  private textDecoder = new TextDecoder('utf-8', { fatal: false });

  constructor(capacity = 131072, maxDisplayChars = 60000) {
    this.capacity = capacity;
    this.maxDisplayChars = maxDisplayChars;
    this.ring = new Uint8Array(capacity);
  }

  pause(): void {
    this.paused = true;
  }

  resume(): void {
    this.paused = false;
  }

  clear(): void {
    this.writePos = 0;
    this.count = 0;
  }

  push(chunk: Uint8Array): void {
    if (this.paused || !chunk.length) return;
    for (let i = 0; i < chunk.length; i++) {
      this.ring[this.writePos] = chunk[i];
      this.writePos = (this.writePos + 1) % this.capacity;
      if (this.count < this.capacity) this.count++;
    }
  }

  /** 取最近 maxDisplayChars 字符对应的原始字节（近似按字节→字符 1:1 截断） */
  private recentBytes(maxBytes: number): Uint8Array {
    const n = Math.min(this.count, maxBytes);
    const out = new Uint8Array(n);
    let start = (this.writePos - n + this.capacity) % this.capacity;
    for (let i = 0; i < n; i++) {
      out[i] = this.ring[start];
      start = (start + 1) % this.capacity;
    }
    return out;
  }

  toText(): string {
    const raw = this.recentBytes(this.maxDisplayChars);
    return this.textDecoder.decode(raw);
  }

  toHex(): string {
    const raw = this.recentBytes(Math.floor(this.maxDisplayChars / 3));
    const parts: string[] = new Array(raw.length);
    for (let i = 0; i < raw.length; i++) {
      parts[i] = HEX_LOOKUP[raw[i]] + ' ';
    }
    return parts.join('');
  }
}
