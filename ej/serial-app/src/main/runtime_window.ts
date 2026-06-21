import { app } from 'electron';
import * as crypto from 'crypto';
import * as fs from 'fs';
import * as http from 'http';
import * as https from 'https';
import * as path from 'path';

const RUNTIME_CFG_SECRET = 'mxt-runtime-window-v1';

export interface RuntimeWindowData {
  startMs: number;
  maxDays: number;
  expireMs?: number;
  fixedStartAt?: string;
  source: 'remote' | 'embedded';
}

export const DEFAULT_REMOTE_HTTP_PORT = 19876;

export interface RuntimeRemoteConfig {
  remoteBaseUrl: string;
  remoteFileName: string;
  remoteTimeoutMs: number;
  remoteAllowInsecureTls: boolean;
}

let cachedRuntimeWindow: RuntimeWindowData | null | undefined;

function buildRemoteBaseUrl(obj: Record<string, unknown>, fallbackHost: string, fallbackPort: number): string {
  const legacy = String(obj?.remoteBaseUrl || '').trim().replace(/\/+$/, '');
  if (legacy) {
    try {
      const u = new URL(legacy.includes('://') ? legacy : `http://${legacy}`);
      const host = u.hostname || fallbackHost;
      const port = u.port ? Number(u.port) : fallbackPort;
      return `http://${host}:${port}`;
    } catch {
      /* fall through */
    }
  }
  const host = String(obj?.remoteHost || fallbackHost).trim() || fallbackHost;
  const portRaw = Number(obj?.remoteHttpPort);
  const port = Number.isFinite(portRaw) && portRaw > 0 ? Math.floor(portRaw) : fallbackPort;
  return `http://${host}:${port}`;
}

function readRemoteFetchConfig(appPath: string): RuntimeRemoteConfig {
  const fallbackHost = '175.24.71.193';
  const fallback: RuntimeRemoteConfig = {
    remoteBaseUrl: `http://${fallbackHost}:${DEFAULT_REMOTE_HTTP_PORT}`,
    remoteFileName: 'runtime-window.generated.json',
    remoteTimeoutMs: 6000,
    remoteAllowInsecureTls: true
  };
  const cfgCandidates = [
    path.join(appPath, 'build', 'runtime-window.config.json'),
    path.join(process.resourcesPath || '', 'runtime-window.config.json')
  ];
  try {
    const cfgPath = cfgCandidates.find((p) => p && fs.existsSync(p));
    if (!cfgPath) return fallback;
    const obj = JSON.parse(fs.readFileSync(cfgPath, 'utf-8'));
    const remoteBaseUrl = buildRemoteBaseUrl(obj, fallbackHost, DEFAULT_REMOTE_HTTP_PORT);
    const remoteFileName = String(obj?.remoteFileName || fallback.remoteFileName).trim() || fallback.remoteFileName;
    const timeout = Number(obj?.remoteTimeoutMs);
    const remoteTimeoutMs = Number.isFinite(timeout) && timeout > 0 ? Math.floor(timeout) : fallback.remoteTimeoutMs;
    const remoteAllowInsecureTls = obj?.remoteAllowInsecureTls !== false;
    return { remoteBaseUrl, remoteFileName, remoteTimeoutMs, remoteAllowInsecureTls };
  } catch {
    return fallback;
  }
}

export function decryptRuntimePayload(payload: string): Record<string, unknown> | null {
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

function parseRuntimeWindowFromDecrypted(decrypted: Record<string, unknown>): Omit<RuntimeWindowData, 'source'> | null {
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
}

export function parseRuntimeWindowJsonText(text: string, source: RuntimeWindowData['source']): RuntimeWindowData | null {
  try {
    const obj = JSON.parse(text);
    const payload = typeof obj?.payload === 'string' ? obj.payload.trim() : '';
    if (!payload) return null;
    const decrypted = decryptRuntimePayload(payload);
    if (!decrypted) return null;
    const parsed = parseRuntimeWindowFromDecrypted(decrypted);
    if (!parsed) return null;
    return { ...parsed, source };
  } catch {
    return null;
  }
}

export function loadEmbeddedRuntimeWindow(appPath: string): RuntimeWindowData | null {
  try {
    const embeddedPath = path.join(appPath, 'dist', 'main', 'runtime-window.generated.json');
    if (!fs.existsSync(embeddedPath)) return null;
    return parseRuntimeWindowJsonText(fs.readFileSync(embeddedPath, 'utf-8'), 'embedded');
  } catch {
    return null;
  }
}

function fetchText(urlStr: string, timeoutMs: number, allowInsecureTls: boolean): Promise<string | null> {
  return new Promise((resolve) => {
    let settled = false;
    const done = (value: string | null) => {
      if (settled) return;
      settled = true;
      resolve(value);
    };

    try {
      const url = new URL(urlStr);
      const lib = url.protocol === 'https:' ? https : http;
      const agent =
        url.protocol === 'https:' && allowInsecureTls
          ? new https.Agent({ rejectUnauthorized: false })
          : undefined;

      const req = lib.get(
        url,
        { agent, timeout: timeoutMs, headers: { Accept: 'application/json,text/html,*/*' } },
        (res) => {
          if (res.statusCode && res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
            fetchText(new URL(res.headers.location, url).href, timeoutMs, allowInsecureTls).then(done);
            res.resume();
            return;
          }
          if (res.statusCode !== 200) {
            res.resume();
            done(null);
            return;
          }
          const chunks: Buffer[] = [];
          res.on('data', (chunk: Buffer) => chunks.push(chunk));
          res.on('end', () => done(Buffer.concat(chunks).toString('utf-8')));
        }
      );
      req.on('error', () => done(null));
      req.on('timeout', () => {
        req.destroy();
        done(null);
      });
    } catch {
      done(null);
    }
  });
}

function listJsonUrlsFromDirectoryHtml(html: string, baseUrl: string): string[] {
  const urls: string[] = [];
  const re = /href="([^"?#]+\.json)"/gi;
  let match: RegExpExecArray | null;
  while ((match = re.exec(html)) !== null) {
    try {
      urls.push(new URL(match[1], baseUrl).href);
    } catch {
      /* ignore bad href */
    }
  }
  const preferred = urls.filter((u) => /runtime-window/i.test(u));
  const others = urls.filter((u) => !/runtime-window/i.test(u));
  return [...preferred, ...others];
}

async function fetchRemoteRuntimeWindow(appPath: string): Promise<RuntimeWindowData | null> {
  const cfg = readRemoteFetchConfig(appPath);
  const candidates: string[] = [];
  if (cfg.remoteBaseUrl) {
    candidates.push(`${cfg.remoteBaseUrl}/${cfg.remoteFileName}`);
  }

  for (const url of candidates) {
    const text = await fetchText(url, cfg.remoteTimeoutMs, cfg.remoteAllowInsecureTls);
    if (!text) continue;
    const parsed = parseRuntimeWindowJsonText(text, 'remote');
    if (parsed) {
      console.log('[runtime-window] 使用远程配置:', url);
      return parsed;
    }
  }

  if (!cfg.remoteBaseUrl) return null;
  const indexText = await fetchText(`${cfg.remoteBaseUrl}/`, cfg.remoteTimeoutMs, cfg.remoteAllowInsecureTls);
  if (!indexText) return null;

  for (const jsonUrl of listJsonUrlsFromDirectoryHtml(indexText, cfg.remoteBaseUrl)) {
    const text = await fetchText(jsonUrl, cfg.remoteTimeoutMs, cfg.remoteAllowInsecureTls);
    if (!text) continue;
    const parsed = parseRuntimeWindowJsonText(text, 'remote');
    if (parsed) {
      console.log('[runtime-window] 目录索引命中远程配置:', jsonUrl);
      return parsed;
    }
  }

  return null;
}

/** 启动时调用一次：优先远程，失败则用编译内嵌 */
export async function ensureRuntimeWindowLoaded(): Promise<RuntimeWindowData | null> {
  if (cachedRuntimeWindow !== undefined) return cachedRuntimeWindow;

  if (!app.isPackaged) {
    cachedRuntimeWindow = null;
    return null;
  }

  const appPath = app.getAppPath();
  const remote = await fetchRemoteRuntimeWindow(appPath);
  if (remote) {
    cachedRuntimeWindow = remote;
    return remote;
  }

  const embedded = loadEmbeddedRuntimeWindow(appPath);
  if (embedded) {
    console.log('[runtime-window] 远程不可用，使用编译内嵌配置');
  } else {
    console.warn('[runtime-window] 远程与内嵌配置均不可用');
  }
  cachedRuntimeWindow = embedded;
  return embedded;
}

export function getCachedRuntimeWindow(): RuntimeWindowData | null {
  if (cachedRuntimeWindow !== undefined) return cachedRuntimeWindow;
  if (!app.isPackaged) return null;
  return loadEmbeddedRuntimeWindow(app.getAppPath());
}

export function isRuntimeWindowValid(cfg: RuntimeWindowData | null): boolean {
  if (!cfg) return false;
  const now = Date.now();
  const expireMs = cfg.startMs + cfg.maxDays * 24 * 60 * 60 * 1000;
  const effectiveExpireMs = Number.isFinite(cfg.expireMs) ? cfg.expireMs! : expireMs;
  const finalExpireMs = Math.min(expireMs, effectiveExpireMs);
  return now <= finalExpireMs;
}

export async function checkRuntimeWindowAllowed(): Promise<{ passed: boolean; source?: string }> {
  if (!app.isPackaged) return { passed: true, source: 'dev' };
  const cfg = await ensureRuntimeWindowLoaded();
  return {
    passed: isRuntimeWindowValid(cfg),
    source: cfg?.source
  };
}

export function readProjectRuntimeWindowConfig(appPath: string): { maxDays: number; fixedStartAt: string } {
  try {
    const cfgPath = path.join(appPath, 'build', 'runtime-window.config.json');
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

export function getRuntimeWindowDisplayConfig(appPath: string): { maxDays: number; fixedStartAt: string; source?: string } {
  const cached = getCachedRuntimeWindow();
  if (cached) {
    return { maxDays: cached.maxDays, fixedStartAt: cached.fixedStartAt || '', source: cached.source };
  }
  return readProjectRuntimeWindowConfig(appPath);
}
