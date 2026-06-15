import { app, BrowserWindow, ipcMain, Menu, dialog, shell } from 'electron';
import * as path from 'path';
import * as fs from 'fs';
import * as crypto from 'crypto';
import { execFile } from 'child_process';
import { buildObjectBytes, parseXcfg, serializeXcfg, updateObjectFieldsFromBytes, type XcfgData } from './xcfg_codec';
import {
  CFG_MAX_CHUNK,
  CFG_MAX_OBJECTS_MCU,
  CFG_PROTOCOL_VERSION,
  CFGWRITE_CHUNK_CMD,
  CFGWRITE_END_CMD,
  CFGWRITE_START_CMD,
  CFG_RESP_ACK_CMD,
  CFG_RESP_NACK_CMD,
  CFGREAD_DATA_CMD,
  CFGREAD_END_CMD,
  STATUS_OK,
  STATUS_OBJ_DONE,
  STATUS_ADDR_NACK,
  STATUS_NO_DEVICE,
  UNFREEZE_COMMAND,
  BACKUPNV_COMMAND,
  assertObjectCountWithinMcuLimit,
  cfgStartFrameMaxBytes
} from './cfg_protocol';
import {
  ENC_PROTOCOL_VERSION,
  ENC_START_CMD,
  ENC_FRAME_CMD,
  ENC_END_CMD,
  ENC_RESP_ACK_CMD,
  ENC_RESP_NACK_CMD,
  ENC_DEFAULT_BL_ADDR,
  ENC_FLAG_SKIP_ENTER_BOOTLOADER,
  ENC_MAX_FRAME_BYTES
} from './enc_protocol';
import { iterateEncFramesFromFile, scanEncFile } from './enc_codec';
import { registerXcfgViewerIpc, setXcfgViewerInitialPayload } from './xcfg_viewer_api';
import {
  createCoordinatedMxtAppRunner,
  getDefaultMxtDeviceFromSessions,
  portPathToMxtDevice,
  type BridgeSessionSnapshot,
  type MxtUsbCoordState
} from './mxt_usb_coord';
import { bytesSwitchToMode0, bytesSwitchToMode1 } from './bridge_mode';

// 参考 xcfg-viewer: 使用 mxt-app -q (libusb) 枚举 WinUSB 设备，与 app.py 中 _run_mxt_app / _extract_supported_devices_from_q 一致
const MXT_APP_SUPPORTED_VIDPID = new Set(['0483:5740', '03eb:211d', '03eb:2119', '03eb:6123']);

function getMxtAppPath(): string | null {
  const envPath = process.env.MXT_APP?.trim();
  if (envPath) return envPath;
  const exeName = process.platform === 'win32' ? 'mxt-app.exe' : 'mxt-app';
  // 与 xcfg-viewer 一致：CLI 目录、resources 同级的 CLI、应用根目录
  const candidates = [
    path.join(path.dirname(process.execPath), 'CLI', exeName),
    path.join(process.resourcesPath || '', 'CLI', exeName),
    path.join(app.getAppPath(), 'CLI', exeName),
    path.join(app.getAppPath(), '..', 'CLI', exeName)
  ];
  for (const p of candidates) {
    try {
      if (p && fs.existsSync(p)) return p;
    } catch (_) {}
  }
  return exeName; // 回退到 PATH
}

function getMxtUsbCoordState(): MxtUsbCoordState {
  return {
    getBridgeSessions: () => {
      const sessions: BridgeSessionSnapshot[] = [];
      for (const [portPath, conn] of winUsbConnections.entries()) {
        const mxtDevice = portPathToMxtDevice(portPath);
        if (!mxtDevice) continue;
        sessions.push({
          portPath,
          mxtDevice,
          readActive: Boolean(conn.readActive),
          kind: 'winusb'
        });
      }
      for (const [portPath, conn] of serialPorts.entries()) {
        const mxtDevice = portPathToMxtDevice(portPath);
        if (!mxtDevice) continue;
        sessions.push({
          portPath,
          mxtDevice,
          readActive: Boolean(conn.readActive),
          kind: 'serial'
        });
      }
      return sessions;
    },
    disconnectBridge: async (session) => {
      if (session.kind === 'winusb') await disconnectWinUsb(session.portPath);
      else await disconnectSerialPort(session.portPath);
    },
    connectBridge: async (session) => {
      if (session.kind === 'winusb') return await connectWinUsb(session.portPath);
      const existing = serialPorts.get(session.portPath);
      return await connectSerialPort(session.portPath, existing?.baudRate || 115200);
    },
    startBridgeRead: (session) => {
      if (session.kind === 'winusb') startWinUsbRead(session.portPath);
      else startSerialRead(session.portPath);
    }
  };
}

function runMxtApp(args: string[], device?: string, timeout = 60000): Promise<{ success: boolean; returncode: number; stdout: string; stderr: string }> {
  return new Promise((resolve) => {
    const mxtPath = getMxtAppPath() || 'mxt-app';
    const finalArgs = device ? ['-d', device, ...args] : args;
    const env = { ...process.env } as NodeJS.ProcessEnv;

    if (path.isAbsolute(mxtPath) && mxtPath.toLowerCase().endsWith('.exe')) {
      env.PATH = path.dirname(mxtPath) + path.delimiter + (env.PATH || '');
    }

    execFile(mxtPath, finalArgs, { windowsHide: true, timeout, env }, (error, stdout, stderr) => {
      if (error) {
        resolve({
          success: false,
          returncode: typeof (error as any).code === 'number' ? (error as any).code : -1,
          stdout: String(stdout || ''),
          stderr: String(stderr || error.message || '')
        });
        return;
      }
      resolve({
        success: true,
        returncode: 0,
        stdout: String(stdout || ''),
        stderr: String(stderr || '')
      });
    });
  });
}

const runMxtAppCoordinated = createCoordinatedMxtAppRunner(getMxtUsbCoordState(), runMxtApp);

function getDefaultMxtDeviceFromSerialApp(): string | undefined {
  return getDefaultMxtDeviceFromSessions(getMxtUsbCoordState().getBridgeSessions());
}

function sanitizeBackupFileName(name: string): string {
  const now = new Date();
  const ts = `${now.getFullYear()}${String(now.getMonth() + 1).padStart(2, '0')}${String(now.getDate()).padStart(2, '0')}-${String(now.getHours()).padStart(2, '0')}${String(now.getMinutes()).padStart(2, '0')}${String(now.getSeconds()).padStart(2, '0')}`;
  const raw = (name || 'test-V1-backup').trim();
  const safe = raw.replace(/[^a-zA-Z0-9._-]/g, '_') || 'test-V1-backup';
  const withExt = safe.toLowerCase().endsWith('.xcfg') ? safe : `${safe}.xcfg`;
  const parsed = path.parse(withExt);
  return `${parsed.name}-${ts}.xcfg`;
}

interface MxtAppDevice {
  usbId: string;
  vidPid: string;
  product?: string;
}

function checkWinUsbViaMxtApp(vendorIdHex: string, productIdHex: string): Promise<{ present: boolean; devices?: MxtAppDevice[]; error?: string }> {
  return new Promise((resolve) => {
    const vid = (vendorIdHex || '').toUpperCase().replace(/^0X/, '').padStart(4, '0');
    const pid = (productIdHex || '').toUpperCase().replace(/^0X/, '').padStart(4, '0');
    const targetVidPid = `${vid}:${pid}`;
    const mxtPath = getMxtAppPath();
    if (!mxtPath) {
      resolve({ present: false, error: 'mxt-app not found' });
      return;
    }
    const env = { ...process.env };
    if (path.isAbsolute(mxtPath) && mxtPath.toLowerCase().endsWith('.exe')) {
      env.PATH = path.dirname(mxtPath) + path.delimiter + (env.PATH || '');
    }
    execFile(
      mxtPath,
      ['-q'],
      { windowsHide: true, timeout: 10000, env },
      (error, stdout, stderr) => {
        if (error) {
          resolve({ present: false, error: error.message });
          return;
        }
        const out = (stdout || '').trim();
        const devices: MxtAppDevice[] = [];
        const re = /^usb:(\d{3})-(\d{3})\s+([0-9A-Fa-f]{4}):([0-9A-Fa-f]{4})(?:\s+(.+))?$/;
        for (const line of out.split('\n')) {
          const m = line.trim().match(re);
          if (!m) continue;
          const vidPid = `${m[3].toLowerCase()}:${m[4].toLowerCase()}`;
          if (vidPid !== targetVidPid.toLowerCase() && !MXT_APP_SUPPORTED_VIDPID.has(vidPid)) continue;
          devices.push({
            usbId: `usb:${m[1]}-${m[2]}`,
            vidPid: `${m[3]}:${m[4]}`,
            product: m[5]?.trim() || undefined
          });
        }
        resolve({
          present: devices.some(d => d.vidPid.toLowerCase() === targetVidPid.toLowerCase()),
          devices: devices.length ? devices : undefined
        });
      }
    );
  });
}

let mainWindow: BrowserWindow | null = null;
let splashWindow: BrowserWindow | null = null;
const detachedModalWindows = new Map<string, BrowserWindow>();
const xcfgViewerWindows = new Map<string, BrowserWindow>();
let lastXcfgDir: string = process.cwd();
const RUNTIME_CFG_SECRET = 'mxt-runtime-window-v1';

function broadcastToRendererWindows(channel: string, ...args: any[]) {
  for (const win of BrowserWindow.getAllWindows()) {
    if (!win || win.isDestroyed()) continue;
    try {
      win.webContents.send(channel, ...args);
    } catch (_) {}
  }
}

function decryptRuntimePayload(payload: string): Record<string, any> | null {
  try {
    const parts = String(payload || '').trim().split(':');
    if (parts.length !== 4 || parts[0] !== 'v1') return null;
    const iv = Buffer.from(parts[1], 'base64');
    const tag = Buffer.from(parts[2], 'base64');
    const encrypted = Buffer.from(parts[3], 'base64');
    const key = crypto.createHash('sha256').update(RUNTIME_CFG_SECRET).digest();
    const decipher = crypto.createDecipheriv('aes-256-gcm', key, iv);
    decipher.setAuthTag(tag);
    const plaintext = Buffer.concat([decipher.update(encrypted), decipher.final()]).toString('utf-8');
    const obj = JSON.parse(plaintext);
    return obj && typeof obj === 'object' ? obj : null;
  } catch {
    return null;
  }
}

function loadEmbeddedRuntimeWindow(): { startMs: number; maxDays: number; expireMs?: number; fixedStartAt?: string } | null {
  try {
    const embeddedPath = path.join(app.getAppPath(), 'dist', 'main', 'runtime-window.generated.json');
    if (!fs.existsSync(embeddedPath)) return null;
    const text = fs.readFileSync(embeddedPath, 'utf-8');
    const obj = JSON.parse(text);
    const payload = typeof obj?.payload === 'string' ? obj.payload.trim() : '';
    if (!payload) return null;
    const decrypted = decryptRuntimePayload(payload);
    if (!decrypted) return null;
    const maxDays = Number(decrypted.max_days);
    const startMs = Date.parse(String(decrypted.start_at || '').replace(' ', 'T'));
    const expireMs = Date.parse(String(decrypted.expire_at || '').replace(' ', 'T'));
    const fixedStartAtRaw = String(decrypted.fixed_start_at || '').trim();
    if (!Number.isFinite(maxDays) || maxDays <= 0 || !Number.isFinite(startMs)) return null;
    return {
      startMs,
      maxDays: Math.floor(maxDays),
      expireMs: Number.isFinite(expireMs) ? expireMs : undefined,
      fixedStartAt: fixedStartAtRaw || undefined
    };
  } catch {
    return null;
  }
}

function readProjectRuntimeWindowConfig(): { maxDays: number; fixedStartAt: string } {
  try {
    const cfgPath = path.join(app.getAppPath(), 'build', 'runtime-window.config.json');
    if (!fs.existsSync(cfgPath)) return { maxDays: 30, fixedStartAt: '' };
    const text = fs.readFileSync(cfgPath, 'utf-8');
    const obj = JSON.parse(text);
    const n = Number(obj?.maxDays);
    const maxDays = Number.isFinite(n) && n > 0 ? Math.floor(n) : 30;
    const fixedStartAt = String(obj?.fixedStartAt || '').trim();
    return { maxDays, fixedStartAt };
  } catch {
    return { maxDays: 30, fixedStartAt: '' };
  }
}

function getRuntimeWindowDisplayConfig(): { maxDays: number; fixedStartAt: string } {
  const embedded = loadEmbeddedRuntimeWindow();
  if (embedded) return { maxDays: embedded.maxDays, fixedStartAt: embedded.fixedStartAt || '' };
  return readProjectRuntimeWindowConfig();
}

function getBuildMode(): 'user' | 'debug' {
  try {
    const envMode = String(process.env.APP_MODE || '').trim().toLowerCase();
    if (envMode === 'debug') return 'debug';
    if (envMode === 'user') return 'user';
    if (!app.isPackaged) return 'debug';
    const pkgPath = path.join(app.getAppPath(), 'package.json');
    if (!fs.existsSync(pkgPath)) return 'user';
    const pkg = JSON.parse(fs.readFileSync(pkgPath, 'utf-8'));
    const mode = String(pkg?.buildMode || '').trim().toLowerCase();
    return mode === 'debug' ? 'debug' : 'user';
  } catch {
    return app.isPackaged ? 'user' : 'debug';
  }
}

function getLocalTempDir(): string {
  const baseDir = app.isPackaged ? path.dirname(process.execPath) : process.cwd();
  return path.join(baseDir, 'temp');
}

function isWithinRuntimeWindow(maxDays: number): boolean {
  try {
    // 开发环境默认放行；打包后仅使用打包时写入到应用内的时间窗口配置
    if (!app.isPackaged) return true;
    const now = Date.now();
    const embedded = loadEmbeddedRuntimeWindow();
    // 打包版采用 fail-closed：若读取不到内嵌窗口配置，则拒绝启动
    if (!embedded) return false;
    const expireMs = embedded.startMs + embedded.maxDays * 24 * 60 * 60 * 1000;
    const effectiveExpireMs = Number.isFinite(embedded.expireMs) ? embedded.expireMs! : expireMs;
    const finalExpireMs = Math.min(expireMs, effectiveExpireMs);
    return now <= finalExpireMs;
  } catch {
    return !app.isPackaged;
  }
}
interface PreparedXcfgChunk {
  seq: number;
  obj_index: number;
  offset: number;
  data: Uint8Array;
}
interface PreparedXcfgData {
  token: string;
  fileName: string;
  backupName: string;
  total_objects: number;
  total_chunks: number;
  total_bytes: number;
  chunks: PreparedXcfgChunk[];
  objectsMeta: Array<{ object_address: number; object_size: number }>;
  parsed: XcfgData;
  objBytes: Uint8Array[];
  preparedFilePath: string;
  preparedAt: number;
}
const preparedXcfgMap = new Map<string, PreparedXcfgData>();
interface ParsedPreparedChunkMeta {
  seq: number;
  obj_index: number;
  offset: number;
  len: number;
}
interface ParsedPreparedBinData {
  protocolVersion: number;
  total_objects: number;
  total_chunks: number;
  total_bytes: number;
  objectsMeta: Array<{ object_address: number; object_size: number }>;
  chunks: ParsedPreparedChunkMeta[];
  objBytes: Uint8Array[];
}

function crc16ModbusIBM(data: Uint8Array): number {
  let crc = 0xFFFF;
  for (let i = 0; i < data.length; i++) {
    crc ^= data[i] & 0xFF;
    for (let j = 0; j < 8; j++) {
      if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
      else crc >>= 1;
    }
  }
  return crc & 0xFFFF;
}

function buildFrameWithCRC(bytes: number[]): Uint8Array {
  const b = Uint8Array.from(bytes);
  const crc = crc16ModbusIBM(b);
  return Uint8Array.from([...bytes, crc & 0xFF, (crc >> 8) & 0xFF]);
}

function u16le(v: number): number[] {
  return [v & 0xFF, (v >> 8) & 0xFF];
}

function u32le(v: number): number[] {
  return [v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, (v >> 24) & 0xFF];
}

function parsePreparedBinBuffer(buf: Buffer): ParsedPreparedBinData {
  if (!buf || buf.length < 16) throw new Error('bin 文件过短');

  let pos = 0;
  if (buf[pos] !== CFGWRITE_START_CMD) throw new Error('bin 起始帧不是 D0');

  const protocolVersion = buf[pos + 1];
  const total_objects = buf[pos + 2] | (buf[pos + 3] << 8);
  assertObjectCountWithinMcuLimit(total_objects);
  const total_chunks = buf[pos + 4] | (buf[pos + 5] << 8);
  const total_bytes =
    (buf[pos + 6]) |
    (buf[pos + 7] << 8) |
    (buf[pos + 8] << 16) |
    (buf[pos + 9] << 24);

  const startLen = 12 + total_objects * 4;
  if (buf.length < startLen) throw new Error('bin 起始帧长度错误');
  const startFrame = buf.subarray(pos, pos + startLen);
  const startCrc = startFrame[startLen - 2] | (startFrame[startLen - 1] << 8);
  if (crc16ModbusIBM(startFrame.subarray(0, startLen - 2)) !== startCrc) throw new Error('bin 起始帧 CRC 错误');

  const objectsMeta: Array<{ object_address: number; object_size: number }> = [];
  for (let i = 0; i < total_objects; i++) {
    const base = pos + 10 + i * 4;
    const object_address = buf[base] | (buf[base + 1] << 8);
    const object_size = buf[base + 2] | (buf[base + 3] << 8);
    objectsMeta.push({ object_address, object_size });
  }
  pos += startLen;

  const objBytes = objectsMeta.map((m) => new Uint8Array(m.object_size));
  const chunks: ParsedPreparedChunkMeta[] = [];
  for (let i = 0; i < total_chunks; i++) {
    if (pos + 11 > buf.length || buf[pos] !== CFGWRITE_CHUNK_CMD) throw new Error('bin 分包帧格式错误');
    const seq = buf[pos + 1] | (buf[pos + 2] << 8);
    const obj_index = buf[pos + 3] | (buf[pos + 4] << 8);
    const offset = buf[pos + 5] | (buf[pos + 6] << 8);
    const len = buf[pos + 7] | (buf[pos + 8] << 8);
    const frameLen = 11 + len;
    if (pos + frameLen > buf.length) throw new Error('bin 分包帧长度错误');
    const frame = buf.subarray(pos, pos + frameLen);
    const crc = frame[frameLen - 2] | (frame[frameLen - 1] << 8);
    if (crc16ModbusIBM(frame.subarray(0, frameLen - 2)) !== crc) throw new Error(`bin 分包 CRC 错误 seq=${seq}`);

    if (obj_index < 0 || obj_index >= objBytes.length) throw new Error(`bin 分包对象索引越界 seq=${seq}`);
    if (offset + len > objBytes[obj_index].length) throw new Error(`bin 分包数据越界 seq=${seq}`);
    objBytes[obj_index].set(frame.subarray(9, 9 + len), offset);

    chunks.push({ seq, obj_index, offset, len });
    pos += frameLen;
  }

  if (pos + 7 > buf.length || buf[pos] !== CFGWRITE_END_CMD) throw new Error('bin 结束帧格式错误');
  const endFrame = buf.subarray(pos, pos + 7);
  const endCrc = endFrame[5] | (endFrame[6] << 8);
  if (crc16ModbusIBM(endFrame.subarray(0, 5)) !== endCrc) throw new Error('bin 结束帧 CRC 错误');

  return {
    protocolVersion,
    total_objects,
    total_chunks,
    total_bytes,
    objectsMeta,
    chunks,
    objBytes
  };
}

function toHex2(v: number): string {
  return (v & 0xFF).toString(16).toUpperCase().padStart(2, '0');
}

function formatPreparedBinAsText(parsed: ParsedPreparedBinData): string {
  const lines: string[] = [];
  lines.push('[PREPARED_BIN_INFO]');
  lines.push(`PROTOCOL_VERSION=${parsed.protocolVersion}`);
  lines.push(`TOTAL_OBJECTS=${parsed.total_objects}`);
  lines.push(`TOTAL_CHUNKS=${parsed.total_chunks}`);
  lines.push(`TOTAL_BYTES=${parsed.total_bytes}`);
  lines.push('');

  lines.push('[OBJECTS_META]');
  parsed.objectsMeta.forEach((m, idx) => {
    lines.push(`OBJECT[${idx}].ADDRESS=${m.object_address}`);
    lines.push(`OBJECT[${idx}].SIZE=${m.object_size}`);
  });
  lines.push('');

  lines.push('[CHUNKS]');
  for (const ch of parsed.chunks) {
    lines.push(`SEQ=${ch.seq} OBJ_INDEX=${ch.obj_index} OFFSET=${ch.offset} LEN=${ch.len}`);
  }
  lines.push('');

  lines.push('[OBJECT_BYTES_HEX]');
  parsed.objBytes.forEach((bytes, idx) => {
    lines.push(`OBJECT[${idx}] SIZE=${bytes.length}`);
    let line = '';
    for (let i = 0; i < bytes.length; i++) {
      line += toHex2(bytes[i]);
      if (i < bytes.length - 1) line += ' ';
      if ((i + 1) % 32 === 0) {
        lines.push(line);
        line = '';
      }
    }
    if (line) lines.push(line);
    lines.push('');
  });

  return lines.join('\n');
}

/** 须与固件 `CFG_MAX_DATA_PER_FRAME` 一致 */
const CFG_XFER_MAX_CHUNK = CFG_MAX_CHUNK;

function buildPreparedXcfgData(
  xcfgContent: string,
  fileName: string,
  backupName: string,
  outputDir: string,
  options?: { writePreparedFile?: boolean }
): PreparedXcfgData {
  const writePreparedFile = options?.writePreparedFile ?? true;
  const CFG_MAX_DATA_PER_FRAME = CFG_XFER_MAX_CHUNK;

  const parsed = parseXcfg(xcfgContent) as XcfgData;
  assertObjectCountWithinMcuLimit(parsed.objects?.length ?? 0);

  const objBytes = parsed.objects.map(o => buildObjectBytes(o));
  const objectsMeta = parsed.objects.map(o => ({ object_address: o.object_address, object_size: o.object_size }));
  const chunks: PreparedXcfgChunk[] = [];
  for (let obj_index = 0; obj_index < parsed.objects.length; obj_index++) {
    const bytes = objBytes[obj_index];
    for (let offset = 0; offset < bytes.length; offset += CFG_MAX_DATA_PER_FRAME) {
      const len = Math.min(CFG_MAX_DATA_PER_FRAME, bytes.length - offset);
      const seq = chunks.length + 1;
      chunks.push({ seq, obj_index, offset, data: bytes.slice(offset, offset + len) });
    }
  }
  const total_objects = parsed.objects.length;
  const total_chunks = chunks.length;
  const total_bytes = objBytes.reduce((a, b) => a + b.length, 0);
  if (total_chunks === 0) throw new Error('object bytes 为空');

  const startWithoutCrc: number[] = [CFGWRITE_START_CMD, CFG_PROTOCOL_VERSION, ...u16le(total_objects), ...u16le(total_chunks), ...u32le(total_bytes)];
  for (const m of objectsMeta) {
    startWithoutCrc.push(...u16le(m.object_address));
    startWithoutCrc.push(...u16le(m.object_size));
  }
  const startFrame = buildFrameWithCRC(startWithoutCrc);
  const endSeq = total_chunks + 1;
  const endFrame = buildFrameWithCRC([CFGWRITE_END_CMD, ...u16le(endSeq), ...u16le(0)]);

  let preparedFilePath = '';
  if (writePreparedFile) {
    const merged: Buffer[] = [Buffer.from(startFrame)];
    for (const ch of chunks) {
      const chunkBytes: number[] = [CFGWRITE_CHUNK_CMD, ...u16le(ch.seq), ...u16le(ch.obj_index), ...u16le(ch.offset), ...u16le(ch.data.length), ...Array.from(ch.data)];
      merged.push(Buffer.from(buildFrameWithCRC(chunkBytes)));
    }
    merged.push(Buffer.from(endFrame));

    fs.mkdirSync(outputDir, { recursive: true });
    const parsedName = path.parse(fileName || 'config1.xcfg');
    const outName = `${parsedName.name || 'config1'}-prepared.bin`;
    preparedFilePath = path.join(outputDir, outName);
    fs.writeFileSync(preparedFilePath, Buffer.concat(merged));
  }

  return {
    token: `${Date.now()}-${Math.random().toString(36).slice(2, 10)}`,
    fileName,
    backupName,
    total_objects,
    total_chunks,
    total_bytes,
    chunks,
    objectsMeta,
    parsed,
    objBytes,
    preparedFilePath,
    preparedAt: Date.now()
  };
}

function sendXcfgTransferProgress(progress: {
  phase: string;
  portPath?: string;
  fileName?: string;
  current?: number;
  total?: number;
  percent?: number;
  message?: string;
}) {
  broadcastToRendererWindows('xcfg-transfer-progress', progress);
}

// 串口连接管理
interface SerialPortInstance {
  port: any;
  isOpen: boolean;
  readActive: boolean;
  baudRate: number;
}

const serialPorts = new Map<string, SerialPortInstance>();

// WinUSB 连接管理（STM32 CDC 使用 interface 1, bulk 0x01 OUT / 0x81 IN）
const WINUSB_PREFIX = 'winusb:';
interface WinUsbInstance {
  device: any;
  iface: any;
  inEp: any;
  outEp: any;
  readActive: boolean;
}
const winUsbConnections = new Map<string, WinUsbInstance>();
const cancelledXcfgTransfers = new Set<string>();

function parseUsbId(usbId: string): { bus: number; device: number } | null {
  const m = usbId.match(/^usb:(\d{3})-(\d{3})$/);
  if (!m) return null;
  return { bus: parseInt(m[1], 10), device: parseInt(m[2], 10) };
}

async function connectWinUsb(portPath: string): Promise<{ path: string }> {
  const usb = require('usb') as { getDeviceList: () => any[] };
  const vid = 0x0483, pid = 0x5740;
  let targetBus: number | null = null, targetAddr: number | null = null;

  const raw = portPath.startsWith(WINUSB_PREFIX) ? portPath.slice(WINUSB_PREFIX.length) : portPath;
  const parsed = parseUsbId(raw);
  if (parsed) {
    targetBus = parsed.bus;
    targetAddr = parsed.device;
  }

  const devices = usb.getDeviceList();
  let dev: any = null;
  for (const d of devices) {
    const desc = d.deviceDescriptor || d;
    const v = desc.idVendor ?? desc.vendorId;
    const p = desc.idProduct ?? desc.productId;
    if (v !== vid || p !== pid) continue;
    const bus = d.busNumber ?? d.bus?.busNumber ?? 0;
    const addr = d.deviceAddress ?? d.device?.deviceAddress ?? 0;
    if (targetBus != null && targetAddr != null) {
      if (bus === targetBus && addr === targetAddr) {
        dev = d;
        break;
      }
    } else {
      dev = d;
      break;
    }
  }
  if (!dev) throw new Error('未找到 WinUSB 设备 (0483:5740)，请确认已用 Zadig 安装 WinUSB 驱动');

  dev.open();
  const ifaces = dev.interfaces || [];
  let iface = ifaces[1] ?? ifaces.find((i: any) => (i.interfaceNumber ?? i.interface ?? i.bInterfaceNumber) === 1);
  if (!iface) iface = ifaces[0];
  if (!iface) {
    dev.close();
    throw new Error('未找到 USB 接口');
  }
  try {
    iface.claim();
  } catch (e: any) {
    dev.close();
    throw new Error('无法声明接口: ' + (e?.message || e) + '。请关闭占用该设备的其他程序。');
  }

  const eps = iface.endpoints || iface.endpointDescriptions || [];
  const inEp = eps.find((e: any) => {
    const a = e.address ?? e.endpointAddress ?? e.bEndpointAddress ?? 0;
    return (a & 0x80) !== 0;
  });
  const outEp = eps.find((e: any) => {
    const a = e.address ?? e.endpointAddress ?? e.bEndpointAddress ?? 0;
    return (a & 0x80) === 0;
  });
  if (!inEp || !outEp) {
    try { iface.release(true); } catch (_) {}
    dev.close();
    throw new Error('未找到 bulk 端点');
  }

  const connPath = portPath.startsWith(WINUSB_PREFIX) ? portPath : WINUSB_PREFIX + raw;
  winUsbConnections.set(connPath, {
    device: dev,
    iface,
    inEp,
    outEp,
    readActive: false
  });
  console.log(`WinUSB ${connPath} opened`);
  return { path: connPath };
}

function disconnectWinUsb(portPath: string): Promise<void> {
  return new Promise((resolve) => {
    const conn = winUsbConnections.get(portPath);
    if (!conn) {
      resolve();
      return;
    }
    conn.readActive = false;
    try {
      conn.iface?.release?.(true);
    } catch (_) {}
    try {
      conn.device?.close?.();
    } catch (_) {}
    winUsbConnections.delete(portPath);
    console.log(`WinUSB ${portPath} closed`);
    resolve();
  });
}

function writeWinUsb(portPath: string, data: Buffer): Promise<void> {
  return new Promise((resolve, reject) => {
    const conn = winUsbConnections.get(portPath);
    if (!conn) return reject(new Error('WinUSB 未连接'));
    conn.outEp.transfer(data, (err: Error | undefined) => {
      if (err) reject(err);
      else resolve();
    });
  });
}

function startWinUsbRead(portPath: string): void {
  const conn = winUsbConnections.get(portPath);
  if (!conn) return;
  conn.readActive = true;
  const doRead = () => {
    if (!conn.readActive || !winUsbConnections.has(portPath)) return;
    conn.inEp.transfer(4096, (err: Error | undefined, data: Buffer | undefined) => {
      if (!conn.readActive) return;
      if (err) {
        broadcastToRendererWindows('serial-error', portPath, err.message);
        return;
      }
      if (data && data.length > 0) broadcastToRendererWindows('serial-data', portPath, Array.from(data));
      doRead();
    });
  };
  doRead();
}

function stopWinUsbRead(portPath: string): void {
  const conn = winUsbConnections.get(portPath);
  if (conn) conn.readActive = false;
}

async function disconnectSerialPort(portPath: string): Promise<void> {
  const portInstance = serialPorts.get(portPath);
  if (!portInstance || !portInstance.isOpen) return;
  portInstance.readActive = false;
  try {
    portInstance.port.removeAllListeners('data');
    portInstance.port.removeAllListeners('error');
    portInstance.port.removeAllListeners('close');
  } catch (_) {}
  await new Promise<void>((resolve) => {
    portInstance.port.close(() => {
      serialPorts.delete(portPath);
      resolve();
    });
  });
}

async function connectSerialPort(portPath: string, baudRate = 115200): Promise<{ path: string }> {
  if (serialPorts.has(portPath)) await disconnectSerialPort(portPath);
  const { SerialPort } = await import('serialport');
  const port = new SerialPort({ path: portPath, baudRate, autoOpen: false });
  await new Promise<void>((resolve, reject) => {
    port.open((err: Error | null | undefined) => (err ? reject(err) : resolve()));
  });
  serialPorts.set(portPath, { port, isOpen: true, readActive: false, baudRate });
  return { path: portPath };
}

function startSerialRead(portPath: string): void {
  const portInstance = serialPorts.get(portPath);
  if (!portInstance || !portInstance.isOpen) return;
  portInstance.readActive = true;
  portInstance.port.removeAllListeners('data');
  portInstance.port.removeAllListeners('error');
  portInstance.port.on('data', (data: Buffer) => {
    broadcastToRendererWindows('serial-data', portPath, Array.from(data));
  });
  portInstance.port.on('error', (error: Error) => {
    broadcastToRendererWindows('serial-error', portPath, error.message);
  });
  portInstance.port.on('close', () => {
    serialPorts.delete(portPath);
    broadcastToRendererWindows('serial-close', portPath);
  });
}

function isWinUsbPath(p: string): boolean {
  return typeof p === 'string' && p.startsWith(WINUSB_PREFIX);
}

interface WinUsbPresenceResult {
  present: boolean;
  instanceId?: string;
  friendlyName?: string;
  driverName?: string;
  status?: string;
  error?: string;
  /** 来自 mxt-app -q (libusb/WinUSB) 的设备标识 */
  mxtAppUsbId?: string;
  mxtAppProduct?: string;
  source?: 'mxt-app' | 'powershell';
}

const usbPresenceCache = new Map<string, { ts: number; result: WinUsbPresenceResult }>();
const usbPresenceInFlight = new Map<string, Promise<WinUsbPresenceResult>>();
const USB_PRESENCE_CACHE_TTL_MS = 1500;

function checkWindowsUsbPresence(vendorIdHex: string, productIdHex: string): Promise<WinUsbPresenceResult> {
  return new Promise((resolve) => {
    if (process.platform !== 'win32') {
      resolve({ present: false, error: 'unsupported-platform' });
      return;
    }

    const vid = (vendorIdHex || '').toUpperCase().replace(/^0X/, '').padStart(4, '0');
    const pid = (productIdHex || '').toUpperCase().replace(/^0X/, '').padStart(4, '0');

    // 方法1: Get-PnpDevice (含 WinUSB 等各类 USB 设备)
    // 分别匹配 VID/PID 避免 & 等特殊字符问题，兼容 USB\VID_xxx&PID_xxx 格式
    const psCmd1 = `
      $vidVal = '${vid}'; $pidVal = '${pid}'
      $d = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object {
        $id = if ($_.InstanceId) { $_.InstanceId.ToUpper() } else { '' }
        $id -match "VID_${vid}" -and $id -match "PID_${pid}"
      } | Select-Object -First 1
      if ($d) { $d | ConvertTo-Json -Compress }
    `;

    // 方法2: WMI Win32_PnPEntity (备选，兼容部分环境)
    const psCmd2 = `
      $vidVal = '${vid}'; $pidVal = '${pid}'
      $d = Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue | Where-Object {
        $id = if ($_.PnPDeviceID) { $_.PnPDeviceID.ToUpper() } else { '' }
        $id -match "VID_${vid}" -and $id -match "PID_${pid}"
      } | Select-Object -First 1 -Property PnPDeviceID, Name, Status
      if ($d) { $d | ConvertTo-Json -Compress }
    `;

    const tryRun = (cmd: string, useWmi: boolean) => {
      execFile(
        'powershell.exe',
        ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', cmd],
        { windowsHide: true, timeout: 8000 },
        (error, stdout, stderr) => {
          if (error) {
            if (useWmi) resolve({ present: false, error: error.message });
            else tryRun(psCmd2, true); // 方法1 失败则尝试方法2
            return;
          }
          const out = (stdout || '').trim();
          if (!out) {
            if (useWmi) resolve({ present: false });
            else tryRun(psCmd2, true);
            return;
          }
          try {
            const obj = JSON.parse(out);
            const instanceId = obj.InstanceId || obj.PnPDeviceID || '';
            const friendlyName = obj.FriendlyName || obj.Name || '';
            const status = obj.Status || '';
            const driverName = obj.DriverProviderName || obj.Service || '';
            resolve({
              present: instanceId.length > 0,
              instanceId: instanceId || undefined,
              friendlyName: friendlyName || undefined,
              driverName: driverName || undefined,
              status: status || undefined,
              source: 'powershell'
            });
          } catch {
            resolve({ present: out.length > 0, instanceId: out || undefined });
          }
        }
      );
    };

    tryRun(psCmd1, false);
  });
}

/** Windows 任务栏与窗口图标：开发用仓库内 build/icon.ico；打包后为 resources/icon.ico（package.json extraResources） */
function resolveWindowsAppIconPath(): string | undefined {
  if (process.platform !== 'win32') return undefined;
  try {
    const packagedIcon = path.join(process.resourcesPath, 'icon.ico');
    const devIcon = path.resolve(__dirname, '..', '..', 'build', 'icon.ico');
    const primary = app.isPackaged ? packagedIcon : devIcon;
    if (fs.existsSync(primary)) return primary;
    if (!app.isPackaged && fs.existsSync(packagedIcon)) return packagedIcon;
  } catch (_) {}
  return undefined;
}

function createWindow() {
  const winIcon = resolveWindowsAppIconPath();
  mainWindow = new BrowserWindow({
    width: 1400,
    height: 900,
    show: false,
    autoHideMenuBar: true,
    ...(winIcon ? { icon: winIcon } : {}),
    webPreferences: {
      preload: path.join(__dirname, '../preload/index.js'),
      nodeIntegration: false,
      contextIsolation: true,
      sandbox: false,
      webSecurity: false,
      // 不再依赖 Web Serial，改为使用主进程 serialport + IPC
    }
  });

  // 隐藏应用菜单栏（去掉 File / Edit / View / Window / Help 等）
  Menu.setApplicationMenu(null);

  if (process.env.NODE_ENV === 'development') {
    mainWindow.loadURL('http://localhost:5173');
    mainWindow.webContents.openDevTools();
  } else {
    mainWindow.loadFile(path.join(__dirname, '../renderer/index.html'));
  }

  mainWindow.once('ready-to-show', () => {
    if (mainWindow && !mainWindow.isDestroyed()) mainWindow.show();
  });

  mainWindow.on('closed', () => {
    mainWindow = null;
  });
}

async function openXcfgViewerWindow(payload?: { page?: string; xcfgContent?: string; fileName?: string }) {
  const page = String(payload?.page || 'index.html').replace(/\\/g, '/');
  const pageName = path.basename(page);
  const key = pageName;

  if (payload?.xcfgContent) {
    setXcfgViewerInitialPayload({
      content: payload.xcfgContent,
      fileName: payload.fileName
    });
  }

  const existing = xcfgViewerWindows.get(key);
  if (existing && !existing.isDestroyed()) {
    if (payload?.xcfgContent) {
      if (process.env.NODE_ENV === 'development') {
        await existing.loadURL(`http://localhost:5173/xcfg-viewer/${pageName}`);
      } else {
        await existing.loadFile(path.join(__dirname, '../renderer/xcfg-viewer', pageName));
      }
    }
    existing.show();
    existing.focus();
    return { success: true, reused: true };
  }

  const winIcon = resolveWindowsAppIconPath();
  const viewerWindow = new BrowserWindow({
    width: 1280,
    height: 860,
    minWidth: 900,
    minHeight: 600,
    autoHideMenuBar: true,
    title: 'maXTouch xcfg 配置查看器',
    ...(winIcon ? { icon: winIcon } : {}),
    webPreferences: {
      preload: path.join(__dirname, '../preload/xcfg-viewer-preload.js'),
      nodeIntegration: false,
      contextIsolation: true,
      sandbox: false,
      webSecurity: false
    }
  });

  if (process.env.NODE_ENV === 'development') {
    await viewerWindow.loadURL(`http://localhost:5173/xcfg-viewer/${pageName}`);
  } else {
    await viewerWindow.loadFile(path.join(__dirname, '../renderer/xcfg-viewer', pageName));
  }

  viewerWindow.on('closed', () => {
    xcfgViewerWindows.delete(key);
  });
  xcfgViewerWindows.set(key, viewerWindow);
  return { success: true };
}

function createSplashWindow() {
  const winIcon = resolveWindowsAppIconPath();
  splashWindow = new BrowserWindow({
    width: 520,
    height: 320,
    resizable: false,
    minimizable: false,
    maximizable: false,
    frame: false,
    show: true,
    autoHideMenuBar: true,
    alwaysOnTop: true,
    ...(winIcon ? { icon: winIcon } : {}),
    webPreferences: {
      nodeIntegration: false,
      contextIsolation: true,
      sandbox: false
    }
  });

  if (process.env.NODE_ENV === 'development') {
    splashWindow.loadURL('http://localhost:5173/splash.html');
  } else {
    splashWindow.loadFile(path.join(__dirname, '../renderer/splash.html'));
  }

  splashWindow.on('closed', () => {
    splashWindow = null;
  });
}

async function bootWithSplash() {
  if (process.platform === 'win32') {
    try {
      app.setAppUserModelId('com.yuanchu.serialterminal');
    } catch (_) {}
  }
  createSplashWindow();
  const startedAt = Date.now();
  const minSplashMs = 1300;
  const passed = isWithinRuntimeWindow(30);
  const waitMs = Math.max(0, minSplashMs - (Date.now() - startedAt));
  if (waitMs > 0) await new Promise<void>((resolve) => setTimeout(resolve, waitMs));

  if (!passed) {
    try {
      if (splashWindow && !splashWindow.isDestroyed()) splashWindow.close();
    } catch (_) {}
    app.exit(0);
    return;
  }

  createWindow();
  if (mainWindow) {
    mainWindow.once('ready-to-show', () => {
      try {
        if (splashWindow && !splashWindow.isDestroyed()) splashWindow.close();
      } catch (_) {}
    });
  } else {
    try {
      if (splashWindow && !splashWindow.isDestroyed()) splashWindow.close();
    } catch (_) {}
  }
}

// 处理串口设备检测请求
ipcMain.handle('get-serial-ports', async () => {
  try {
    // 动态导入 serialport 库，避免启动时依赖
    const { SerialPort } = await import('serialport');
    
    // 获取所有可用的串口设备
    const ports = await SerialPort.list();
    console.log('Available serial ports:', ports);
    
    return ports;
  } catch (error) {
    console.error('Error getting serial ports:', error);
    return [];
  }
});

ipcMain.handle('get-last-xcfg-dir', async () => {
  return { success: true, dir: lastXcfgDir || process.cwd() };
});

ipcMain.handle('get-runtime-window-config', async () => {
  const cfg = getRuntimeWindowDisplayConfig();
  return { success: true, ...cfg };
});

ipcMain.handle('get-app-mode', async () => {
  return { success: true, mode: getBuildMode() };
});

ipcMain.handle('get-app-paths', async () => {
  try {
    return {
      success: true,
      userData: app.getPath('userData'),
      sessionData: app.getPath('sessionData'),
      exePath: process.execPath
    };
  } catch (error: any) {
    return { success: false, error: error?.message || String(error) };
  }
});

ipcMain.handle('append-serial-log', async (_event: Electron.IpcMainInvokeEvent, payload: { text?: string }) => {
  try {
    const text = String(payload?.text || '');
    if (!text) return { success: true, appendedChars: 0, filePath: path.join(app.getPath('userData'), 'logs', 'serial-terminal.log') };
    const logDir = path.join(app.getPath('userData'), 'logs');
    const logPath = path.join(logDir, 'serial-terminal.log');
    fs.mkdirSync(logDir, { recursive: true });
    fs.appendFileSync(logPath, text, 'utf-8');
    return { success: true, filePath: logPath, appendedChars: text.length };
  } catch (error: any) {
    return { success: false, error: error?.message || String(error) };
  }
});

ipcMain.handle('show-serial-log-in-explorer', async () => {
  try {
    const logDir = path.join(app.getPath('userData'), 'logs');
    const logPath = path.join(logDir, 'serial-terminal.log');
    fs.mkdirSync(logDir, { recursive: true });
    if (!fs.existsSync(logPath)) fs.writeFileSync(logPath, '', 'utf-8');
    shell.showItemInFolder(logPath);
    return { success: true, filePath: logPath };
  } catch (error: any) {
    return { success: false, error: error?.message || String(error) };
  }
});

ipcMain.handle('open-detached-modal-window', async (_event: Electron.IpcMainInvokeEvent, payload: {
  modalId: string;
  modalKey?: string;
  row?: number;
  col?: number;
  freqType?: string;
}) => {
  try {
    const modalId = String(payload?.modalId || '').trim();
    if (!modalId) return { success: false, error: 'missing modalId' };
    const modalKey = String(payload?.modalKey || modalId).trim() || modalId;
    const key = `${modalId}::${modalKey}`;
    const existing = detachedModalWindows.get(key);
    if (existing && !existing.isDestroyed()) {
      existing.show();
      existing.focus();
      return { success: true };
    }

    const winIcon = resolveWindowsAppIconPath();
    const popupWindow = new BrowserWindow({
      width: 840,
      height: 620,
      minWidth: 420,
      minHeight: 320,
      autoHideMenuBar: true,
      ...(winIcon ? { icon: winIcon } : {}),
      webPreferences: {
        preload: path.join(__dirname, '../preload/index.js'),
        nodeIntegration: false,
        contextIsolation: true,
        sandbox: false,
        webSecurity: false
      }
    });

    const query = new URLSearchParams();
    query.set('popupModal', modalId);
    query.set('popupKey', modalKey);
    if (typeof payload?.row === 'number') query.set('row', String(payload.row));
    if (typeof payload?.col === 'number') query.set('col', String(payload.col));
    if (payload?.freqType) query.set('freqType', String(payload.freqType));
    const queryText = query.toString();

    if (process.env.NODE_ENV === 'development') {
      await popupWindow.loadURL(`http://localhost:5173/?${queryText}`);
    } else {
      await popupWindow.loadFile(path.join(__dirname, '../renderer/index.html'), { query: Object.fromEntries(query.entries()) });
    }

    popupWindow.on('closed', () => {
      detachedModalWindows.delete(key);
    });
    detachedModalWindows.set(key, popupWindow);
    return { success: true };
  } catch (error: any) {
    return { success: false, error: error?.message || String(error) };
  }
});

ipcMain.handle('select-xcfg-file', async () => {
  try {
    const result = await dialog.showOpenDialog({
      title: '选择 XCFG 文件',
      properties: ['openFile'],
      defaultPath: lastXcfgDir || process.cwd(),
      filters: [{ name: 'XCFG', extensions: ['xcfg'] }]
    });
    if (result.canceled || !result.filePaths || result.filePaths.length === 0) {
      return { success: false, canceled: true };
    }
    const filePath = result.filePaths[0];
    const dir = path.dirname(filePath);
    lastXcfgDir = dir;
    const content = fs.readFileSync(filePath, 'utf-8');
    return {
      success: true,
      filePath,
      fileName: path.basename(filePath),
      dir,
      content
    };
  } catch (error: any) {
    return { success: false, error: error?.message || String(error) };
  }
});

ipcMain.handle('select-config-file', async () => {
  try {
    const result = await dialog.showOpenDialog({
      title: '选择 XCFG/BIN/ENC 文件',
      properties: ['openFile'],
      defaultPath: lastXcfgDir || process.cwd(),
      filters: [{ name: 'Config / Firmware', extensions: ['xcfg', 'bin', 'enc'] }]
    });
    if (result.canceled || !result.filePaths || result.filePaths.length === 0) {
      return { success: false, canceled: true };
    }
    const filePath = result.filePaths[0];
    const dir = path.dirname(filePath);
    const fileName = path.basename(filePath);
    const ext = path.extname(filePath).toLowerCase();
    lastXcfgDir = dir;
    if (ext === '.bin') {
      const bin = fs.readFileSync(filePath);
      return { success: true, kind: 'bin', filePath, fileName, dir, binBase64: bin.toString('base64') };
    }
    if (ext === '.enc') {
      return { success: true, kind: 'enc', filePath, fileName, dir };
    }
    const content = fs.readFileSync(filePath, 'utf-8');
    return { success: true, kind: 'xcfg', filePath, fileName, dir, content };
  } catch (error: any) {
    return { success: false, error: error?.message || String(error) };
  }
});

// 连接串口（支持 COM 口或 WinUSB）
ipcMain.handle('connect-serial-port', async (_event: Electron.IpcMainInvokeEvent, portPath: string, baudRate: number = 115200) => {
  try {
    if (isWinUsbPath(portPath)) {
      if (winUsbConnections.has(portPath)) await disconnectWinUsb(portPath);
      return await connectWinUsb(portPath);
    }

    if (serialPorts.has(portPath)) {
      const existing = serialPorts.get(portPath);
      if (existing && existing.isOpen) {
        await existing.port.close();
        serialPorts.delete(portPath);
      }
    }

    const { SerialPort } = await import('serialport');
    const port = new SerialPort({
      path: portPath,
      baudRate: baudRate,
      autoOpen: false,
    });

    return new Promise((resolve, reject) => {
      port.open((err: Error | null | undefined) => {
        if (err) {
          console.error('Error opening serial port:', err);
          reject(err);
          return;
        }

        console.log(`Serial port ${portPath} opened successfully`);
        
        serialPorts.set(portPath, {
          port: port,
          isOpen: true,
          readActive: false,
          baudRate
        });

        // 获取端口信息
        const portInfo = {
          path: portPath,
          baudRate: baudRate,
        };

        resolve(portInfo);
      });
    });
  } catch (error) {
    console.error('Error connecting to serial port:', error);
    throw error;
  }
});

// 断开串口
ipcMain.handle('disconnect-serial-port', async (_event: Electron.IpcMainInvokeEvent, portPath: string) => {
  try {
    if (isWinUsbPath(portPath)) {
      await disconnectWinUsb(portPath);
      broadcastToRendererWindows('serial-close', portPath);
      return;
    }
    const portInstance = serialPorts.get(portPath);
    if (portInstance && portInstance.isOpen) {
      return new Promise<void>((resolve, reject) => {
        portInstance.port.close((err: any) => {
          if (err) {
            console.error('Error closing serial port:', err);
            reject(err);
            return;
          }
          
          console.log(`Serial port ${portPath} closed successfully`);
          serialPorts.delete(portPath);
          resolve();
        });
      });
    }
    return Promise.resolve();
  } catch (error) {
    console.error('Error disconnecting serial port:', error);
    throw error;
  }
});

// 写入数据到串口
ipcMain.handle('write-serial-port', async (_event: Electron.IpcMainInvokeEvent, portPath: string, data: string | Uint8Array) => {
  try {
    if (isWinUsbPath(portPath)) {
      const buffer = typeof data === 'string' ? Buffer.from(data, 'utf8') : Buffer.from(data);
      await writeWinUsb(portPath, buffer);
      return;
    }
    const portInstance = serialPorts.get(portPath);
    if (!portInstance || !portInstance.isOpen) {
      throw new Error('Serial port is not open');
    }

    return new Promise<void>((resolve, reject) => {
      const buffer = typeof data === 'string' ? Buffer.from(data, 'utf8') : Buffer.from(data);
      portInstance.port.write(buffer, (err: any) => {
        if (err) {
          console.error('Error writing to serial port:', err);
          reject(err);
          return;
        }
        resolve();
      });
    });
  } catch (error) {
    console.error('Error writing to serial port:', error);
    throw error;
  }
});

// 监听串口数据（通过事件发送给渲染进程）
ipcMain.handle('start-serial-read', async (_event: Electron.IpcMainInvokeEvent, portPath: string) => {
  try {
    if (isWinUsbPath(portPath)) {
      startWinUsbRead(portPath);
      return true;
    }
    const portInstance = serialPorts.get(portPath);
    if (!portInstance || !portInstance.isOpen) {
      throw new Error('Serial port is not open');
    }

    portInstance.readActive = true;

    // 移除之前的监听器（如果存在）
    portInstance.port.removeAllListeners('data');
    portInstance.port.removeAllListeners('error');

    // 设置数据监听器
    portInstance.port.on('data', (data: Buffer) => {
      // 将数据发送到渲染进程
      broadcastToRendererWindows('serial-data', portPath, Array.from(data));
    });

    // 设置错误监听器
    portInstance.port.on('error', (error: Error) => {
      console.error('Serial port error:', error);
      broadcastToRendererWindows('serial-error', portPath, error.message);
    });

    // 设置关闭监听器
    portInstance.port.on('close', () => {
      console.log(`Serial port ${portPath} was closed`);
      serialPorts.delete(portPath);
      broadcastToRendererWindows('serial-close', portPath);
    });

    return true;
  } catch (error) {
    console.error('Error starting serial read:', error);
    throw error;
  }
});

// 停止串口数据监听
ipcMain.handle('stop-serial-read', async (_event: Electron.IpcMainInvokeEvent, portPath: string) => {
  try {
    if (isWinUsbPath(portPath)) {
      stopWinUsbRead(portPath);
      return true;
    }
    const portInstance = serialPorts.get(portPath);
    if (portInstance && portInstance.port) {
      portInstance.port.removeAllListeners('data');
      portInstance.port.removeAllListeners('error');
      portInstance.port.removeAllListeners('close');
    }
    return true;
  } catch (error) {
    console.error('Error stopping serial read:', error);
    throw error;
  }
});

// 检测 USB 设备是否存在（即使没有 COM 口）
// 参考 xcfg-viewer：优先用 mxt-app -q (libusb) 枚举 WinUSB 设备，失败则回退到 PowerShell
ipcMain.handle('check-usb-device-presence', async (
  _event: Electron.IpcMainInvokeEvent,
  vendorIdHex: string,
  productIdHex: string,
  options?: { allowPowerShellFallback?: boolean; forceRefresh?: boolean }
) => {
  const vid = (vendorIdHex || '').replace(/^0x/i, '').padStart(4, '0').toLowerCase();
  const pid = (productIdHex || '').replace(/^0x/i, '').padStart(4, '0').toLowerCase();
  const allowPowerShellFallback = options?.allowPowerShellFallback !== false;
  const forceRefresh = options?.forceRefresh === true;
  const cacheKey = `${vid}:${pid}:${allowPowerShellFallback ? 'ps-on' : 'ps-off'}`;
  const now = Date.now();

  if (!forceRefresh) {
    const cached = usbPresenceCache.get(cacheKey);
    if (cached && now - cached.ts < USB_PRESENCE_CACHE_TTL_MS) {
      return cached.result;
    }
  }

  const inFlight = usbPresenceInFlight.get(cacheKey);
  if (inFlight) return await inFlight;

  const task = (async (): Promise<WinUsbPresenceResult> => {
    try {
      const mxt = await checkWinUsbViaMxtApp(vendorIdHex, productIdHex);
      if (mxt.present && mxt.devices && mxt.devices.length > 0) {
        const d = mxt.devices.find(x =>
          x.vidPid.toLowerCase() === `${vid}:${pid}`
        ) || mxt.devices[0];
        const result: WinUsbPresenceResult = {
          present: true,
          mxtAppUsbId: d.usbId,
          mxtAppProduct: d.product,
          source: 'mxt-app'
        };
        usbPresenceCache.set(cacheKey, { ts: Date.now(), result });
        return result;
      }

      // 自动检测场景禁用 PowerShell 回退，避免频繁拉起 powershell.exe
      if (!allowPowerShellFallback) {
        const result: WinUsbPresenceResult = { present: false, source: 'mxt-app' };
        usbPresenceCache.set(cacheKey, { ts: Date.now(), result });
        return result;
      }

      const result = await checkWindowsUsbPresence(vendorIdHex, productIdHex);
      usbPresenceCache.set(cacheKey, { ts: Date.now(), result });
      return result;
    } catch (error: any) {
      const result: WinUsbPresenceResult = { present: false, error: error?.message || String(error) };
      usbPresenceCache.set(cacheKey, { ts: Date.now(), result });
      return result;
    } finally {
      usbPresenceInFlight.delete(cacheKey);
    }
  })();

  usbPresenceInFlight.set(cacheKey, task);
  return await task;
});

// 参考 xcfg-viewer 的 --load / --save 流程：上传 xcfg 内容 -> test-V1 写入 -> 自动备份
ipcMain.handle('write-xcfg-and-backup', async (_event: Electron.IpcMainInvokeEvent, payload: {
  device?: string;
  fileName?: string;
  xcfgContent: string;
  backupName?: string;
}) => {
  const device = (payload?.device || '').trim() || getDefaultMxtDeviceFromSerialApp() || undefined;
  const fileName = (payload?.fileName || 'config1.xcfg').trim();
  const xcfgContent = payload?.xcfgContent;
  const backupName = sanitizeBackupFileName(payload?.backupName || 'test-V1');

  if (!xcfgContent || !xcfgContent.trim()) {
    return { success: false, error: 'xcfg 内容为空' };
  }

  const baseTmpDir = path.join(getLocalTempDir(), 'serial-app-xcfg');
  fs.mkdirSync(baseTmpDir, { recursive: true });

  const writePath = path.join(baseTmpDir, `${Date.now()}-${path.basename(fileName)}`);
  const backupPath = path.join(baseTmpDir, backupName);

  try {
    fs.writeFileSync(writePath, xcfgContent, 'utf-8');

    // 先写入（等价 xcfg-viewer 的 --load）
    const writeResult = await runMxtAppCoordinated(['--load', writePath], device, 90000);
    if (!writeResult.success) {
      return {
        success: false,
        phase: 'write',
        error: writeResult.stderr || writeResult.stdout || `写入失败(${writeResult.returncode})`,
        writeResult
      };
    }

    // 写入后自动备份（等价 xcfg-viewer 的 --save --format 3）
    const backupResult = await runMxtAppCoordinated(['--save', backupPath, '--format', '3'], device, 90000);
    if (!backupResult.success) {
      return {
        success: false,
        phase: 'backup',
        error: backupResult.stderr || backupResult.stdout || `备份失败(${backupResult.returncode})`,
        writeResult,
        backupResult
      };
    }

    const backupContent = fs.readFileSync(backupPath, 'utf-8');
    return {
      success: true,
      message: '写入成功并已自动备份',
      writeResult,
      backupResult,
      backupFileName: path.basename(backupPath),
      backupContent
    };
  } catch (error: any) {
    return {
      success: false,
      error: error?.message || String(error)
    };
  } finally {
    try { if (fs.existsSync(writePath)) fs.unlinkSync(writePath); } catch (_) {}
    try { if (fs.existsSync(backupPath)) fs.unlinkSync(backupPath); } catch (_) {}
  }
});

// 上传 xcfg 到 MCU：按对象写入 + CRC16/ACK + MCU 备份 NVM + 读回导出 xcfg
ipcMain.handle('write-xcfg-and-export-from-mcu', async (_event: Electron.IpcMainInvokeEvent, payload: {
  portPath: string;
  fileName?: string;
  xcfgContent: string;
  backupName?: string;
  preparedToken?: string;
  sourceDir?: string;
}) => {
  const portPath = (payload?.portPath || '').trim();
  const fileName = (payload?.fileName || 'config1.xcfg').trim();
  const backupName = sanitizeBackupFileName(payload?.backupName || 'test-V1');
  const xcfgContent = payload?.xcfgContent;
  const sourceDir = (payload?.sourceDir || '').trim();
  const safeOutputDir = (() => {
    if (sourceDir && path.isAbsolute(sourceDir) && fs.existsSync(sourceDir)) return sourceDir;
    const fallback = lastXcfgDir || process.cwd();
    return fallback;
  })();

  if (!portPath) return { success: false, error: '缺少 portPath' };
  if (!xcfgContent || !xcfgContent.trim()) return { success: false, error: 'xcfg 内容为空' };
  cancelledXcfgTransfers.delete(portPath);

  type AckFrame = { cmd: number; seq: number; status: number };
  type ReadDataFrame = { cmd: number; seq: number; obj_index: number; offset: number; data: Buffer };

  const CFG_MAX_DATA_PER_FRAME = CFG_XFER_MAX_CHUNK;
  const CFG_ACK_TIMEOUT_MS = 3000;

  const tryConsumeFrame = (rx: Buffer): { frame: AckFrame | ReadDataFrame | { cmd: number; status: number }; frameLen: number } | null => {
    if (rx.length < 1) return null;
    const cmd = rx[0];

    // ACK/NACK: 6 bytes: cmd | seq(u16) | status(u8) | crc16
    if (cmd === CFG_RESP_ACK_CMD || cmd === CFG_RESP_NACK_CMD) {
      if (rx.length < 6) return null;
      const seq = rx[1] | (rx[2] << 8);
      const status = rx[3];
      const crcRx = rx[4] | (rx[5] << 8);
      const crcCalc = crc16ModbusIBM(rx.subarray(0, 4));
      if (crcRx !== crcCalc) return null; // CRC 不通过：外层会丢弃一个字节重同步
      return { frame: { cmd, seq, status }, frameLen: 6 };
    }

    // Read data frame: E1 | seq(u16) | obj_index(u16) | offset(u16) | len(u16) | data | crc16
    if (cmd === CFGREAD_DATA_CMD) {
      if (rx.length < 9) return null;
      const seq = rx[1] | (rx[2] << 8);
      const obj_index = rx[3] | (rx[4] << 8);
      const offset = rx[5] | (rx[6] << 8);
      const len = rx[7] | (rx[8] << 8);
      const frameLen = 11 + len;
      if (rx.length < frameLen) return null;
      const data = rx.subarray(9, 9 + len);
      const crcRx = rx[9 + len] | (rx[10 + len] << 8);
      const crcCalc = crc16ModbusIBM(rx.subarray(0, frameLen - 2));
      if (crcRx !== crcCalc) return null;
      return { frame: { cmd, seq, obj_index, offset, data: Buffer.from(data) }, frameLen };
    }

    // Read end frame: E2 | status(u16) | crc16 (5 bytes)
    if (cmd === CFGREAD_END_CMD) {
      if (rx.length < 5) return null;
      const status = rx[1] | (rx[2] << 8);
      const crcRx = rx[3] | (rx[4] << 8);
      const crcCalc = crc16ModbusIBM(rx.subarray(0, 3));
      if (crcRx !== crcCalc) return null;
      return { frame: { cmd, status }, frameLen: 5 };
    }

    return null;
  };

  const sendBuf = async (data: Uint8Array, timeoutMs = 20000) => {
    if (cancelledXcfgTransfers.has(portPath)) throw new Error('transfer cancelled');
    await sendBufWithTimeout(data, timeoutMs, 'USB/串口写入超时');
  };
  const restoreStringMode = async () => {
    await sendBuf(bytesSwitchToMode1());
    await sleep(30);
  };
  const triggerHelpOutput = async () => {
    try {
      await sendBuf(Uint8Array.from(Buffer.from('help\n', 'ascii')));
      await sleep(40);
    } catch (_) {}
  };
  const probeT100Cfg = async (): Promise<{ success: boolean; message: string }> => {
    try {
      rxBuffer = Buffer.alloc(0);
      await sendBuf(Uint8Array.from(Buffer.from('T100CFG\n', 'ascii')));
      const deadline = Date.now() + 2000;
      while (Date.now() < deadline) {
        const chunk = await nextIncomingChunkWithTimeout(200).catch(() => Buffer.alloc(0));
        if (isWinUsbPath(portPath) && chunk.length > 0) {
          rxBuffer = Buffer.concat([rxBuffer, chunk]);
        }
        if (rxBuffer.length > 0) {
          const text = rxBuffer.toString('utf8');
          if (/UNKNOWN\[9\]|UNKNOWN\[20\]|T100/i.test(text)) return { success: true, message: 'T100CFG 自动执行成功' };
          if (/ERR|ERROR/i.test(text)) return { success: false, message: 'T100CFG 自动执行失败' };
        }
      }
      return { success: false, message: 'T100CFG 自动执行超时（无响应）' };
    } catch (_) {
      return { success: false, message: 'T100CFG 自动执行异常' };
    }
  };
  const sleep = (ms: number) => new Promise<void>(resolve => setTimeout(resolve, ms));
  const sendBufWithTimeout = async (data: Uint8Array, timeoutMs: number, timeoutMsg: string) => {
    await Promise.race([
      (async () => {
        if (isWinUsbPath(portPath)) {
          await writeWinUsb(portPath, Buffer.from(data));
          return;
        }
        const portInstance = serialPorts.get(portPath);
        if (!portInstance || !portInstance.isOpen) throw new Error('串口未连接');
        await new Promise<void>((resolve, reject) => {
          portInstance.port.write(Buffer.from(data), (err: Error | null | undefined) => {
            if (err) reject(err);
            else resolve();
          });
        });
      })(),
      new Promise<never>((_, reject) => setTimeout(() => reject(new Error(timeoutMsg)), timeoutMs))
    ]);
  };

  // test-V1 控制命令：按 CRC16 帧发送，设备可能不返回 ACK，因此这里采用“发送成功即继续”
  const sendControlCommand = async (command: number, phaseText: string, doneText?: string) => {
    sendXcfgTransferProgress({ phase: 'transfer', portPath, fileName, message: phaseText });
    const cmdFrame = buildFrameWithCRC([command]);
    // 控制命令阶段可能触发固件内部 I2C/NVM 操作，导致主机写入回调延迟
    await sendBufWithTimeout(cmdFrame, 20000, `${phaseText}超时`);
    await sleep(80);
    if (doneText) sendXcfgTransferProgress({ phase: 'transfer', portPath, fileName, message: doneText });
  };

  // 停止当前串口转发，避免与协议收包竞争
  if (isWinUsbPath(portPath)) {
    stopWinUsbRead(portPath);
  } else {
    const portInstance = serialPorts.get(portPath);
    portInstance?.port?.removeAllListeners?.('data');
    portInstance?.port?.removeAllListeners?.('error');
    portInstance?.port?.removeAllListeners?.('close');
  }

  // 协议收包缓存 + next chunk 等待
  let rxBuffer = Buffer.alloc(0);
  let notifyNextChunk: (() => void) | null = null;
  let nextChunkResolve: ((buf: Buffer) => void) | null = null;

  const attachRx = () => {
    if (isWinUsbPath(portPath)) return;
    const portInstance = serialPorts.get(portPath);
    if (!portInstance || !portInstance.isOpen) throw new Error('串口未连接');
    portInstance.port.on('data', (d: Buffer) => {
      rxBuffer = Buffer.concat([rxBuffer, d]);
      if (notifyNextChunk) {
        notifyNextChunk();
        notifyNextChunk = null;
      }
      if (nextChunkResolve) {
        nextChunkResolve(d);
        nextChunkResolve = null;
      }
    });
  };

  let winUsbRxPumpActive = false;
  const stopWinUsbRxPump = () => { winUsbRxPumpActive = false; };
  const startWinUsbRxPump = () => {
    if (!isWinUsbPath(portPath)) return;
    winUsbRxPumpActive = true;
    const pump = () => {
      if (!winUsbRxPumpActive || cancelledXcfgTransfers.has(portPath)) return;
      const conn = winUsbConnections.get(portPath);
      if (!conn) return;
      conn.inEp.transfer(4096, (err: Error | undefined, data: Buffer | undefined) => {
        if (!winUsbRxPumpActive) return;
        if (!err && data && data.length > 0) {
          rxBuffer = Buffer.concat([rxBuffer, data]);
          if (nextChunkResolve) {
            const resolve = nextChunkResolve;
            nextChunkResolve = null;
            resolve(data);
          }
        }
        if (winUsbRxPumpActive) pump();
      });
    };
    pump();
  };

  const nextIncomingChunk = async (): Promise<Buffer> => {
    if (isWinUsbPath(portPath)) {
      const conn = winUsbConnections.get(portPath);
      if (!conn) throw new Error('WinUSB 未连接');
      return await new Promise<Buffer>((resolve, reject) => {
        conn.inEp.transfer(4096, (err: Error | undefined, data: Buffer | undefined) => {
          if (err) reject(err);
          else resolve(Buffer.from(data || Buffer.alloc(0)));
        });
      });
    }
    return await new Promise<Buffer>((resolve) => {
      nextChunkResolve = resolve;
    });
  };

  const nextIncomingChunkWithTimeout = async (timeoutMs: number): Promise<Buffer> => {
    if (isWinUsbPath(portPath)) {
      const t0 = Date.now();
      return await new Promise<Buffer>((resolve, reject) => {
        const conn = winUsbConnections.get(portPath);
        if (!conn) return reject(new Error('WinUSB 未连接'));
        const timer = setTimeout(() => {
          clearTimeout(timer);
          reject(new Error(`nextIncomingChunk timeout (${timeoutMs}ms)`));
        }, timeoutMs);
        conn.inEp.transfer(4096, (err: Error | undefined, data: Buffer | undefined) => {
          if (Date.now() - t0 > timeoutMs + 200) {
            // ignore late callbacks
            return;
          }
          clearTimeout(timer);
          if (err) reject(err);
          else resolve(Buffer.from(data || Buffer.alloc(0)));
        });
      });
    }

    return await new Promise<Buffer>((resolve, reject) => {
      const timer = setTimeout(() => {
        nextChunkResolve = null;
        reject(new Error(`nextIncomingChunk timeout (${timeoutMs}ms)`));
      }, timeoutMs);
      nextChunkResolve = (buf: Buffer) => {
        clearTimeout(timer);
        nextChunkResolve = null;
        resolve(buf);
      };
    });
  };

  try {
    attachRx();
    startWinUsbRxPump();
    rxBuffer = Buffer.alloc(0);

    // 1) 切换到二进制桥模式：使用 CMD_CONFIG 立即切换，避免等待 pending command
    //    CMD_CONFIG: [0x80] [speed] [i2c_addr|flags]
    //    mode_flag(最高位)=0 => BRIDGE_MODE_BINARY
    sendXcfgTransferProgress({ phase: 'transfer', portPath, fileName, message: '切换 MCU 到二进制桥模式 (mode0)...' });
    await sendBuf(bytesSwitchToMode0());
    await sleep(100);
    rxBuffer = Buffer.alloc(0);

    // 2) 使用预处理结果（优先 preparedToken，无则即时编码分包计划，流式发送不依赖整文件读回）
    sendXcfgTransferProgress({ phase: 'prepare', portPath, fileName, current: 0, total: 1, percent: 0, message: '正在准备传输分包计划' });
    const prepared = (payload?.preparedToken && preparedXcfgMap.get(payload.preparedToken))
      ? preparedXcfgMap.get(payload.preparedToken)!
      : buildPreparedXcfgData(xcfgContent, fileName, backupName, safeOutputDir, { writePreparedFile: true });
    const parsed = prepared.parsed;
    const objBytes = prepared.objBytes;
    const objectsMeta = prepared.objectsMeta;
    const chunks = prepared.chunks;
    const total_objects = prepared.total_objects;
    const total_chunks = prepared.total_chunks;
    const total_bytes = prepared.total_bytes;
    sendXcfgTransferProgress({
      phase: 'prepare',
      portPath,
      fileName,
      current: 1,
      total: 1,
      percent: 100,
      message: `就绪: ${total_objects}/${CFG_MAX_OBJECTS_MCU} 对象, ${total_chunks} 包 (流式发送, MCU 边收边写 I2C)`
    });

    // 3) START 帧由 MCU 内部执行 FREEZE(0x22)，此处不再先发 FREEZE

    // 4) 发送 START（含对象 addr/size 表；MCU 校验后冻结并 ACK）
    const startWithoutCrc: number[] = [];
    startWithoutCrc.push(CFGWRITE_START_CMD);
    startWithoutCrc.push(CFG_PROTOCOL_VERSION);
    startWithoutCrc.push(...u16le(total_objects));
    startWithoutCrc.push(...u16le(total_chunks));
    startWithoutCrc.push(...u32le(total_bytes));
    for (const m of objectsMeta) {
      startWithoutCrc.push(...u16le(m.object_address));
      startWithoutCrc.push(...u16le(m.object_size));
    }
    const startFrame = buildFrameWithCRC(startWithoutCrc);
    sendXcfgTransferProgress({
      phase: 'transfer',
      portPath,
      fileName,
      message: `发送 START (${startFrame.length}B, ≤${cfgStartFrameMaxBytes(CFG_MAX_OBJECTS_MCU)}B 预留), MCU 将 FREEZE...`
    });
    await sendBuf(startFrame, 30000);

    const waitAck = async (expectedSeq: number, timeoutMs: number): Promise<AckFrame> => {
      const deadline = Date.now() + timeoutMs;
      while (Date.now() < deadline) {
      if (cancelledXcfgTransfers.has(portPath)) throw new Error('transfer cancelled');
        // try parse frames in current rxBuffer
        while (rxBuffer.length > 0) {
          const parsedFrame = tryConsumeFrame(rxBuffer);
          if (!parsedFrame) {
            // 重同步：如果首字节不是我们关心的命令，丢弃 1 字节；如果长度足够但 CRC 不通过，也丢弃 1 字节
            if (rxBuffer.length >= 6 && (rxBuffer[0] === CFG_RESP_ACK_CMD || rxBuffer[0] === CFG_RESP_NACK_CMD)) {
              rxBuffer = rxBuffer.subarray(1);
              continue;
            }
            if (rxBuffer[0] !== CFG_RESP_ACK_CMD && rxBuffer[0] !== CFG_RESP_NACK_CMD) {
              rxBuffer = rxBuffer.subarray(1);
              continue;
            }
            break;
          }
          const { frame, frameLen } = parsedFrame;
          rxBuffer = rxBuffer.subarray(frameLen);
          if ((frame as AckFrame).seq !== undefined && ((frame as AckFrame).cmd === CFG_RESP_ACK_CMD || (frame as AckFrame).cmd === CFG_RESP_NACK_CMD)) {
            const f = frame as AckFrame;
            if (f.seq === expectedSeq) return f;
          }
        }
        // no matching yet: wait next chunk
        const remaining = deadline - Date.now();
        if (remaining <= 0) break;
        const waitMs = Math.min(200, remaining);
        if (isWinUsbPath(portPath)) {
          const chunk = await nextIncomingChunkWithTimeout(waitMs).catch(() => Buffer.alloc(0));
          if (chunk.length > 0) rxBuffer = Buffer.concat([rxBuffer, chunk]);
        } else {
          await nextIncomingChunkWithTimeout(waitMs).catch(() => {});
        }
        // 如果没有新数据，下一轮会继续尝试
      }
      throw new Error(`ACK timeout seq=${expectedSeq}`);
    };

    // MCU start ack: seq=0
    const startAck = await waitAck(0, 15000).catch(() => null);
    if (!startAck || startAck.cmd !== CFG_RESP_ACK_CMD || startAck.status !== STATUS_OK) {
      const statusHint = startAck
        ? (startAck.status === 0x81 ? '触摸芯片未找到' : startAck.status === STATUS_ADDR_NACK ? '参数/CRC/对象数超限' : '')
        : '';
      const detail = startAck
        ? `cmd=0x${startAck.cmd.toString(16)} status=0x${startAck.status.toString(16)}${statusHint ? ` (${statusHint})` : ''}`
        : 'no response（请确认 MCU 已烧录支持 128 对象与 enlarged CFG_RX_BUF 的固件）';
      sendXcfgTransferProgress({ phase: 'error', portPath, fileName, message: `MCU START 失败: ${detail}` });
      return { success: false, error: `MCU start failed: ${detail}` };
    }

    // 5) 按对象发送 chunks（对象写完后才进入下一个对象；最后一包必须返回 STATUS_OBJ_DONE）
    const maxRetries = 5;
    sendXcfgTransferProgress({
      phase: 'transfer',
      portPath,
      fileName,
      current: 0,
      total: total_chunks,
      percent: 0,
      message: '开始按对象传输配置包'
    });

    let sentChunks = 0;
    let idx = 0;
    while (idx < chunks.length) {
      const objIndex = chunks[idx].obj_index;
      const objChunks: PreparedXcfgChunk[] = [];
      while (idx < chunks.length && chunks[idx].obj_index === objIndex) {
        objChunks.push(chunks[idx]);
        idx++;
      }

      let lastObjRespStatus = 0xFF;
      for (let k = 0; k < objChunks.length; k++) {
        const ch = objChunks[k];
        const isLastChunkOfObj = k === objChunks.length - 1;

        let ok = false;
        let respStatus = 0xFF;
        for (let attempt = 0; attempt < maxRetries && !ok; attempt++) {
          const chunkBytes: number[] = [];
          chunkBytes.push(CFGWRITE_CHUNK_CMD);
          chunkBytes.push(...u16le(ch.seq));
          chunkBytes.push(...u16le(ch.obj_index));
          chunkBytes.push(...u16le(ch.offset));
          chunkBytes.push(...u16le(ch.data.length));
          chunkBytes.push(...Array.from(ch.data));
          const frame = buildFrameWithCRC(chunkBytes);
          await sendBuf(frame);

          const resp = await waitAck(ch.seq, 2000).catch(() => null);
          if (resp && resp.cmd === CFG_RESP_ACK_CMD && (resp.status === STATUS_OK || resp.status === STATUS_OBJ_DONE)) {
            ok = true;
            respStatus = resp.status;
          }
        }

        if (!ok) {
          sendXcfgTransferProgress({
            phase: 'error',
            portPath,
            fileName,
            current: ch.seq,
            total: total_chunks,
            percent: Math.floor((ch.seq * 100) / Math.max(1, total_chunks)),
            message: `分包发送失败 seq=${ch.seq}`
          });
          return { success: false, error: `chunk failed (seq=${ch.seq}, obj=${objIndex + 1})` };
        }

        sentChunks++;
        lastObjRespStatus = respStatus;
        if (ch.seq === 1 || ch.seq === total_chunks || ch.seq % 10 === 0) {
          sendXcfgTransferProgress({
            phase: 'transfer',
            portPath,
            fileName,
            current: ch.seq,
            total: total_chunks,
            percent: Math.floor((ch.seq * 100) / Math.max(1, total_chunks)),
            message: `已发送 ${ch.seq}/${total_chunks}`
          });
        }

        // 对象最后一包必须返回 OBJ_DONE，否则说明该对象并未真正写完
        if (isLastChunkOfObj && respStatus !== STATUS_OBJ_DONE) {
          return {
            success: false,
            error: `对象写入未完成: obj=${objIndex + 1}/${total_objects}, lastChunkSeq=${ch.seq}, status=0x${respStatus.toString(16)}`
          };
        }
      }

      sendXcfgTransferProgress({
        phase: 'transfer',
        portPath,
        fileName,
        current: sentChunks,
        total: total_chunks,
        percent: Math.floor((sentChunks * 100) / Math.max(1, total_chunks)),
        message: `对象 ${objIndex + 1}/${total_objects} 执行完成(status=${lastObjRespStatus})`
      });
    }

    // 6) 发送 END（end_seq = total_chunks + 1）
    const endSeq = total_chunks + 1;
    const endWithoutCrc: number[] = [CFGWRITE_END_CMD, ...u16le(endSeq), ...u16le(0)];
    const endFrame = buildFrameWithCRC(endWithoutCrc);
    await sendBuf(endFrame);

    const endAck = await waitAck(endSeq, 10000).catch(() => null);
    if (!endAck || endAck.cmd !== CFG_RESP_ACK_CMD || endAck.status !== STATUS_OK) {
      return { success: false, error: `MCU end failed` };
    }

    rxBuffer = Buffer.alloc(0);

    // 7) 当前固件读回链路不稳定时，先跳过实时读回，直接备份并退出更新模式
    sendXcfgTransferProgress({ phase: 'readback', portPath, fileName, current: 0, total: 1, percent: 0, message: '已禁用完整读回校验（按对象确认）' });
    sendXcfgTransferProgress({ phase: 'backup', portPath, fileName, current: 0, total: 1, percent: 0, message: '执行备份指令并退出更新模式' });
    let backupFeedback: AckFrame | null = null;
    try {
      await sendControlCommand(BACKUPNV_COMMAND, '正在执行备份指令', '备份指令已发送');
      backupFeedback = await waitAck(0, 10000).catch(() => null);
      if (backupFeedback) {
        sendXcfgTransferProgress({
          phase: 'backup',
          portPath,
          fileName,
          message: `单片机反馈: cmd=${backupFeedback.cmd} status=${backupFeedback.status}`
        });
      }
    } catch (e: any) {
      sendXcfgTransferProgress({
        phase: 'backup',
        portPath,
        fileName,
        message: `备份指令发送/等待异常: ${e?.message || String(e)}`
      });
    }
    // BACKUPNV 触发后 NVM 写入仍可能需要更久：等待一下再 UNFREEZE，避免 I2C 卡死
    const backupAcked = !!(backupFeedback && backupFeedback.cmd === CFG_RESP_ACK_CMD && backupFeedback.status === STATUS_OK);
    if (backupAcked) await sleep(600);
    rxBuffer = Buffer.alloc(0); // 清空历史帧，仅等待本次控制命令反馈
    let unfreezeFeedback: AckFrame | null = null;
    let unfreezeSent = false;
    try {
      await sendControlCommand(UNFREEZE_COMMAND, '正在退出更新模式', '退出更新模式指令已发送');
      unfreezeSent = true;
    } catch (e: any) {
      sendXcfgTransferProgress({
        phase: 'backup',
        portPath,
        fileName,
        message: `UNFREEZE 指令发送超时/异常: ${e?.message || String(e)}`
      });
    }
    if (unfreezeSent) unfreezeFeedback = await waitAck(0, 10000).catch(() => null);
    const unfreezeAcked = !!(unfreezeFeedback && unfreezeFeedback.cmd === CFG_RESP_ACK_CMD && unfreezeFeedback.status === STATUS_OK);
    if (unfreezeFeedback) {
      sendXcfgTransferProgress({
        phase: 'backup',
        portPath,
        fileName,
        message: `单片机反馈: cmd=${unfreezeFeedback.cmd} status=${unfreezeFeedback.status}`
      });
    } else {
      sendXcfgTransferProgress({
        phase: 'backup',
        portPath,
        fileName,
        message: unfreezeSent
          ? `单片机未收到 UNFREEZE ACK: 当前缓存=${rxBuffer.length}B`
          : '跳过 UNFREEZE ACK 等待（发送失败）'
      });
    }
    sendXcfgTransferProgress({ phase: 'backup', portPath, fileName, current: 1, total: 1, percent: 100, message: '对象分包写入完成' });

    // 8) 未做芯片读回时仅回传源文件内容（不代表 NVM 已校验一致）
    const exportedXcfg = xcfgContent;
    const exportedFromMcuVerified = false;
    // 如果 UNFREEZE 未确认成功，通常 help/T100CFG 也不会稳定输出，先跳过避免干扰后续重连上传
    let t100cfgAuto: { success: boolean; message: string } | null = null;
    if (unfreezeAcked) {
      await triggerHelpOutput();
      t100cfgAuto = await probeT100Cfg();
    }
    sendXcfgTransferProgress({
      phase: 'done',
      portPath,
      fileName,
      current: 1,
      total: 1,
      percent: 100,
      message: unfreezeAcked
        ? '传输完成，已退出更新模式并切回字符串命令模式'
        : '传输完成，备份可能已写入；UNFREEZE 未收到确认（建议断电重连）'
    });

    return {
      success: true,
      backupFileName: backupName,
      backupDone: backupAcked,
      t100cfgAuto,
      exportedXcfg,
      exportedFromMcuVerified,
      exportFileName: fileName,
      preparedFilePath: prepared.preparedFilePath
    };
  } catch (error: any) {
    if (cancelledXcfgTransfers.has(portPath)) {
      sendXcfgTransferProgress({ phase: 'error', portPath, fileName, message: '已停止当前传输' });
      return { success: false, cancelled: true, error: '已停止当前传输' };
    }
    sendXcfgTransferProgress({ phase: 'error', portPath, fileName, message: error?.message || String(error) });
    return { success: false, error: error?.message || String(error) };
  } finally {
    stopWinUsbRxPump();
    cancelledXcfgTransfers.delete(portPath);
    // 恢复串口转发（给终端继续显示输出用）
    try {
      if (isWinUsbPath(portPath)) {
        startWinUsbRead(portPath);
      } else {
        const portInstance = serialPorts.get(portPath);
        if (portInstance?.port) {
          portInstance.port.removeAllListeners('data');
          portInstance.port.removeAllListeners('error');
          portInstance.port.removeAllListeners('close');
          portInstance.port.on('data', (data: Buffer) => {
            broadcastToRendererWindows('serial-data', portPath, Array.from(data));
          });
          portInstance.port.on('error', (error: Error) => {
            broadcastToRendererWindows('serial-error', portPath, error.message);
          });
          portInstance.port.on('close', () => {
            broadcastToRendererWindows('serial-close', portPath);
            serialPorts.delete(portPath);
          });
        }
      }
    } catch (_) {}
  }
});

ipcMain.handle('prepare-xcfg-binary', async (_event: Electron.IpcMainInvokeEvent, payload: {
  fileName?: string;
  xcfgContent: string;
  backupName?: string;
  outputDir?: string;
}) => {
  const fileName = (payload?.fileName || 'config1.xcfg').trim();
  const xcfgContent = payload?.xcfgContent;
  const backupName = sanitizeBackupFileName(payload?.backupName || 'test-V1');
  const outputDir = (payload?.outputDir || process.cwd()).trim() || process.cwd();
  if (!xcfgContent || !xcfgContent.trim()) return { success: false, error: 'xcfg 内容为空' };
  try {
    sendXcfgTransferProgress({ phase: 'prepare', fileName, current: 0, total: 1, percent: 0, message: '正在预处理 xcfg 为二进制+CRC16' });
    const prepared = buildPreparedXcfgData(xcfgContent, fileName, backupName, outputDir);
    preparedXcfgMap.set(prepared.token, prepared);
    sendXcfgTransferProgress({ phase: 'prepare', fileName, current: 1, total: 1, percent: 100, message: `预处理完成: ${path.basename(prepared.preparedFilePath)}` });
    return {
      success: true,
      token: prepared.token,
      preparedFilePath: prepared.preparedFilePath,
      totalObjects: prepared.total_objects,
      maxObjectsMcu: CFG_MAX_OBJECTS_MCU,
      totalChunks: prepared.total_chunks,
      totalBytes: prepared.total_bytes
    };
  } catch (error: any) {
    sendXcfgTransferProgress({ phase: 'error', fileName, message: error?.message || String(error) });
    return { success: false, error: error?.message || String(error) };
  }
});

ipcMain.handle('parse-prepared-binary', async (_event: Electron.IpcMainInvokeEvent, payload: {
  filePath?: string;
  binBase64?: string;
}) => {
  const filePath = (payload?.filePath || '').trim();
  const binBase64 = (payload?.binBase64 || '').trim();
  try {
    let bin: Buffer;
    if (filePath) {
      bin = fs.readFileSync(filePath);
    } else if (binBase64) {
      bin = Buffer.from(binBase64, 'base64');
    } else {
      return { success: false, error: '请提供 filePath 或 binBase64' };
    }

    const parsed = parsePreparedBinBuffer(bin);
    return {
      success: true,
      protocolVersion: parsed.protocolVersion,
      totalObjects: parsed.total_objects,
      totalChunks: parsed.total_chunks,
      totalBytes: parsed.total_bytes,
      objectsMeta: parsed.objectsMeta,
      chunks: parsed.chunks,
      objectBytesBase64: parsed.objBytes.map((b) => Buffer.from(b).toString('base64'))
    };
  } catch (error: any) {
    return { success: false, error: error?.message || String(error) };
  }
});

ipcMain.handle('export-prepared-binary-txt', async (_event: Electron.IpcMainInvokeEvent, payload: {
  filePath?: string;
  binBase64?: string;
  outputPath?: string;
}) => {
  const filePath = (payload?.filePath || '').trim();
  const binBase64 = (payload?.binBase64 || '').trim();
  let outputPath = (payload?.outputPath || '').trim();

  try {
    let bin: Buffer;
    if (filePath) {
      bin = fs.readFileSync(filePath);
    } else if (binBase64) {
      bin = Buffer.from(binBase64, 'base64');
    } else {
      return { success: false, error: '请提供 filePath 或 binBase64' };
    }

    const parsed = parsePreparedBinBuffer(bin);
    const text = formatPreparedBinAsText(parsed);

    if (!outputPath) {
      const base = filePath ? path.parse(filePath).name : `prepared-${Date.now()}`;
      outputPath = path.join(filePath ? path.dirname(filePath) : process.cwd(), `${base}.txt`);
    }
    fs.writeFileSync(outputPath, text, 'utf-8');

    return {
      success: true,
      outputPath,
      totalObjects: parsed.total_objects,
      totalChunks: parsed.total_chunks,
      totalBytes: parsed.total_bytes
    };
  } catch (error: any) {
    return { success: false, error: error?.message || String(error) };
  }
});

ipcMain.handle('write-bin-and-backup-from-mcu', async (_event: Electron.IpcMainInvokeEvent, payload: {
  portPath: string;
  fileName?: string;
  binBase64: string;
  backupName?: string;
}) => {
  const portPath = (payload?.portPath || '').trim();
  const fileName = (payload?.fileName || 'config1-prepared.bin').trim();
  const backupName = sanitizeBackupFileName(payload?.backupName || 'test-V1');
  const binBase64 = payload?.binBase64;
  if (!portPath) return { success: false, error: '缺少 portPath' };
  if (!binBase64 || !String(binBase64).trim()) return { success: false, error: 'bin 内容为空' };
  cancelledXcfgTransfers.delete(portPath);

  type AckFrame = { cmd: number; seq: number; status: number };

  const parsePreparedFrames = (buf: Buffer): { startFrame: Uint8Array; chunks: Array<{ seq: number; frame: Uint8Array }>; endFrame: Uint8Array } => {
    if (!buf || buf.length < 16) throw new Error('bin 文件过短');
    let pos = 0;
    if (buf[pos] !== CFGWRITE_START_CMD) throw new Error('bin 起始帧不是 D0');
    const totalObjects = buf[pos + 2] | (buf[pos + 3] << 8);
    assertObjectCountWithinMcuLimit(totalObjects);
    const totalChunks = buf[pos + 4] | (buf[pos + 5] << 8);
    const startLen = 12 + totalObjects * 4;
    if (buf.length < startLen) throw new Error('bin 起始帧长度错误');
    const startFrame = buf.subarray(pos, pos + startLen);
    const startCrc = startFrame[startLen - 2] | (startFrame[startLen - 1] << 8);
    if (crc16ModbusIBM(startFrame.subarray(0, startLen - 2)) !== startCrc) throw new Error('bin 起始帧 CRC 错误');
    pos += startLen;

    const chunks: Array<{ seq: number; frame: Uint8Array }> = [];
    for (let i = 0; i < totalChunks; i++) {
      if (pos + 11 > buf.length || buf[pos] !== CFGWRITE_CHUNK_CMD) throw new Error('bin 分包帧格式错误');
      const seq = buf[pos + 1] | (buf[pos + 2] << 8);
      const len = buf[pos + 7] | (buf[pos + 8] << 8);
      if (len > CFG_XFER_MAX_CHUNK) {
        throw new Error(`bin 单包长度 ${len} 超过固件上限 ${CFG_XFER_MAX_CHUNK}，请用当前版本重新从 XCFG 预处理`);
      }
      const frameLen = 11 + len;
      if (pos + frameLen > buf.length) throw new Error('bin 分包帧长度错误');
      const frame = buf.subarray(pos, pos + frameLen);
      const crc = frame[frameLen - 2] | (frame[frameLen - 1] << 8);
      if (crc16ModbusIBM(frame.subarray(0, frameLen - 2)) !== crc) throw new Error(`bin 分包 CRC 错误 seq=${seq}`);
      chunks.push({ seq, frame });
      pos += frameLen;
    }
    if (pos + 7 > buf.length || buf[pos] !== CFGWRITE_END_CMD) throw new Error('bin 结束帧格式错误');
    const endFrame = buf.subarray(pos, pos + 7);
    const endCrc = endFrame[5] | (endFrame[6] << 8);
    if (crc16ModbusIBM(endFrame.subarray(0, 5)) !== endCrc) throw new Error('bin 结束帧 CRC 错误');
    return { startFrame: Uint8Array.from(startFrame), chunks, endFrame: Uint8Array.from(endFrame) };
  };

  let rxBuffer = Buffer.alloc(0);
  let nextChunkResolve: ((buf: Buffer) => void) | null = null;
  const sendBuf = async (data: Uint8Array) => {
    if (cancelledXcfgTransfers.has(portPath)) throw new Error('transfer cancelled');
    if (isWinUsbPath(portPath)) return await writeWinUsb(portPath, Buffer.from(data));
    const portInstance = serialPorts.get(portPath);
    if (!portInstance || !portInstance.isOpen) throw new Error('串口未连接');
    await new Promise<void>((resolve, reject) => {
      portInstance.port.write(Buffer.from(data), (err: Error | null | undefined) => err ? reject(err) : resolve());
    });
  };
  const restoreStringMode = async () => {
    await sendBuf(bytesSwitchToMode1());
    await new Promise<void>(r => setTimeout(r, 30));
  };
  const triggerHelpOutput = async () => {
    try {
      await sendBuf(Uint8Array.from(Buffer.from('help\n', 'ascii')));
      await new Promise<void>(r => setTimeout(r, 40));
    } catch (_) {}
  };
  const probeT100Cfg = async (): Promise<{ success: boolean; message: string }> => {
    try {
      rxBuffer = Buffer.alloc(0);
      await sendBuf(Uint8Array.from(Buffer.from('T100CFG\n', 'ascii')));
      const deadline = Date.now() + 2000;
      while (Date.now() < deadline) {
        const chunk = await nextIncomingChunkWithTimeout(200).catch(() => Buffer.alloc(0));
        if (isWinUsbPath(portPath) && chunk.length > 0) rxBuffer = Buffer.concat([rxBuffer, chunk]);
        if (rxBuffer.length > 0) {
          const text = rxBuffer.toString('utf8');
          if (/UNKNOWN\[9\]|UNKNOWN\[20\]|T100/i.test(text)) return { success: true, message: 'T100CFG 自动执行成功' };
          if (/ERR|ERROR/i.test(text)) return { success: false, message: 'T100CFG 自动执行失败' };
        }
      }
      return { success: false, message: 'T100CFG 自动执行超时（无响应）' };
    } catch (_) {
      return { success: false, message: 'T100CFG 自动执行异常' };
    }
  };
  const nextIncomingChunkWithTimeout = async (timeoutMs: number): Promise<Buffer> => {
    if (isWinUsbPath(portPath)) {
      return await new Promise<Buffer>((resolve, reject) => {
        const conn = winUsbConnections.get(portPath);
        if (!conn) return reject(new Error('WinUSB 未连接'));
        const timer = setTimeout(() => reject(new Error(`nextIncomingChunk timeout (${timeoutMs}ms)`)), timeoutMs);
        conn.inEp.transfer(4096, (err: Error | undefined, data: Buffer | undefined) => {
          clearTimeout(timer);
          if (err) reject(err);
          else resolve(Buffer.from(data || Buffer.alloc(0)));
        });
      });
    }
    return await new Promise<Buffer>((resolve, reject) => {
      const timer = setTimeout(() => {
        nextChunkResolve = null;
        reject(new Error(`nextIncomingChunk timeout (${timeoutMs}ms)`));
      }, timeoutMs);
      nextChunkResolve = (buf: Buffer) => {
        clearTimeout(timer);
        nextChunkResolve = null;
        resolve(buf);
      };
    });
  };
  const attachRx = () => {
    if (isWinUsbPath(portPath)) return;
    const portInstance = serialPorts.get(portPath);
    if (!portInstance || !portInstance.isOpen) throw new Error('串口未连接');
    portInstance.port.on('data', (d: Buffer) => {
      rxBuffer = Buffer.concat([rxBuffer, d]);
      if (nextChunkResolve) {
        nextChunkResolve(d);
        nextChunkResolve = null;
      }
    });
  };
  const tryConsumeAck = (rx: Buffer): { frame: AckFrame; frameLen: number } | null => {
    if (rx.length < 6) return null;
    const cmd = rx[0];
    if (cmd !== CFG_RESP_ACK_CMD && cmd !== CFG_RESP_NACK_CMD) return null;
    const seq = rx[1] | (rx[2] << 8);
    const status = rx[3];
    const crcRx = rx[4] | (rx[5] << 8);
    const crcCalc = crc16ModbusIBM(rx.subarray(0, 4));
    if (crcRx !== crcCalc) return null;
    return { frame: { cmd, seq, status }, frameLen: 6 };
  };
  const waitAck = async (expectedSeq: number, timeoutMs: number): Promise<AckFrame> => {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
      if (cancelledXcfgTransfers.has(portPath)) throw new Error('transfer cancelled');
      while (rxBuffer.length > 0) {
        const parsed = tryConsumeAck(rxBuffer);
        if (!parsed) {
          rxBuffer = rxBuffer.subarray(1);
          continue;
        }
        rxBuffer = rxBuffer.subarray(parsed.frameLen);
        if (parsed.frame.seq === expectedSeq) return parsed.frame;
      }
      const chunk = await nextIncomingChunkWithTimeout(200).catch(() => Buffer.alloc(0));
      if (isWinUsbPath(portPath) && chunk.length > 0) rxBuffer = Buffer.concat([rxBuffer, chunk]);
    }
    throw new Error(`ACK timeout seq=${expectedSeq}`);
  };

  try {
    if (isWinUsbPath(portPath)) stopWinUsbRead(portPath);
    else {
      const p = serialPorts.get(portPath);
      p?.port?.removeAllListeners?.('data');
      p?.port?.removeAllListeners?.('error');
      p?.port?.removeAllListeners?.('close');
    }
    attachRx();
    rxBuffer = Buffer.alloc(0);
    await sendBuf(bytesSwitchToMode0());
    const parsed = parsePreparedFrames(Buffer.from(binBase64, 'base64'));
    sendXcfgTransferProgress({ phase: 'transfer', portPath, fileName, message: '正在启动配置写入（START 内含 freeze）' });
    await sendBuf(parsed.startFrame);
    const startAck = await waitAck(0, 5000).catch(() => null);
    if (!startAck || startAck.cmd !== CFG_RESP_ACK_CMD || startAck.status !== STATUS_OK) {
      return { success: false, error: 'MCU start failed' };
    }
    sendXcfgTransferProgress({ phase: 'transfer', portPath, fileName, current: 0, total: parsed.chunks.length, percent: 0, message: '开始上传 BIN 分包（按对象反馈）' });
    let sentChunks = 0;
    const totalObjects = parsed.startFrame[2] | (parsed.startFrame[3] << 8);
    const totalChunks = parsed.startFrame[4] | (parsed.startFrame[5] << 8);

    let idx = 0;
    while (idx < parsed.chunks.length) {
      const first = parsed.chunks[idx];
      const objIndexInFrame = first.frame[3] | (first.frame[4] << 8);

      const objChunks: Array<{ seq: number; frame: Uint8Array }> = [];
      while (idx < parsed.chunks.length) {
        const cur = parsed.chunks[idx];
        const curObjIndex = cur.frame[3] | (cur.frame[4] << 8);
        if (curObjIndex !== objIndexInFrame) break;
        objChunks.push(cur);
        idx++;
      }

      let lastObjRespStatus = 0xFF;
      for (let k = 0; k < objChunks.length; k++) {
        const ch = objChunks[k];
        const isLastChunkOfObj = k === objChunks.length - 1;

        let ok = false;
        let respStatus = 0xFF;
        for (let attempt = 0; attempt < 5 && !ok; attempt++) {
          await sendBuf(ch.frame);
          const ack = await waitAck(ch.seq, 3000).catch(() => null);
          if (ack && ack.cmd === CFG_RESP_ACK_CMD && (ack.status === STATUS_OK || ack.status === STATUS_OBJ_DONE)) {
            ok = true;
            respStatus = ack.status;
          }
        }
        if (!ok) return { success: false, error: `BIN 分包上传失败 seq=${ch.seq}` };

        sentChunks++;
        lastObjRespStatus = respStatus;
        if (ch.seq === 1 || ch.seq === totalChunks || ch.seq % 10 === 0) {
          sendXcfgTransferProgress({
            phase: 'transfer',
            portPath,
            fileName,
            current: ch.seq,
            total: totalChunks,
            percent: Math.floor((ch.seq * 100) / Math.max(1, totalChunks)),
            message: `已上传 ${ch.seq}/${totalChunks}`
          });
        }

        // 对象最后一包必须返回 OBJ_DONE
        if (isLastChunkOfObj && respStatus !== STATUS_OBJ_DONE) {
          return {
            success: false,
            error: `对象写入未完成: obj=${objIndexInFrame + 1}/${totalObjects}, lastChunkSeq=${ch.seq}, status=0x${respStatus.toString(16)}`
          };
        }
      }

      sendXcfgTransferProgress({
        phase: 'transfer',
        portPath,
        fileName,
        current: sentChunks,
        total: totalChunks,
        percent: Math.floor((sentChunks * 100) / Math.max(1, totalChunks)),
        message: `对象 ${objIndexInFrame + 1}/${totalObjects} 执行完成(status=${lastObjRespStatus})`
      });
    }
    await sendBuf(parsed.endFrame);
    const endSeq = parsed.chunks.length + 1;
    const endAck = await waitAck(endSeq, 8000).catch(() => null);
    if (!endAck || endAck.cmd !== CFG_RESP_ACK_CMD || endAck.status !== STATUS_OK) return { success: false, error: 'MCU end failed' };

    rxBuffer = Buffer.alloc(0);

    sendXcfgTransferProgress({ phase: 'backup', portPath, fileName, current: 0, total: 1, percent: 0, message: '执行备份指令并退出更新模式' });
    await sendBuf(buildFrameWithCRC([BACKUPNV_COMMAND]));
    const backupFeedback = await waitAck(0, 10000).catch(() => null);
    if (backupFeedback) {
      sendXcfgTransferProgress({
        phase: 'backup',
        portPath,
        fileName,
        message: `单片机反馈: cmd=${backupFeedback.cmd} status=${backupFeedback.status}`
      });
    }
    if (backupFeedback) await new Promise<void>(r => setTimeout(r, 600));
    rxBuffer = Buffer.alloc(0); // 清空历史帧，仅等待本次控制命令反馈
    await sendBuf(buildFrameWithCRC([UNFREEZE_COMMAND]));
    const unfreezeFeedback = await waitAck(0, 10000).catch(() => null);
    if (unfreezeFeedback) {
      sendXcfgTransferProgress({
        phase: 'backup',
        portPath,
        fileName,
        message: `单片机反馈: cmd=${unfreezeFeedback.cmd} status=${unfreezeFeedback.status}`
      });
    }
    await new Promise<void>(r => setTimeout(r, 80));
    await triggerHelpOutput();
    const t100cfgAuto = await probeT100Cfg();
    sendXcfgTransferProgress({ phase: 'done', portPath, fileName, current: 1, total: 1, percent: 100, message: 'BIN 按对象上传完成，已切回字符串命令模式' });
    const backupDone = !!(backupFeedback && backupFeedback.cmd === CFG_RESP_ACK_CMD && backupFeedback.status === STATUS_OK);
    return { success: true, backupFileName: backupName, backupDone, uploadedBinFileName: fileName, t100cfgAuto };
  } catch (error: any) {
    if (cancelledXcfgTransfers.has(portPath)) {
      sendXcfgTransferProgress({ phase: 'error', portPath, fileName, message: '已停止当前 BIN 传输' });
      return { success: false, cancelled: true, error: '已停止当前 BIN 传输' };
    }
    sendXcfgTransferProgress({ phase: 'error', portPath, fileName, message: error?.message || String(error) });
    return { success: false, error: error?.message || String(error) };
  } finally {
    cancelledXcfgTransfers.delete(portPath);
    try {
      if (isWinUsbPath(portPath)) startWinUsbRead(portPath);
      else {
        const portInstance = serialPorts.get(portPath);
        if (portInstance?.port) {
          portInstance.port.removeAllListeners('data');
          portInstance.port.removeAllListeners('error');
          portInstance.port.removeAllListeners('close');
          portInstance.port.on('data', (data: Buffer) => {
            broadcastToRendererWindows('serial-data', portPath, Array.from(data));
          });
          portInstance.port.on('error', (error: Error) => {
            broadcastToRendererWindows('serial-error', portPath, error.message);
          });
          portInstance.port.on('close', () => {
            broadcastToRendererWindows('serial-close', portPath);
            serialPorts.delete(portPath);
          });
        }
      }
    } catch (_) {}
  }
});

ipcMain.handle('flash-enc-from-mcu', async (_event: Electron.IpcMainInvokeEvent, payload: {
  portPath: string;
  encFilePath: string;
  fileName?: string;
  bootloaderAddr?: number;
  skipEnterBootloader?: boolean;
}) => {
  const portPath = (payload?.portPath || '').trim();
  const encFilePath = (payload?.encFilePath || '').trim();
  const fileName = (payload?.fileName || path.basename(encFilePath) || 'firmware.enc').trim();
  const bootloaderAddr = payload?.bootloaderAddr ?? ENC_DEFAULT_BL_ADDR;
  const skipEnter = payload?.skipEnterBootloader === true;

  if (!portPath) return { success: false, error: '缺少 portPath' };
  if (!encFilePath || !fs.existsSync(encFilePath)) return { success: false, error: 'enc 文件不存在' };
  cancelledXcfgTransfers.delete(portPath);

  type EncAckFrame = { cmd: number; seq: number; status: number };

  let rxBuffer = Buffer.alloc(0);
  let nextChunkResolve: ((buf: Buffer) => void) | null = null;

  const sendBuf = async (data: Uint8Array, timeoutMs = 30000) => {
    if (cancelledXcfgTransfers.has(portPath)) throw new Error('transfer cancelled');
    await Promise.race([
      (async () => {
        if (isWinUsbPath(portPath)) {
          await writeWinUsb(portPath, Buffer.from(data));
          return;
        }
        const portInstance = serialPorts.get(portPath);
        if (!portInstance || !portInstance.isOpen) throw new Error('串口未连接');
        await new Promise<void>((resolve, reject) => {
          portInstance.port.write(Buffer.from(data), (err: Error | null | undefined) => err ? reject(err) : resolve());
        });
      })(),
      new Promise<never>((_, reject) => setTimeout(() => reject(new Error('USB/串口写入超时')), timeoutMs))
    ]);
  };

  const nextIncomingChunkWithTimeout = async (timeoutMs: number): Promise<Buffer> => {
    if (isWinUsbPath(portPath)) {
      return await new Promise<Buffer>((resolve, reject) => {
        const conn = winUsbConnections.get(portPath);
        if (!conn) return reject(new Error('WinUSB 未连接'));
        const timer = setTimeout(() => reject(new Error(`nextIncomingChunk timeout (${timeoutMs}ms)`)), timeoutMs);
        conn.inEp.transfer(4096, (err: Error | undefined, data: Buffer | undefined) => {
          clearTimeout(timer);
          if (err) reject(err);
          else resolve(Buffer.from(data || Buffer.alloc(0)));
        });
      });
    }
    return await new Promise<Buffer>((resolve, reject) => {
      const timer = setTimeout(() => {
        nextChunkResolve = null;
        reject(new Error(`nextIncomingChunk timeout (${timeoutMs}ms)`));
      }, timeoutMs);
      nextChunkResolve = (buf: Buffer) => {
        clearTimeout(timer);
        nextChunkResolve = null;
        resolve(buf);
      };
    });
  };

  const attachRx = () => {
    if (isWinUsbPath(portPath)) return;
    const portInstance = serialPorts.get(portPath);
    if (!portInstance || !portInstance.isOpen) throw new Error('串口未连接');
    portInstance.port.on('data', (d: Buffer) => {
      rxBuffer = Buffer.concat([rxBuffer, d]);
      if (nextChunkResolve) {
        nextChunkResolve(d);
        nextChunkResolve = null;
      }
    });
  };

  let winUsbRxPumpActive = false;
  const stopWinUsbRxPump = () => { winUsbRxPumpActive = false; };
  const startWinUsbRxPump = () => {
    if (!isWinUsbPath(portPath)) return;
    winUsbRxPumpActive = true;
    const pump = () => {
      if (!winUsbRxPumpActive || cancelledXcfgTransfers.has(portPath)) return;
      const conn = winUsbConnections.get(portPath);
      if (!conn) return;
      conn.inEp.transfer(4096, (err: Error | undefined, data: Buffer | undefined) => {
        if (!winUsbRxPumpActive) return;
        if (!err && data && data.length > 0) {
          rxBuffer = Buffer.concat([rxBuffer, data]);
          if (nextChunkResolve) {
            const resolve = nextChunkResolve;
            nextChunkResolve = null;
            resolve(data);
          }
        }
        if (winUsbRxPumpActive) pump();
      });
    };
    pump();
  };

  const tryConsumeEncAck = (rx: Buffer): { frame: EncAckFrame; frameLen: number } | null => {
    if (rx.length < 6) return null;
    const cmd = rx[0];
    if (cmd !== ENC_RESP_ACK_CMD && cmd !== ENC_RESP_NACK_CMD) return null;
    const seq = rx[1] | (rx[2] << 8);
    const status = rx[3];
    const crcRx = rx[4] | (rx[5] << 8);
    const crcCalc = crc16ModbusIBM(rx.subarray(0, 4));
    if (crcRx !== crcCalc) return null;
    return { frame: { cmd, seq, status }, frameLen: 6 };
  };

  const waitEncAck = async (expectedSeq: number, timeoutMs: number): Promise<EncAckFrame> => {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
      if (cancelledXcfgTransfers.has(portPath)) throw new Error('transfer cancelled');
      while (rxBuffer.length > 0) {
        const parsed = tryConsumeEncAck(rxBuffer);
        if (!parsed) {
          rxBuffer = rxBuffer.subarray(1);
          continue;
        }
        rxBuffer = rxBuffer.subarray(parsed.frameLen);
        if (parsed.frame.seq === expectedSeq) return parsed.frame;
      }
      const chunk = await nextIncomingChunkWithTimeout(300).catch(() => Buffer.alloc(0));
      if (isWinUsbPath(portPath) && chunk.length > 0) rxBuffer = Buffer.concat([rxBuffer, chunk]);
    }
    throw new Error(`ENC ACK timeout seq=${expectedSeq}`);
  };

  try {
    if (isWinUsbPath(portPath)) stopWinUsbRead(portPath);
    else {
      const p = serialPorts.get(portPath);
      p?.port?.removeAllListeners?.('data');
      p?.port?.removeAllListeners?.('error');
      p?.port?.removeAllListeners?.('close');
    }
    attachRx();
    if (isWinUsbPath(portPath)) startWinUsbRxPump();
    rxBuffer = Buffer.alloc(0);

    sendXcfgTransferProgress({ phase: 'prepare', portPath, fileName, message: '扫描 enc 帧数（流式，不占大内存）...' });
    const scan = await scanEncFile(encFilePath);
    if (scan.totalFrames === 0) return { success: false, error: 'enc 中未找到有效帧' };

    sendXcfgTransferProgress({ phase: 'transfer', portPath, fileName, message: '切换 MCU 到二进制桥模式 (mode0)...' });
    await sendBuf(bytesSwitchToMode0());
    await new Promise<void>(r => setTimeout(r, 80));

    const flags = skipEnter ? ENC_FLAG_SKIP_ENTER_BOOTLOADER : 0;
    const startFrame = buildFrameWithCRC([
      ENC_START_CMD,
      ENC_PROTOCOL_VERSION,
      bootloaderAddr & 0xFF,
      flags,
      ...u16le(scan.totalFrames)
    ]);
    sendXcfgTransferProgress({
      phase: 'transfer',
      portPath,
      fileName,
      message: `进入 Bootloader 并解锁 (BL=${bootloaderAddr ? `0x${bootloaderAddr.toString(16)}` : 'auto'}, ${scan.totalFrames} 帧)`
    });
    await sendBuf(startFrame, 60000);
    const startAck = await waitEncAck(0, 30000).catch(() => null);
    if (!startAck || startAck.cmd !== ENC_RESP_ACK_CMD || startAck.status !== STATUS_OK) {
      const st = startAck ? `status=0x${startAck.status.toString(16)}` : '无 ACK';
      return { success: false, phase: 'start', error: `ENC START 失败: ${st}` };
    }

    let seq = 1;
    let sent = 0;
    for await (const frame of iterateEncFramesFromFile(encFilePath)) {
      if (frame.length > ENC_MAX_FRAME_BYTES) {
        return { success: false, phase: 'transfer', error: `enc 帧过大: ${frame.length}B` };
      }
      const encFrame = buildFrameWithCRC([
        ENC_FRAME_CMD,
        ...u16le(seq),
        ...u16le(frame.length),
        ...Array.from(frame)
      ]);
      await sendBuf(encFrame, 60000);
      const ack = await waitEncAck(seq, 60000).catch(() => null);
      if (!ack || ack.cmd !== ENC_RESP_ACK_CMD || ack.status !== STATUS_OK) {
        const st = ack ? `status=0x${ack.status.toString(16)}` : '无 ACK';
        return { success: false, phase: 'transfer', error: `ENC 帧 ${seq}/${scan.totalFrames} 失败: ${st}` };
      }
      sent += 1;
      seq += 1;
      const percent = Math.min(100, Math.round((sent / scan.totalFrames) * 100));
      sendXcfgTransferProgress({
        phase: 'transfer',
        portPath,
        fileName,
        current: sent,
        total: scan.totalFrames,
        percent,
        message: `Bootloader 写帧 ${sent}/${scan.totalFrames} (${frame.length}B)`
      });
    }

    const endSeq = seq;
    const endFrame = buildFrameWithCRC([ENC_END_CMD, ...u16le(endSeq), ...u16le(0)]);
    sendXcfgTransferProgress({ phase: 'transfer', portPath, fileName, message: '发送 ENC END，等待芯片复位...' });
    await sendBuf(endFrame, 30000);
    const endAck = await waitEncAck(endSeq, 30000).catch(() => null);
    if (!endAck || endAck.cmd !== ENC_RESP_ACK_CMD || endAck.status !== STATUS_OK) {
      const st = endAck ? `status=0x${endAck.status.toString(16)}` : '无 ACK';
      return { success: false, phase: 'end', error: `ENC END 失败: ${st}` };
    }

    sendXcfgTransferProgress({
      phase: 'done',
      portPath,
      fileName,
      current: sent,
      total: scan.totalFrames,
      percent: 100,
      message: `ENC 烧录完成: ${sent} 帧, ${scan.totalBinaryBytes}B 二进制流`
    });
    return {
      success: true,
      totalFrames: sent,
      totalBinaryBytes: scan.totalBinaryBytes,
      encFilePath
    };
  } catch (error: any) {
    if (cancelledXcfgTransfers.has(portPath)) {
      sendXcfgTransferProgress({ phase: 'error', portPath, fileName, message: '已停止 ENC 传输' });
      return { success: false, error: 'transfer cancelled' };
    }
    sendXcfgTransferProgress({ phase: 'error', portPath, fileName, message: error?.message || String(error) });
    return { success: false, error: error?.message || String(error) };
  } finally {
    stopWinUsbRxPump();
    cancelledXcfgTransfers.delete(portPath);
    try {
      if (isWinUsbPath(portPath)) startWinUsbRead(portPath);
      else {
        const portInstance = serialPorts.get(portPath);
        if (portInstance && portInstance.isOpen) {
          portInstance.port.removeAllListeners('data');
          portInstance.port.removeAllListeners('error');
          portInstance.port.removeAllListeners('close');
          portInstance.port.on('data', (data: Buffer) => {
            broadcastToRendererWindows('serial-data', portPath, Array.from(data));
          });
          portInstance.port.on('error', (error: Error) => {
            broadcastToRendererWindows('serial-error', portPath, error.message);
          });
          portInstance.port.on('close', () => {
            broadcastToRendererWindows('serial-close', portPath);
            serialPorts.delete(portPath);
          });
        }
      }
    } catch (_) {}
  }
});

ipcMain.handle('cancel-xcfg-transfer', async (_event: Electron.IpcMainInvokeEvent, payload: { portPath?: string }) => {
  const portPath = (payload?.portPath || '').trim();
  if (!portPath) return { success: false, error: '缺少 portPath' };
  cancelledXcfgTransfers.add(portPath);
  return { success: true };
});

registerXcfgViewerIpc({
  getMxtAppPath,
  runMxtApp: runMxtAppCoordinated,
  getDefaultMxtDevice: getDefaultMxtDeviceFromSerialApp
});

ipcMain.handle('open-xcfg-viewer-window', async (_event: Electron.IpcMainInvokeEvent, payload?: {
  page?: string;
  xcfgContent?: string;
  fileName?: string;
}) => {
  try {
    return await openXcfgViewerWindow(payload);
  } catch (error: any) {
    return { success: false, error: error?.message || String(error) };
  }
});

app.whenReady().then(bootWithSplash);

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});

app.on('activate', () => {
  if (mainWindow === null) {
    createWindow();
  }
});
