import { app, dialog, ipcMain } from 'electron';
import * as fs from 'fs';
import * as os from 'os';
import * as path from 'path';
import { parseXcfg, serializeXcfg, type XcfgData } from './xcfg_codec';

export interface MxtAppRunner {
  (args: string[], device?: string, timeout?: number): Promise<{ success: boolean; returncode: number; stdout: string; stderr: string }>;
}

export interface XcfgViewerDeps {
  getMxtAppPath: () => string | null;
  runMxtApp: MxtAppRunner;
  getDefaultMxtDevice?: () => string | undefined;
}

const SUPPORTED_VIDPID = new Set<string>([
  '0483:5740',
  '03eb:211d',
  '03eb:2119',
  '03eb:6123'
]);

for (let pid = 0x2126; pid <= 0x212d; pid++) SUPPORTED_VIDPID.add(`03eb:${pid.toString(16).padStart(4, '0')}`);
for (let pid = 0x2135; pid <= 0x2139; pid++) SUPPORTED_VIDPID.add(`03eb:${pid.toString(16).padStart(4, '0')}`);
for (let pid = 0x213a; pid <= 0x21fc; pid++) SUPPORTED_VIDPID.add(`03eb:${pid.toString(16).padStart(4, '0')}`);
for (let pid = 0x8000; pid <= 0x8fff; pid++) SUPPORTED_VIDPID.add(`03eb:${pid.toString(16).padStart(4, '0')}`);

let diagLatest: { content: string; ts: number; params: Record<string, unknown> } = {
  content: '',
  ts: 0,
  params: {}
};

function getViewerBaseDir(): string {
  return path.join(app.getPath('userData'), 'xcfg-viewer');
}

function getMetadataFile(): string {
  return path.join(getViewerBaseDir(), 'xcfg_viewer_metadata.json');
}

function getMetadataImagesDir(): string {
  return path.join(getViewerBaseDir(), 'metadata_images');
}

function getServerXcfgDir(): string {
  return path.join(getViewerBaseDir(), 'xcfg');
}

function ensureViewerDirs(): void {
  const base = getViewerBaseDir();
  fs.mkdirSync(base, { recursive: true });
  fs.mkdirSync(getMetadataImagesDir(), { recursive: true });
  fs.mkdirSync(getServerXcfgDir(), { recursive: true });
}

function getBundledMetadataPath(): string {
  const candidates = [
    path.join(process.resourcesPath || '', 'xcfg-viewer', 'xcfg_viewer_metadata.json'),
    path.join(app.getAppPath(), 'resources', 'xcfg-viewer', 'xcfg_viewer_metadata.json')
  ];
  for (const p of candidates) {
    if (p && fs.existsSync(p)) return p;
  }
  return '';
}

function getBundledMetadataImagesDir(): string {
  const candidates = [
    path.join(process.resourcesPath || '', 'xcfg-viewer', 'metadata_images'),
    path.join(app.getAppPath(), 'resources', 'xcfg-viewer', 'metadata_images')
  ];
  for (const p of candidates) {
    if (p && fs.existsSync(p)) return p;
  }
  return '';
}

function initBundledMetadataImagesIfNeeded(): void {
  const bundledDir = getBundledMetadataImagesDir();
  if (!bundledDir) return;
  const destDir = getMetadataImagesDir();
  fs.mkdirSync(destDir, { recursive: true });
  for (const name of fs.readdirSync(bundledDir)) {
    const src = path.join(bundledDir, name);
    const dest = path.join(destDir, name);
    if (!fs.statSync(src).isFile()) continue;
    if (!fs.existsSync(dest)) {
      fs.copyFileSync(src, dest);
    }
  }
}

function initMetadataIfNeeded(): void {
  ensureViewerDirs();
  const metaPath = getMetadataFile();
  if (!fs.existsSync(metaPath)) {
    const bundled = getBundledMetadataPath();
    if (bundled) {
      fs.copyFileSync(bundled, metaPath);
    } else {
      fs.writeFileSync(metaPath, JSON.stringify({ fieldAliases: {}, fieldDescriptions: {}, objectDescriptions: {} }, null, 2), 'utf-8');
    }
  }
  initBundledMetadataImagesIfNeeded();
}

function toFileUrl(filePath: string): string {
  const normalized = filePath.replace(/\\/g, '/');
  return normalized.startsWith('/') ? `file://${normalized}` : `file:///${normalized}`;
}

function rewriteMetadataImageUrls(metadata: Record<string, any>): Record<string, any> {
  const imgDir = getMetadataImagesDir();
  const rewriteText = (text: string): string => {
    if (!text || typeof text !== 'string') return text;
    return text.replace(/\/metadata_images\/([^)\s"']+)/g, (_m, name: string) => {
      const fp = path.join(imgDir, path.basename(name));
      return fs.existsSync(fp) ? toFileUrl(fp) : `/metadata_images/${name}`;
    });
  };

  const out = { ...metadata };
  if (out.fieldDescriptions && typeof out.fieldDescriptions === 'object') {
    const next: Record<string, string> = {};
    for (const [k, v] of Object.entries(out.fieldDescriptions)) next[k] = rewriteText(String(v));
    out.fieldDescriptions = next;
  }
  if (out.objectDescriptions && typeof out.objectDescriptions === 'object') {
    const next: Record<string, string> = {};
    for (const [k, v] of Object.entries(out.objectDescriptions)) next[k] = rewriteText(String(v));
    out.objectDescriptions = next;
  }
  return out;
}

function extractSupportedDevices(stdout: string): string[] {
  const devices: string[] = [];
  for (const line of (stdout || '').split(/\r?\n/)) {
    const m = line.trim().match(/^(usb:\d{3}-\d{3})\s+([0-9A-Fa-f]{4}:[0-9A-Fa-f]{4})\b/);
    if (!m) continue;
    if (SUPPORTED_VIDPID.has(m[2].toLowerCase())) devices.push(m[1]);
  }
  return devices;
}

async function autoDetectDevice(deps: XcfgViewerDeps): Promise<{ device: string | null; devices: string[]; error?: string }> {
  const fromSerialApp = deps.getDefaultMxtDevice?.();
  if (fromSerialApp) {
    return { device: fromSerialApp, devices: [fromSerialApp] };
  }

  const q = await deps.runMxtApp(['-q'], undefined, 10000);
  if (!q.success) return { device: null, devices: [], error: q.stderr || q.stdout || '枚举设备失败' };
  const devices = extractSupportedDevices(q.stdout);
  if (devices.length === 1) return { device: devices[0], devices };
  if (devices.length === 0) {
    return { device: null, devices, error: `未找到支持的设备（支持 VID:PID: ${[...SUPPORTED_VIDPID].sort().join(', ')}）` };
  }
  return { device: null, devices, error: '检测到多个支持的设备，请指定 device 参数' };
}

async function runMxtAppAuto(
  deps: XcfgViewerDeps,
  args: string[],
  device?: string | null,
  timeout = 30000
): Promise<{ success: boolean; returncode: number; stdout: string; stderr: string; devices?: string[] }> {
  let injectDevice = (device || '').trim() || undefined;
  if (!injectDevice) {
    const envDevice = (process.env.MXT_DEVICE || '').trim();
    if (envDevice) injectDevice = envDevice;
    else {
      const detected = await autoDetectDevice(deps);
      if (!detected.device) {
        return {
          success: false,
          returncode: 1,
          stdout: '',
          stderr: detected.error || '自动枚举设备失败',
          devices: detected.devices
        };
      }
      injectDevice = detected.device;
    }
  }
  const result = await deps.runMxtApp(args, injectDevice, timeout);
  return result;
}

function u16ToI16(v: number): number {
  const n = Number(v) & 0xffff;
  return (n & 0x8000) ? n - 0x10000 : n;
}

function convertMutualRefsCsvToSigned(content: string): string {
  if (!content) return content;
  const timeRe = /^\d{2}:\d{2}:\d{2}(?:\.\d+)?$/;
  const out: string[] = [];
  for (const line of content.split(/\r?\n/)) {
    const cols = line.split(',');
    if (cols.length < 3 || !timeRe.test(cols[0].trim())) {
      out.push(line);
      continue;
    }
    for (let i = 2; i < cols.length; i++) {
      const s = cols[i].trim();
      if (!s) continue;
      if (/^-?\d+$/.test(s)) {
        try { cols[i] = String(u16ToI16(parseInt(s, 10))); } catch (_) {}
      }
    }
    out.push(cols.join(','));
  }
  return out.join('\n');
}

function buildDebugDumpArgs(mode: string, kind: string, frames: number, instance: number, fmt: number): string[] {
  const args = ['--debug-dump', '-', '--frames', String(frames), '--instance', String(instance), '--format', String(fmt)];
  if (mode === 'm') {
    if (kind === 'r') args.push('--references');
  } else if (mode === 's') {
    if (kind === 'd') args.push('--self-cap-deltas');
    else if (kind === 'r') args.push('--self-cap-refs');
  } else if (mode === 'k') {
    if (kind === 'd') args.push('--key-array-deltas');
    else if (kind === 'r') args.push('--key-array-refs');
    else if (kind === 's') args.push('--key-array-signals');
  } else if (mode === 'a') {
    if (kind === 'd') args.push('--active-stylus-deltas');
    else if (kind === 'r') args.push('--active-stylus-refs');
  }
  return args;
}

function parseReadObjectStdout(stdout: string): { object_name?: string; fields: Array<{ offset: number; length: number; name: string; value: number }> } {
  const fields: Array<{ offset: number; length: number; name: string; value: number }> = [];
  let object_name: string | undefined;
  for (const line of (stdout || '').split(/\r?\n/)) {
    const s = line.trim();
    if (!s) continue;
    if (s.startsWith('T') && s.includes('-') && !s.includes(':')) {
      object_name = s;
      continue;
    }
    const m = s.match(/^(\d+):\s+0x([0-9A-Fa-f]+)/);
    if (m) {
      fields.push({
        offset: parseInt(m[1], 10),
        length: 1,
        name: `UNKNOWN[${m[1]}]`,
        value: parseInt(m[2], 16)
      });
    }
  }
  return { object_name, fields };
}

function toU8(v: unknown): number {
  if (typeof v === 'number') return v & 0xff;
  const s = String(v ?? '').trim();
  if (!s) throw new Error('空字符串');
  if (/^0x[0-9a-fA-F]+$/.test(s)) return parseInt(s, 16) & 0xff;
  if (/[a-fA-F]/.test(s)) return parseInt(s, 16) & 0xff;
  return parseInt(s, 10) & 0xff;
}

function apiOk(data: unknown, status = 200): { status: number; data: unknown } {
  return { status, data };
}

function apiErr(message: string, status = 400, extra: Record<string, unknown> = {}): { status: number; data: unknown } {
  return { status, data: { error: message, ...extra } };
}

export async function handleXcfgViewerRequest(
  deps: XcfgViewerDeps,
  req: { path: string; method?: string; body?: any }
): Promise<{ status: number; data: unknown; contentType?: string; binary?: Buffer }> {
  initMetadataIfNeeded();
  const method = String(req.method || 'GET').toUpperCase();
  const rawPath = String(req.path || '');
  const url = new URL(rawPath.startsWith('http') ? rawPath : `http://local${rawPath.startsWith('/') ? '' : '/'}${rawPath}`);
  const pathname = url.pathname;

  if (pathname.startsWith('/metadata_images/')) {
    const filename = path.basename(pathname.replace('/metadata_images/', ''));
    const fp = path.join(getMetadataImagesDir(), filename);
    if (!fs.existsSync(fp)) return { status: 404, data: { error: 'not found' } };
    const ext = path.extname(fp).toLowerCase();
    const mime = ext === '.png' ? 'image/png'
      : ext === '.jpg' || ext === '.jpeg' ? 'image/jpeg'
      : ext === '.gif' ? 'image/gif'
      : ext === '.webp' ? 'image/webp'
      : ext === '.svg' ? 'image/svg+xml'
      : 'application/octet-stream';
    return { status: 200, data: null, contentType: mime, binary: fs.readFileSync(fp) };
  }

  if (pathname === '/api/metadata' && method === 'GET') {
    try {
      const text = fs.readFileSync(getMetadataFile(), 'utf-8');
      const data = rewriteMetadataImageUrls(JSON.parse(text));
      return apiOk(data);
    } catch (e: any) {
      return apiErr(e?.message || String(e), 500);
    }
  }

  if (pathname === '/api/metadata' && method === 'POST') {
    const data = req.body || {};
    try {
      const out = {
        fieldAliases: data.fieldAliases || {},
        fieldDescriptions: data.fieldDescriptions || {},
        objectDescriptions: data.objectDescriptions || {}
      };
      fs.writeFileSync(getMetadataFile(), JSON.stringify(out, null, 2), 'utf-8');
      return apiOk({ success: true, path: getMetadataFile() });
    } catch (e: any) {
      return apiErr(e?.message || String(e), 500);
    }
  }

  if (pathname === '/api/parse-xcfg' && method === 'POST') {
    const body = req.body || {};
    let content = body.content || body.text || '';
    if (body.__formData) {
      if (body.fields && body.fields.content) content = String(body.fields.content);
      const file = Array.isArray(body.files) ? body.files[0] : null;
      if (!content && file && file.data) {
        content = Buffer.from(file.data).toString('utf-8');
      }
    }
    if (!content) return apiErr('请上传 xcfg 文件或提供 content');
    try {
      return apiOk(parseXcfg(String(content)));
    } catch (e: any) {
      return apiErr(e?.message || String(e), 500);
    }
  }

  if (pathname === '/api/save-xcfg' && method === 'POST') {
    const body = req.body || {};
    const data: XcfgData = body.config || body;
    if (!data) return apiErr('请提供 config 数据');
    try {
      const content = serializeXcfg(data);
      let savedServerPath: string | null = null;
      const savePath = String(body.path || '').trim();
      if (savePath) {
        const abs = path.resolve(savePath.endsWith('.xcfg') ? savePath : `${savePath}.xcfg`);
        fs.writeFileSync(abs, content, 'utf-8');
      }
      if (body.saveToServer) {
        let baseName = String(body.name || 'config').trim() || 'config';
        baseName = path.basename(baseName);
        if (!baseName.toLowerCase().endsWith('.xcfg')) baseName += '.xcfg';
        savedServerPath = path.join(getServerXcfgDir(), baseName);
        fs.writeFileSync(savedServerPath, content, 'utf-8');
      }
      const resp: Record<string, unknown> = { content, success: true };
      if (savePath) resp.path = path.resolve(savePath.endsWith('.xcfg') ? savePath : `${savePath}.xcfg`);
      if (savedServerPath) resp.serverPath = savedServerPath;
      return apiOk(resp);
    } catch (e: any) {
      return apiErr(e?.message || String(e), 500);
    }
  }

  if (pathname === '/api/server-xcfg-list' && method === 'GET') {
    try {
      const files = fs.readdirSync(getServerXcfgDir())
        .filter((n) => n.toLowerCase().endsWith('.xcfg'))
        .map((name) => {
          const stat = fs.statSync(path.join(getServerXcfgDir(), name));
          return { name, size: stat.size, mtime: Math.floor(stat.mtimeMs / 1000) };
        })
        .sort((a, b) => b.mtime - a.mtime);
      return apiOk({ files });
    } catch (e: any) {
      return apiErr(e?.message || String(e), 500);
    }
  }

  if (pathname === '/api/load-server-xcfg' && method === 'POST') {
    let name = String((req.body || {}).name || '').trim();
    if (!name) return apiErr('请提供文件名');
    name = path.basename(name);
    if (!name.toLowerCase().endsWith('.xcfg')) name += '.xcfg';
    const fp = path.join(getServerXcfgDir(), name);
    if (!fs.existsSync(fp)) return apiErr(`文件不存在: ${name}`, 404);
    try {
      const content = fs.readFileSync(fp, 'utf-8');
      return apiOk({ success: true, data: parseXcfg(content), name });
    } catch (e: any) {
      return apiErr(e?.message || String(e), 500);
    }
  }

  if (pathname === '/api/write-device' && method === 'POST') {
    const body = req.body || {};
    const cfg: XcfgData = body.config || body;
    if (!cfg) return apiErr('请提供 JSON 数据');
    const tmpPath = path.join(os.tmpdir(), `mxt_${Date.now()}.xcfg`);
    try {
      fs.writeFileSync(tmpPath, serializeXcfg(cfg), 'utf-8');
      const result = await runMxtAppAuto(deps, ['--load', tmpPath], body.device, 60000);
      if (result.success) return apiOk({ success: true, message: '配置已成功写入设备', stdout: result.stdout });
      return apiErr(result.stderr || result.stdout || `退出码 ${result.returncode}`, 500, { devices: result.devices });
    } catch (e: any) {
      return apiErr(e?.message || String(e), 500);
    } finally {
      try { if (fs.existsSync(tmpPath)) fs.unlinkSync(tmpPath); } catch (_) {}
    }
  }

  if (pathname === '/api/read-device' && method === 'POST') {
    const body = req.body || {};
    const tmpPath = path.join(os.tmpdir(), `mxt_read_${Date.now()}.xcfg`);
    try {
      let device = String(body.device || '').trim() || undefined;
      if (!device) {
        const detected = await autoDetectDevice(deps);
        if (!detected.device) return apiErr(detected.error || '未找到设备', detected.devices?.length ? 409 : 404, { devices: detected.devices });
        device = detected.device;
      }
      const result = await deps.runMxtApp(['--save', tmpPath, '--format', '3'], device, 30000);
      if (!result.success) return apiErr(result.stderr || result.stdout || `退出码 ${result.returncode}`, 500);
      const content = fs.readFileSync(tmpPath, 'utf-8');
      return apiOk({ success: true, data: parseXcfg(content), content });
    } catch (e: any) {
      return apiErr(e?.message || String(e), 500);
    } finally {
      try { if (fs.existsSync(tmpPath)) fs.unlinkSync(tmpPath); } catch (_) {}
    }
  }

  if (pathname === '/api/read-object' && method === 'POST') {
    const body = req.body || {};
    const objType = parseInt(String(body.object_type || 0), 10);
    const instance = parseInt(String(body.instance || 0), 10);
    if (objType <= 0) return apiErr('请提供 object_type（如 100）');
    const result = await runMxtAppAuto(deps, ['-R', '-T', String(objType), '-I', String(instance), '-f', '1'], body.device, 15000);
    if (!result.success) return apiErr(result.stderr || result.stdout || '读取失败', 500, { devices: result.devices });
    const parsed = parseReadObjectStdout(result.stdout);
    return apiOk({
      success: true,
      object_type: objType,
      instance,
      object_name: parsed.object_name || `T${objType}`,
      fields: parsed.fields
    });
  }

  if (pathname === '/api/write-object' && method === 'POST') {
    const body = req.body || {};
    const objType = parseInt(String(body.object_type || 0), 10);
    const instance = parseInt(String(body.instance || 0), 10);
    const bytesData = body.data || [];
    if (objType <= 0) return apiErr('请提供 object_type');
    if (!Array.isArray(bytesData) || bytesData.length === 0) return apiErr('请提供 data（字节数组）');
    try {
      const hexArgs = bytesData.map((b: unknown) => `${toU8(b).toString(16).toUpperCase().padStart(2, '0')}`);
      const result = await runMxtAppAuto(deps, ['-W', '-T', String(objType), '-I', String(instance), ...hexArgs], body.device, 15000);
      if (result.success) return apiOk({ success: true, message: '写入完成' });
      return apiErr(result.stderr || result.stdout || '写入失败', 500, { devices: result.devices });
    } catch (e: any) {
      return apiErr(e?.message || String(e), 400);
    }
  }

  if (pathname === '/api/backup' && method === 'POST') {
    const result = await runMxtAppAuto(deps, ['--backup'], (req.body || {}).device, 30000);
    if (result.success) return apiOk({ success: true, stdout: result.stdout });
    return apiErr(result.stderr || result.stdout || '备份失败', 500);
  }

  if (pathname === '/api/reset' && method === 'POST') {
    const result = await runMxtAppAuto(deps, ['--reset'], (req.body || {}).device, 20000);
    if (result.success) return apiOk({ success: true, stdout: result.stdout });
    return apiErr(result.stderr || result.stdout || '复位失败', 500);
  }

  if (pathname === '/api/calibrate' && method === 'POST') {
    const result = await runMxtAppAuto(deps, ['--calibrate'], (req.body || {}).device, 30000);
    if (result.success) return apiOk({ success: true, stdout: result.stdout });
    return apiErr(result.stderr || result.stdout || '校准失败', 500);
  }

  if (pathname === '/api/send-command' && method === 'POST') {
    const command = String((req.body || {}).command || 'mode0').trim();
    if (!command) return apiErr('command 不能为空');
    const result = await runMxtAppAuto(deps, command.split(/\s+/g), (req.body || {}).device, 20000);
    if (result.success) return apiOk({ success: true, stdout: result.stdout });
    return apiErr(result.stderr || result.stdout || '发送失败', 500);
  }

  if (pathname === '/api/info' && method === 'POST') {
    const result = await runMxtAppAuto(deps, ['--info'], (req.body || {}).device, 20000);
    if (result.success) return apiOk({ success: true, stdout: result.stdout });
    return apiErr(result.stderr || result.stdout || '读取失败', 500);
  }

  if (pathname === '/api/upload-image' && method === 'POST') {
    const body = req.body || {};
    let files = Array.isArray(body.files) ? body.files : [];
    if (body.__formData && Array.isArray(body.files)) files = body.files;
    const file = files[0];
    if (!file || !file.data) return apiErr('请选择图片文件');
    const ext = path.extname(file.name || '.png').toLowerCase() || '.png';
    const allowed = new Set(['.png', '.jpg', '.jpeg', '.gif', '.webp', '.svg']);
    if (!allowed.has(ext)) return apiErr('仅支持图片格式 png/jpg/gif/webp/svg');
    const base = (path.basename(file.name || 'image', ext) || 'image').replace(/[^a-zA-Z0-9._-]/g, '_') || 'image';
    const name = `${base}-${Date.now()}-${Math.floor(1000 + Math.random() * 8999)}${ext}`;
    const dest = path.join(getMetadataImagesDir(), name);
    try {
      fs.writeFileSync(dest, Buffer.from(file.data));
      return apiOk({ success: true, url: toFileUrl(dest) });
    } catch (e: any) {
      return apiErr(e?.message || String(e), 500);
    }
  }

  if (pathname === '/api/rename-image' && method === 'POST') {
    const body = req.body || {};
    const oldName = path.basename(String(body.old || '').trim());
    const newName = path.basename(String(body.new || '').trim());
    if (!oldName || !newName) return apiErr('请提供 old/new 文件名');
    const allowed = new Set(['.png', '.jpg', '.jpeg', '.gif', '.webp', '.svg']);
    const oldExt = path.extname(oldName).toLowerCase();
    const newExt = path.extname(newName).toLowerCase();
    if (!allowed.has(oldExt) || !allowed.has(newExt) || oldExt !== newExt) return apiErr('不允许修改扩展名或格式不支持');
    const src = path.join(getMetadataImagesDir(), oldName);
    const dst = path.join(getMetadataImagesDir(), newName);
    if (!fs.existsSync(src)) return apiErr(`原文件不存在: ${oldName}`, 404);
    if (fs.existsSync(dst)) return apiErr(`目标文件已存在: ${newName}`, 409);
    try {
      fs.renameSync(src, dst);
      return apiOk({ success: true, url: toFileUrl(dst) });
    } catch (e: any) {
      return apiErr(e?.message || String(e), 500);
    }
  }

  if (pathname === '/api/diag/capture' && method === 'POST') {
    const body = req.body || {};
    const mode = String(body.mode || 'm').toLowerCase();
    const kind = String(body.kind || 'd').toLowerCase();
    const frames = parseInt(String(body.frames || 1), 10);
    const instance = parseInt(String(body.instance || 0), 10);
    const fmt = parseInt(String(body.format || 0), 10);
    if (!['m', 's', 'k', 'a'].includes(mode)) return apiErr('mode 仅支持 m/s/k/a');
    const args = buildDebugDumpArgs(mode, kind, frames, instance, fmt);
    const result = await runMxtAppAuto(deps, args, body.device, Math.max(30000, frames * 10000));
    if (!result.success) return apiErr(result.stderr || result.stdout || '采集失败', 500);
    let content = result.stdout || '';
    if (mode === 'm' && kind === 'r') content = convertMutualRefsCsvToSigned(content);
    diagLatest = {
      content,
      ts: Date.now(),
      params: { mode, kind, frames, instance, format: fmt }
    };
    return apiOk({ success: true, content, ts: diagLatest.ts, params: diagLatest.params });
  }

  if (pathname === '/api/diag/latest' && method === 'GET') {
    return apiOk({ success: true, content: diagLatest.content, ts: diagLatest.ts, params: diagLatest.params });
  }

  if (pathname === '/api/save-xcfg-dialog' && method === 'POST') {
    const body = req.body || {};
    const data: XcfgData = body.config || body;
    if (!data) return apiErr('请提供 config 数据');
    try {
      const content = serializeXcfg(data);
      const defaultName = String(body.name || 'config.xcfg');
      const result = await dialog.showSaveDialog({
        title: '保存 XCFG 文件',
        defaultPath: path.join(getServerXcfgDir(), defaultName),
        filters: [{ name: 'XCFG', extensions: ['xcfg'] }]
      });
      if (result.canceled || !result.filePath) return apiOk({ success: false, canceled: true });
      fs.writeFileSync(result.filePath, content, 'utf-8');
      return apiOk({ success: true, path: result.filePath, content });
    } catch (e: any) {
      return apiErr(e?.message || String(e), 500);
    }
  }

  return apiErr(`未知接口: ${pathname}`, 404);
}

export function registerXcfgViewerIpc(deps: XcfgViewerDeps): void {
  ipcMain.handle('xcfg-viewer-api', async (_event, req: { path: string; method?: string; body?: any }) => {
    try {
      const result = await handleXcfgViewerRequest(deps, req || {});
      if (result.binary) {
        return {
          status: result.status,
          data: result.data,
          contentType: result.contentType,
          binary: Array.from(result.binary)
        };
      }
      return result;
    } catch (e: any) {
      return { status: 500, data: { error: e?.message || String(e) } };
    }
  });

  ipcMain.handle('xcfg-viewer-get-initial', async () => {
    return { success: true, pending: (global as any).__xcfgViewerPendingInitial || null };
  });

  ipcMain.handle('xcfg-viewer-clear-initial', async () => {
    (global as any).__xcfgViewerPendingInitial = null;
    return { success: true };
  });

  ipcMain.handle('xcfg-viewer-get-connection', async () => {
    const getter = deps.getDefaultMxtDevice;
    const mxtDevice = getter ? getter() : undefined;
    return {
      success: true,
      connected: Boolean(mxtDevice),
      mxtDevice: mxtDevice || null,
      source: mxtDevice ? 'serial-app-winusb' : null
    };
  });
}

export function setXcfgViewerInitialPayload(payload: { content?: string; fileName?: string } | null): void {
  (global as any).__xcfgViewerPendingInitial = payload;
}
