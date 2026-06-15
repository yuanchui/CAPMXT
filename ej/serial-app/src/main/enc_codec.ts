import fs from 'fs';
import { Readable } from 'stream';

/** 从二进制流按大端 L 切分 enc 帧（与 enc_upload_analysis.md 一致） */
export function splitEncFramesFromBinary(pending: Buffer): { frames: Buffer[]; rest: Buffer } {
  const frames: Buffer[] = [];
  let i = 0;
  while (i + 2 <= pending.length) {
    const L = (pending[i] << 8) | pending[i + 1];
    if (L === 0) {
      frames.push(pending.subarray(i, i + 2));
      i += 2;
      continue;
    }
    const need = 2 + L;
    if (i + need > pending.length) break;
    frames.push(pending.subarray(i, i + need));
    i += need;
  }
  return { frames, rest: pending.subarray(i) };
}

export class EncFrameStreamParser {
  private hexDigits = '';
  private binaryPending = Buffer.alloc(0);

  pushText(text: string): Buffer[] {
    for (const ch of text) {
      if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) {
        this.hexDigits += ch;
      }
    }
    while (this.hexDigits.length >= 2) {
      const pair = this.hexDigits.slice(0, 2);
      this.hexDigits = this.hexDigits.slice(2);
      const byte = parseInt(pair, 16);
      this.binaryPending = Buffer.concat([this.binaryPending, Buffer.from([byte])]);
    }
    const split = splitEncFramesFromBinary(this.binaryPending);
    this.binaryPending = Buffer.from(split.rest);
    return split.frames;
  }

  finish(): Buffer[] {
    if (this.hexDigits.length > 0) {
      throw new Error(`enc 文件末尾存在不完整十六进制 (${this.hexDigits.length} 字符)`);
    }
    if (this.binaryPending.length > 0) {
      throw new Error(`enc 二进制流末尾残留 ${this.binaryPending.length} 字节（帧不完整）`);
    }
    return [];
  }
}

export async function scanEncFile(filePath: string): Promise<{ totalFrames: number; totalBinaryBytes: number }> {
  let totalFrames = 0;
  let totalBinaryBytes = 0;
  for await (const frame of iterateEncFramesFromFile(filePath)) {
    totalFrames += 1;
    totalBinaryBytes += frame.length;
  }
  return { totalFrames, totalBinaryBytes };
}

export async function* iterateEncFramesFromFile(filePath: string): AsyncGenerator<Buffer> {
  const parser = new EncFrameStreamParser();
  const stream = fs.createReadStream(filePath, { encoding: 'utf8', highWaterMark: 65536 });
  for await (const chunk of stream) {
    const frames = parser.pushText(chunk);
    for (const f of frames) yield f;
  }
  const tail = parser.finish();
  for (const f of tail) yield f;
}

export function iterateEncFramesFromReadable(readable: Readable): AsyncGenerator<Buffer> {
  async function* gen() {
    const parser = new EncFrameStreamParser();
    for await (const chunk of readable) {
      const text = typeof chunk === 'string' ? chunk : chunk.toString('utf8');
      const frames = parser.pushText(text);
      for (const f of frames) yield f;
    }
    const tail = parser.finish();
    for (const f of tail) yield f;
  }
  return gen();
}
