/**
 * 与 mxt-app serial_device.c 等价的 I2C 桥协议（mode0 二进制层）。
 * 串口场景请优先走 runMxtAppShared → mxt-app → 串口代理，勿在业务层直接调用本模块。
 */
import type { SerialPort } from 'serialport';
import { getMxtObjectName, isVolatileMxtObject, isRuntimeDataContainerObject } from './mxt_object_names';

const REPORT_ID = 0x01;
const IIC_DATA_1 = 0x51;
const CMD_FIND_IIC_ADDRESS = 0xe0;
const COMMS_STATUS_OK = 0x00;
const COMMS_STATUS_WRITE_OK = 0x04;
const SERIAL_MAX_PACKET_SIZE = 64;
const TRANSFER_TIMEOUT_MS = 5000;
const GEN_COMMANDPROCESSOR_T6 = 6;
const RESTORENV_COMMAND = 0x33;
const BACKUPNV_COMMAND = 0x55;
const MXT_T6_BACKUPNV_OFFSET = 0x01;

export interface SerialPortAccessor {
  write(buf: Buffer): Promise<void>;
  readSome(maxSize: number, timeoutMs?: number): Promise<Buffer>;
  drainRx(timeoutMs?: number): Promise<void>;
}

interface MxtObjectMeta {
  type: number;
  startPos: number;
  size: number;
  instances: number;
}

interface MxtIdHeader {
  family: number;
  variant: number;
  version: number;
  build: number;
  matrixX: number;
  matrixY: number;
  numObjects: number;
}

interface MxtConfigBlock {
  type: number;
  instance: number;
  address: number;
  size: number;
  data: Buffer;
}

export interface DeviceConfigSnapshot {
  family: number;
  variant: number;
  version: number;
  build: number;
  matrixX: number;
  matrixY: number;
  numObjects: number;
  infoCrc: number;
  configCrc?: number;
  objects: MxtConfigBlock[];
}

function instancesFromRaw(sizeMinusOne: number, instancesMinusOne: number) {
  return { size: sizeMinusOne + 1, instances: instancesMinusOne + 1 };
}

function startPosition(obj: MxtObjectMeta, instance: number) {
  return obj.startPos + obj.size * instance;
}

function parseObjects(raw: Buffer, offset: number, count: number): MxtObjectMeta[] {
  const objects: MxtObjectMeta[] = [];
  for (let i = 0; i < count; i++) {
    const base = offset + i * 6;
    const { size, instances } = instancesFromRaw(raw[base + 3], raw[base + 4]);
    objects.push({
      type: raw[base],
      startPos: raw[base + 1] + raw[base + 2] * 256,
      size,
      instances
    });
  }
  return objects;
}

function parseInfoCrc(raw: Buffer, crcAreaSize: number) {
  const crcLo = raw[crcAreaSize] | (raw[crcAreaSize + 1] << 8);
  const crcHi = raw[crcAreaSize + 2];
  return ((crcHi << 16) | crcLo) & 0xffffff;
}

function getObjectAddress(objects: MxtObjectMeta[], objectType: number, instance = 0): number | null {
  for (const obj of objects) {
    if (obj.type !== objectType) continue;
    if (instance >= obj.instances) return null;
    return startPosition(obj, instance);
  }
  return null;
}

function formatTimestamp() {
  const d = new Date();
  const pad = (n: number) => String(n).padStart(2, '0');
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
}

function delay(ms: number) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function transfer(accessor: SerialPortAccessor, cmd: Buffer, responseSize = SERIAL_MAX_PACKET_SIZE) {
  await accessor.write(cmd);
  return accessor.readSome(responseSize, TRANSFER_TIMEOUT_MS);
}

async function switchMode0(accessor: SerialPortAccessor) {
  await accessor.write(Buffer.from('mode0\r\n', 'ascii'));
  await delay(150);
  await accessor.drainRx(300);
}

async function switchMode1(accessor: SerialPortAccessor) {
  await accessor.write(Buffer.from('mode1\r\n', 'ascii'));
  await delay(100);
  await accessor.drainRx(200);
}

async function findI2cAddress(accessor: SerialPortAccessor) {
  const resp = await transfer(accessor, Buffer.from([CMD_FIND_IIC_ADDRESS]));
  if (resp[1] === 0x81) {
    throw new Error('桥接芯片未找到 I2C 设备');
  }
}

async function readRegister(accessor: SerialPortAccessor, startRegister: number, count: number) {
  const maxCount = SERIAL_MAX_PACKET_SIZE - 6;
  const actual = Math.min(count, maxCount);
  const pkt = Buffer.alloc(6);
  pkt[0] = REPORT_ID;
  pkt[1] = IIC_DATA_1;
  pkt[2] = 2;
  pkt[3] = actual;
  pkt[4] = startRegister & 0xff;
  pkt[5] = (startRegister >> 8) & 0xff;
  const resp = await transfer(accessor, pkt);
  if (resp.length < 3 + actual) {
    throw new Error(`读寄存器应答过短 ${resp.length} 字节，期望 ${3 + actual} @0x${startRegister.toString(16)}`);
  }
  if (resp[1] !== COMMS_STATUS_OK) {
    throw new Error(`读寄存器失败 0x${resp[1].toString(16)} @0x${startRegister.toString(16)}`);
  }
  return resp.subarray(3, 3 + actual);
}

async function readRegisterAll(accessor: SerialPortAccessor, startRegister: number, count: number) {
  const chunks: Buffer[] = [];
  let offset = 0;
  while (offset < count) {
    const chunk = await readRegister(accessor, startRegister + offset, count - offset);
    chunks.push(chunk);
    offset += chunk.length;
    if (chunk.length === 0) break;
  }
  return Buffer.concat(chunks);
}

async function writeRegister(accessor: SerialPortAccessor, startRegister: number, data: Buffer) {
  let offset = 0;
  while (offset < data.length) {
    const maxCount = SERIAL_MAX_PACKET_SIZE - 6;
    const chunk = data.subarray(offset, offset + maxCount);
    const pkt = Buffer.alloc(6 + chunk.length);
    pkt[0] = REPORT_ID;
    pkt[1] = IIC_DATA_1;
    pkt[2] = 2 + chunk.length;
    pkt[3] = 0;
    pkt[4] = (startRegister + offset) & 0xff;
    pkt[5] = ((startRegister + offset) >> 8) & 0xff;
    chunk.copy(pkt, 6);
    const resp = await transfer(accessor, pkt);
    if (resp.length < 2) {
      throw new Error(`写寄存器应答过短 ${resp.length} 字节 @0x${(startRegister + offset).toString(16)}`);
    }
    if (resp[1] !== COMMS_STATUS_WRITE_OK) {
      throw new Error(`写寄存器失败 0x${resp[1].toString(16)} @0x${(startRegister + offset).toString(16)}`);
    }
    offset += chunk.length;
  }
}

async function restoreNv(accessor: SerialPortAccessor, objects: MxtObjectMeta[]) {
  const t6Addr = getObjectAddress(objects, GEN_COMMANDPROCESSOR_T6, 0);
  if (t6Addr == null) return;
  await writeRegister(accessor, t6Addr + MXT_T6_BACKUPNV_OFFSET, Buffer.from([RESTORENV_COMMAND]));
  await delay(100);
}

function buildXcfgFormat3(id: MxtIdHeader, infoCrc: number, configs: MxtConfigBlock[]) {
  const lines: string[] = [];
  lines.push('[COMMENTS]');
  lines.push(`Date and time: ${formatTimestamp()}`);
  lines.push('');
  lines.push('[VERSION_INFO_HEADER]');
  lines.push(`FAMILY_ID=${id.family}`);
  lines.push(`VARIANT=${id.variant}`);
  lines.push(`VERSION=${id.version}`);
  lines.push(`BUILD=${id.build}`);
  lines.push('CHECKSUM=0x000000');
  lines.push(`INFO_BLOCK_CHECKSUM=0x${infoCrc.toString(16).toUpperCase().padStart(6, '0')}`);
  lines.push('[FILE_INFO_HEADER]');
  lines.push('VERSION=3');
  lines.push('ENCRYPTION=0');
  lines.push('MAX_ENCRYPTION_BLOCKS=0');
  lines.push('[APPLICATION_INFO_HEADER]');
  lines.push('NAME=serial-app');
  lines.push('VERSION=1.0');
  for (const cfg of configs) {
    if (isRuntimeDataContainerObject(cfg.type)) continue;
    const objName = getMxtObjectName(cfg.type);
    lines.push(`[${objName || `UNKNOWN_T${cfg.type}`} INSTANCE ${cfg.instance}]`);
    lines.push(`OBJECT_ADDRESS=${cfg.address}`);
    lines.push(`OBJECT_SIZE=${cfg.size}`);
    for (let i = 0; i < cfg.size; i++) {
      lines.push(`${i} 1 UNKNOWN[${i}]=${cfg.data[i]}`);
    }
    lines.push('');
  }
  return lines.join('\n').replace(/\n+$/, '\n');
}

export async function readDeviceConfigViaSerialBridge(accessor: SerialPortAccessor): Promise<DeviceConfigSnapshot> {
  await accessor.drainRx(200);
  await switchMode0(accessor);
  await findI2cAddress(accessor);

  const idHeader = await readRegister(accessor, 0, 7);
  const numObjects = idHeader[6];
  const crcAreaSize = 7 + numObjects * 6;
  const infoBlockSize = crcAreaSize + 3;
  const infoRaw = await readRegisterAll(accessor, 0, infoBlockSize);
  const id: MxtIdHeader = {
    family: infoRaw[0],
    variant: infoRaw[1],
    version: infoRaw[2],
    build: infoRaw[3],
    matrixX: infoRaw[4],
    matrixY: infoRaw[5],
    numObjects
  };
  const objects = parseObjects(infoRaw, 7, numObjects);
  const infoCrc = parseInfoCrc(infoRaw, crcAreaSize);
  await restoreNv(accessor, objects);

  const configs: MxtConfigBlock[] = [];
  for (const obj of objects) {
    if (isVolatileMxtObject(obj.type) || isRuntimeDataContainerObject(obj.type)) continue;
    for (let instance = 0; instance < obj.instances; instance++) {
      const address = startPosition(obj, instance);
      const data = await readRegisterAll(accessor, address, obj.size);
      configs.push({ type: obj.type, instance, address, size: obj.size, data });
    }
  }
  return {
    family: id.family,
    variant: id.variant,
    version: id.version,
    build: id.build,
    matrixX: id.matrixX,
    matrixY: id.matrixY,
    numObjects: id.numObjects,
    infoCrc,
    objects: configs
  };
}

export async function readXcfgViaSerialBridge(accessor: SerialPortAccessor): Promise<string> {
  const snap = await readDeviceConfigViaSerialBridge(accessor);
  const id: MxtIdHeader = {
    family: snap.family,
    variant: snap.variant,
    version: snap.version,
    build: snap.build,
    matrixX: snap.matrixX,
    matrixY: snap.matrixY,
    numObjects: snap.numObjects
  };
  return buildXcfgFormat3(id, snap.infoCrc, snap.objects);
}

async function resolveT6Address(accessor: SerialPortAccessor) {
  const idHeader = await readRegister(accessor, 0, 7);
  const numObjects = idHeader[6];
  const infoBlockSize = 7 + numObjects * 6 + 3;
  const infoRaw = await readRegisterAll(accessor, 0, infoBlockSize);
  const objects = parseObjects(infoRaw, 7, numObjects);
  const t6Addr = getObjectAddress(objects, GEN_COMMANDPROCESSOR_T6, 0);
  if (t6Addr == null) throw new Error('未找到 T6 命令处理器');
  return t6Addr;
}

export async function backupNvViaSerialBridge(accessor: SerialPortAccessor): Promise<void> {
  await accessor.drainRx(200);
  await switchMode0(accessor);
  await findI2cAddress(accessor);
  const t6Addr = await resolveT6Address(accessor);
  await writeRegister(accessor, t6Addr + MXT_T6_BACKUPNV_OFFSET, Buffer.from([BACKUPNV_COMMAND]));
  await delay(2000);
}

export async function withSerialBridgeRestore<T>(accessor: SerialPortAccessor, fn: () => Promise<T>): Promise<T> {
  try {
    return await fn();
  } finally {
    try {
      await switchMode1(accessor);
    } catch (_) {}
  }
}

/** 由外部收发逻辑构造桥接访问器（支持 WinUSB 等非 SerialPort 场景） */
export function createBridgeAccessorFromHandlers(handlers: {
  write: (buf: Buffer) => Promise<void>;
  getRxLength: () => number;
  consumeRx: (maxSize: number) => Buffer;
  clearRx: () => void;
}): SerialPortAccessor {
  return {
    async write(buf: Buffer) {
      await handlers.write(buf);
    },
    async readSome(maxSize: number, timeoutMs = TRANSFER_TIMEOUT_MS) {
      const deadline = Date.now() + timeoutMs;
      while (handlers.getRxLength() === 0) {
        if (Date.now() >= deadline) {
          throw new Error('串口读取超时，未收到数据');
        }
        await delay(5);
      }
      let lastLen = handlers.getRxLength();
      let idleSince = Date.now();
      const idleMs = 50;
      while (Date.now() < deadline) {
        const len = handlers.getRxLength();
        if (len > lastLen) {
          lastLen = len;
          idleSince = Date.now();
        } else if (Date.now() - idleSince >= idleMs) {
          break;
        }
        if (len >= maxSize) break;
        await delay(5);
      }
      return handlers.consumeRx(Math.min(handlers.getRxLength(), maxSize));
    },
    async drainRx(timeoutMs = 300) {
      const deadline = Date.now() + timeoutMs;
      while (Date.now() < deadline) {
        if (handlers.getRxLength() === 0) {
          await delay(20);
          continue;
        }
        handlers.clearRx();
        await delay(20);
      }
      handlers.clearRx();
    }
  };
}

export function createSerialPortAccessor(port: SerialPort): SerialPortAccessor {
  let rxBuffer = Buffer.alloc(0);
  let dataHandler: ((chunk: Buffer) => void) | null = null;

  const attach = () => {
    if (dataHandler) return;
    dataHandler = (chunk: Buffer) => {
      rxBuffer = Buffer.concat([rxBuffer, chunk]);
    };
    port.removeAllListeners('data');
    port.on('data', dataHandler);
  };

  attach();

  return {
    async write(buf: Buffer) {
      await new Promise<void>((resolve, reject) => {
        port.write(buf, (err) => (err ? reject(err) : resolve()));
      });
    },
    async readSome(maxSize: number, timeoutMs = TRANSFER_TIMEOUT_MS) {
      const deadline = Date.now() + timeoutMs;
      while (rxBuffer.length === 0) {
        if (Date.now() >= deadline) {
          throw new Error('串口读取超时，未收到数据');
        }
        await delay(5);
      }
      let lastLen = rxBuffer.length;
      let idleSince = Date.now();
      const idleMs = 50;
      while (Date.now() < deadline) {
        if (rxBuffer.length > lastLen) {
          lastLen = rxBuffer.length;
          idleSince = Date.now();
        } else if (Date.now() - idleSince >= idleMs) {
          break;
        }
        if (rxBuffer.length >= maxSize) break;
        await delay(5);
      }
      const size = Math.min(rxBuffer.length, maxSize);
      const out = rxBuffer.subarray(0, size);
      rxBuffer = rxBuffer.subarray(size);
      return Buffer.from(out);
    },
    async drainRx(timeoutMs = 300) {
      const deadline = Date.now() + timeoutMs;
      while (Date.now() < deadline) {
        if (rxBuffer.length === 0) {
          await delay(20);
          continue;
        }
        rxBuffer = Buffer.alloc(0);
        await delay(20);
      }
      rxBuffer = Buffer.alloc(0);
    }
  };
}
