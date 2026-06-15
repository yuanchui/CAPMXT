export type XcfgKVMap = Record<string, string>;

export interface XcfgField {
  offset: number;
  length: number;
  name: string;
  value: number;
}

export interface XcfgObject {
  header: string; // section header content without [ ]
  object_id: number;
  instance: number;
  object_address: number;
  object_size: number;
  fields: XcfgField[];
}

export interface XcfgData {
  header: XcfgKVMap;
  application_header: XcfgKVMap;
  objects: XcfgObject[];
}

function parseIntStrict(s: string): number {
  const t = (s || '').trim();
  if (!t) return 0;
  if (/^[-+]?\d+$/.test(t)) return parseInt(t, 10);
  if (/^0x[0-9a-fA-F]+$/.test(t)) return parseInt(t, 16);
  return 0;
}

function parseFieldLine(line: string): XcfgField | null {
  const raw = (line || '').trim();
  if (!raw || !raw.includes('=')) return null;

  const [left, valueStr] = raw.split('=', 2);
  const leftTokens = left.trim().split(/\s+/g);
  if (leftTokens.length < 3) return null;

  const offset = parseInt(leftTokens[0], 10);
  const length = parseInt(leftTokens[1], 10);
  if (!Number.isFinite(offset) || !Number.isFinite(length)) return null;

  const name = leftTokens.slice(2).join(' ');
  const value = parseIntStrict(valueStr);
  return { offset, length, name, value };
}

export function parseXcfg(content: string): XcfgData {
  const result: XcfgData = {
    header: {},
    application_header: {},
    objects: []
  };

  const lines = (content || '').split(/\r?\n/);
  let i = 0;
  while (i < lines.length) {
    const line = lines[i] ?? '';
    const stripped = line.trim();
    if (!stripped) {
      i++;
      continue;
    }

    if (stripped.startsWith('[')) {
      const sectionMatch = stripped.match(/^\[([^\]]+)\]$/);
      if (!sectionMatch) {
        i++;
        continue;
      }

      const section = sectionMatch[1].trim();

      // COMMENTS: skip until next [SECTION]
      if (section === 'COMMENTS') {
        i++;
        while (i < lines.length && !lines[i].trim().startsWith('[')) i++;
        continue;
      }

      // APPLICATION_INFO_HEADER
      if (section === 'APPLICATION_INFO_HEADER') {
        i++;
        while (i < lines.length) {
          const l = lines[i].trim();
          if (l.startsWith('[')) break;
          if (l.includes('=')) {
            const [k, ...rest] = l.split('=');
            const v = rest.join('=');
            result.application_header[k.trim()] = v.trim();
          }
          i++;
        }
        continue;
      }

      // VERSION_INFO_HEADER
      if (section === 'VERSION_INFO_HEADER') {
        i++;
        while (i < lines.length) {
          const l = lines[i].trim();
          if (l.startsWith('[')) break;
          if (l.includes('=')) {
            const [k, ...rest] = l.split('=');
            const v = rest.join('=');
            result.header[k.trim()] = v.trim();
          }
          i++;
        }
        continue;
      }

      // Object sections: [Txx-xxx INSTANCE n]
      const instMatch = section.match(/INSTANCE\s+(\d+)/i);
      const objMatch = section.match(/T(\d+)/i);
      if (instMatch && objMatch) {
        const object_id = parseInt(objMatch[1], 10);
        const instance = parseInt(instMatch[1], 10);
        const obj: XcfgObject = {
          header: section,
          object_id,
          instance,
          object_address: 0,
          object_size: 0,
          fields: []
        };

        i++;
        while (i < lines.length) {
          const l = lines[i].trim();
          if (!l) {
            i++;
            continue;
          }
          if (l.startsWith('[')) break;

          if (l.startsWith('OBJECT_ADDRESS=')) {
            obj.object_address = parseInt(l.split('=', 2)[1].trim(), 10);
          } else if (l.startsWith('OBJECT_SIZE=')) {
            obj.object_size = parseInt(l.split('=', 2)[1].trim(), 10);
          } else if (l.includes('=')) {
            const f = parseFieldLine(l);
            if (f) obj.fields.push(f);
          }
          i++;
        }
        result.objects.push(obj);
        continue;
      }
    }

    i++;
  }

  return result;
}

export function buildObjectBytes(obj: XcfgObject): Uint8Array {
  const buf = new Uint8Array(obj.object_size);
  for (const f of obj.fields) {
    let val = f.value;
    // normalize to 32-bit
    val = val >>> 0;
    for (let k = 0; k < f.length; k++) {
      const idx = f.offset + k;
      if (idx < 0 || idx >= buf.length) continue;
      buf[idx] = (val >> (k * 8)) & 0xff;
    }
  }
  return buf;
}

export function updateObjectFieldsFromBytes(obj: XcfgObject, bytes: Uint8Array): void {
  for (const f of obj.fields) {
    let val = 0;
    for (let k = 0; k < f.length; k++) {
      const idx = f.offset + k;
      if (idx < 0 || idx >= bytes.length) continue;
      val |= bytes[idx] << (k * 8);
    }
    // Keep as unsigned integer representation; xcfg serializer in our UI can handle it.
    f.value = val >>> 0;
  }
}

export function serializeXcfg(data: XcfgData): string {
  const lines: string[] = [];

  if (data.header && Object.keys(data.header).length > 0) {
    lines.push('[VERSION_INFO_HEADER]');
    for (const [k, v] of Object.entries(data.header)) {
      lines.push(`${k}=${v}`);
    }
    lines.push('');
  }

  // APPLICATION_INFO_HEADER
  const appHeader = data.application_header ?? {};
  if (appHeader && Object.keys(appHeader).length > 0) {
    lines.push('[APPLICATION_INFO_HEADER]');
    // Keep same keys as python serializer uses.
    const name = appHeader['NAME'] ?? appHeader['name'] ?? 'libmaxtouch';
    const version = appHeader['VERSION'] ?? appHeader['version'] ?? '1.0';
    lines.push(`NAME=${name}`);
    lines.push(`VERSION=${version}`);
    lines.push('');
  }

  for (const obj of data.objects) {
    lines.push(`[${obj.header}]`);
    lines.push(`OBJECT_ADDRESS=${obj.object_address}`);
    lines.push(`OBJECT_SIZE=${obj.object_size}`);
    for (const f of obj.fields) {
      lines.push(`${f.offset} ${f.length} ${f.name}=${f.value}`);
    }
    lines.push('');
  }

  return lines.join('\n');
}

