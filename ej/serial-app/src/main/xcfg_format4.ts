/**
 * 将 MCU/桥接读回的对象字节合并到 maXTouch Studio format-4 模板（如 104HZ6208MS.xcfg），
 * 输出带命名字段、不含 T117 等运行时容器的 xcfg。
 */
import * as fs from 'fs';
import * as path from 'path';
import { app } from 'electron';
import { parseXcfg, updateObjectFieldsFromBytes, type XcfgData, type XcfgField, type XcfgObject } from './xcfg_codec';

export interface DeviceConfigObject {
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
  objects: DeviceConfigObject[];
}

export interface XcfgFormat4Template extends XcfgData {
  commentLines: string[];
  fileInfoHeader: Record<string, string>;
  includeDeviceSection: boolean;
}

function parseIntStrict(s: string): number {
  const t = (s || '').trim();
  if (!t) return 0;
  if (/^[-+]?\d+$/.test(t)) return parseInt(t, 10);
  if (/^0x[0-9a-fA-F]+$/.test(t)) return parseInt(t, 16);
  return 0;
}

/** 解析 Studio format-4 模板（保留 COMMENTS / FILE_INFO / DEVICE_0） */
export function parseXcfgFormat4Template(content: string): XcfgFormat4Template {
  const base = parseXcfg(content);
  const result: XcfgFormat4Template = {
    ...base,
    commentLines: [],
    fileInfoHeader: {},
    includeDeviceSection: false
  };

  const lines = (content || '').split(/\r?\n/);
  let i = 0;
  while (i < lines.length) {
    const stripped = (lines[i] ?? '').trim();
    if (stripped === '[COMMENTS]') {
      i++;
      while (i < lines.length && !lines[i].trim().startsWith('[')) {
        const l = lines[i];
        if (l.trim()) result.commentLines.push(l);
        i++;
      }
      continue;
    }
    if (stripped === '[FILE_INFO_HEADER]') {
      i++;
      while (i < lines.length) {
        const l = lines[i].trim();
        if (l.startsWith('[')) break;
        if (l.includes('=')) {
          const [k, ...rest] = l.split('=');
          result.fileInfoHeader[k.trim()] = rest.join('=').trim();
        }
        i++;
      }
      continue;
    }
    if (stripped === '[DEVICE_0]') {
      result.includeDeviceSection = true;
    }
    i++;
  }

  return result;
}

function objectKey(type: number, instance: number): string {
  return `${type}:${instance}`;
}

function findDeviceObject(snapshot: DeviceConfigSnapshot, type: number, instance: number): DeviceConfigObject | null {
  return snapshot.objects.find((o) => o.type === type && o.instance === instance) || null;
}

function formatTimestamp(): string {
  const d = new Date();
  const days = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
  const months = ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December'];
  const pad = (n: number) => String(n).padStart(2, '0');
  return `${days[d.getDay()]}, ${d.getDate()} ${months[d.getMonth()]} ${d.getFullYear()} ${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
}

function formatFieldValue(field: XcfgField): string {
  const v = field.value >>> 0;
  if (field.length >= 2 && /CRC|CHECKSUM|MASK|OFFSET|CONTNROFFSET/i.test(field.name)) {
    const hexWidth = field.length * 2;
    return `0x${v.toString(16).toUpperCase().padStart(hexWidth, '0')}`;
  }
  if (/^0x/i.test(String(field.value))) return String(field.value);
  return String(v);
}

/** 将设备读回字节合并进模板对象字段 */
export function mergeDeviceIntoFormat4Template(
  template: XcfgFormat4Template,
  device: DeviceConfigSnapshot
): XcfgFormat4Template {
  const merged: XcfgFormat4Template = {
    header: { ...template.header },
    application_header: { ...template.application_header },
    objects: template.objects.map((obj) => ({
      ...obj,
      fields: obj.fields.map((f) => ({ ...f }))
    })),
    commentLines: [...template.commentLines],
    fileInfoHeader: { ...template.fileInfoHeader },
    includeDeviceSection: template.includeDeviceSection
  };

  merged.header.FAMILY_ID = String(device.family);
  merged.header.VARIANT = String(device.variant);
  merged.header.VERSION = String(device.version);
  merged.header.BUILD = String(device.build);
  merged.header.MATRIX_X = String(device.matrixX);
  merged.header.MATRIX_Y = String(device.matrixY);
  merged.header.NO_OBJECTS = String(device.numObjects);
  merged.header.INFO_BLOCK_CHECKSUM = `0x${device.infoCrc.toString(16).toUpperCase().padStart(6, '0')}`;
  if (device.configCrc != null) {
    merged.header.CHECKSUM_DEVICE_0 = `0x${device.configCrc.toString(16).toUpperCase().padStart(6, '0')}`;
  }

  let matched = 0;
  let missing = 0;
  for (const obj of merged.objects) {
    const dev = findDeviceObject(device, obj.object_id, obj.instance);
    if (!dev) {
      missing += 1;
      continue;
    }
    if (dev.data.length >= obj.object_size) {
      obj.object_address = dev.address;
      updateObjectFieldsFromBytes(obj, new Uint8Array(dev.data.subarray(0, obj.object_size)));
      matched += 1;
    }
  }

  if (matched === 0) {
    throw new Error('模板与设备对象无法匹配（请确认模板与芯片型号一致）');
  }
  if (missing > 0) {
    // 模板中部分对象设备未返回（通常可忽略）
  }

  return merged;
}

export function serializeXcfgFormat4(template: XcfgFormat4Template, options?: { commentTitle?: string }): string {
  const lines: string[] = [];
  lines.push('[COMMENTS]');
  const title = options?.commentTitle?.trim();
  if (title) lines.push(title);
  for (const line of template.commentLines) {
    if (line.toLowerCase().includes('date and time')) {
      lines.push(`Date and time : ${formatTimestamp()}`);
    } else if (title && line === title) {
      continue;
    } else {
      lines.push(line);
    }
  }
  if (!template.commentLines.some((l) => l.toLowerCase().includes('date and time'))) {
    lines.push(`Date and time : ${formatTimestamp()}`);
  }
  if (!template.commentLines.some((l) => l.toLowerCase().includes('saved configuration'))) {
    lines.push('Saved configuration in device mode.');
  }
  lines.push('');

  lines.push('[VERSION_INFO_HEADER]');
  const headerOrder = [
    'FAMILY_ID', 'VARIANT', 'VERSION', 'BUILD',
    'MATRIX_X', 'MATRIX_Y', 'NO_OBJECTS', 'NO_DEVICES',
    'VENDOR_ID', 'PRODUCT_ID', 'CHECKSUM_DEVICE_0', 'INFO_BLOCK_CHECKSUM'
  ];
  for (const key of headerOrder) {
    if (template.header[key] != null) lines.push(`${key}=${template.header[key]}`);
  }
  for (const [k, v] of Object.entries(template.header)) {
    if (!headerOrder.includes(k)) lines.push(`${k}=${v}`);
  }
  lines.push('');

  if (Object.keys(template.fileInfoHeader).length > 0) {
    lines.push('[FILE_INFO_HEADER]');
    const fileOrder = ['VERSION', 'ENCRYPTION', 'MAX_ENCRYPTION_BLOCKS'];
    for (const key of fileOrder) {
      if (template.fileInfoHeader[key] != null) lines.push(`${key}=${template.fileInfoHeader[key]}`);
    }
    for (const [k, v] of Object.entries(template.fileInfoHeader)) {
      if (!fileOrder.includes(k)) lines.push(`${k}=${v}`);
    }
    lines.push('');
  }

  lines.push('[APPLICATION_INFO_HEADER]');
  const appName = template.application_header.NAME || template.application_header.name || 'maXTouchStudioLite';
  const appVer = template.application_header.VERSION || template.application_header.version || '3.1.3200';
  lines.push(`NAME=${appName}`);
  lines.push(`VERSION=${appVer}`);
  lines.push('');

  if (template.includeDeviceSection) {
    lines.push('[DEVICE_0]');
  }

  for (const obj of template.objects) {
    lines.push(`[${obj.header}]`);
    lines.push(`OBJECT_ADDRESS=${obj.object_address}`);
    lines.push(`OBJECT_SIZE=${obj.object_size}`);
    for (const f of obj.fields) {
      lines.push(`${f.offset} ${f.length} ${f.name}=${formatFieldValue(f)}`);
    }
    lines.push('');
  }

  return lines.join('\n').replace(/\n+$/, '\n');
}

export function buildFormat4XcfgFromDevice(templateContent: string, device: DeviceConfigSnapshot, options?: { commentTitle?: string }): string {
  const template = parseXcfgFormat4Template(templateContent);
  const merged = mergeDeviceIntoFormat4Template(template, device);
  return serializeXcfgFormat4(merged, options);
}

export function resolveXcfgFormat4TemplatePath(): string | null {
  const candidates = [
    path.join(process.resourcesPath || '', 'xcfg-templates', '104HZ6208MS.xcfg'),
    path.join(app.getAppPath(), 'resources', 'xcfg-templates', '104HZ6208MS.xcfg'),
    path.join(__dirname, '..', '..', 'resources', 'xcfg-templates', '104HZ6208MS.xcfg'),
    path.join(__dirname, '..', '..', '..', 'doc', '104HZ6208MS.xcfg'),
    path.join(process.cwd(), 'ej', 'doc', '104HZ6208MS.xcfg'),
    path.join(process.cwd(), '..', 'doc', '104HZ6208MS.xcfg')
  ];
  for (const p of candidates) {
    try {
      if (p && fs.existsSync(p)) return p;
    } catch (_) {}
  }
  return null;
}

export function loadXcfgFormat4Template(): string {
  const fp = resolveXcfgFormat4TemplatePath();
  if (!fp) {
    throw new Error('未找到 format-4 模板 104HZ6208MS.xcfg（请放入 resources/xcfg-templates/ 或 ej/doc/）');
  }
  return fs.readFileSync(fp, 'utf-8');
}

export function deviceObjectsFromSnapshot(objects: DeviceConfigObject[]): Map<string, DeviceConfigObject> {
  const map = new Map<string, DeviceConfigObject>();
  for (const o of objects) map.set(objectKey(o.type, o.instance), o);
  return map;
}
