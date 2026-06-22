/**
 * 解析 test-V1.7 / 主工程 MCU 字符串命令 EXPORTBIN 的二进制流（A0/A1/A2）。
 */
import type { DeviceConfigObject, DeviceConfigSnapshot } from './xcfg_format4';
import { isVolatileMxtObject, isRuntimeDataContainerObject } from './mxt_object_names';

const EXPORT_BIN_TIMEOUT_MS = 120000;

function shouldIncludeObject(type: number): boolean {
  if (isVolatileMxtObject(type)) return false;
  if (isRuntimeDataContainerObject(type)) return false;
  return true;
}

export function parseExportBinBuffer(buf: Buffer): DeviceConfigSnapshot {
  let pos = 0;
  let family = 0;
  let variant = 0;
  let version = 0;
  let build = 0;
  let numObjects = 0;
  let endStatus = 255;
  const partial = new Map<string, { type: number; instance: number; address: number; size: number; chunks: Map<number, Buffer> }>();

  while (pos < buf.length) {
    const tag = buf[pos];
    if (tag === 0xA0) {
      if (pos + 6 > buf.length) break;
      family = buf[pos + 1];
      variant = buf[pos + 2];
      version = buf[pos + 3];
      build = buf[pos + 4];
      numObjects = buf[pos + 5];
      pos += 6;
      continue;
    }
    if (tag === 0xA1) {
      if (pos + 8 > buf.length) break;
      const objType = buf[pos + 1];
      const inst = buf[pos + 2];
      const addr = buf[pos + 3] | (buf[pos + 4] << 8);
      const offset = buf[pos + 5] | (buf[pos + 6] << 8);
      const chunkLen = buf[pos + 7];
      if (pos + 8 + chunkLen > buf.length) break;
      const payload = buf.subarray(pos + 8, pos + 8 + chunkLen);
      pos += 8 + chunkLen;

      const key = `${objType}:${inst}`;
      let entry = partial.get(key);
      if (!entry) {
        entry = { type: objType, instance: inst, address: addr, size: 0, chunks: new Map() };
        partial.set(key, entry);
      }
      entry.chunks.set(offset, Buffer.from(payload));
      const end = offset + chunkLen;
      if (end > entry.size) entry.size = end;
      continue;
    }
    if (tag === 0xA2) {
      if (pos + 2 > buf.length) break;
      endStatus = buf[pos + 1];
      pos += 2;
      break;
    }
    pos += 1;
  }

  if (endStatus !== 0) {
    throw new Error(`EXPORTBIN 结束状态异常: ${endStatus}`);
  }

  const objects: DeviceConfigObject[] = [];
  for (const entry of partial.values()) {
    if (!shouldIncludeObject(entry.type)) continue;
    const data = Buffer.alloc(entry.size);
    for (const [off, chunk] of entry.chunks.entries()) {
      chunk.copy(data, off);
    }
    objects.push({
      type: entry.type,
      instance: entry.instance,
      address: entry.address,
      size: entry.size,
      data
    });
  }

  objects.sort((a, b) => a.address - b.address || a.type - b.type || a.instance - b.instance);

  return {
    family,
    variant,
    version,
    build,
    matrixX: 0,
    matrixY: 0,
    numObjects,
    infoCrc: 0,
    objects
  };
}

/** 等待 EXPORTBIN 完整二进制流（A0…A2） */
export async function waitForExportBinComplete(
  getBuffer: () => Buffer,
  waitChunk: (timeoutMs: number) => Promise<void>,
  deadlineMs = EXPORT_BIN_TIMEOUT_MS
): Promise<Buffer> {
  const deadline = Date.now() + deadlineMs;
  while (Date.now() < deadline) {
    const buf = getBuffer();
    const startIdx = buf.indexOf(0xA0);
    if (startIdx < 0) {
      await waitChunk(Math.min(500, Math.max(0, deadline - Date.now()))).catch(() => {});
      continue;
    }
    const slice = startIdx > 0 ? buf.subarray(startIdx) : buf;
    const endIdx = slice.indexOf(0xA2);
    if (endIdx >= 0 && endIdx + 1 < slice.length) {
      const status = slice[endIdx + 1];
      if (status === 0) {
        return slice.subarray(0, endIdx + 2);
      }
      throw new Error(`EXPORTBIN 失败 status=${status}`);
    }
    await waitChunk(Math.min(500, Math.max(0, deadline - Date.now()))).catch(() => {});
  }
  throw new Error('EXPORTBIN 超时');
}
