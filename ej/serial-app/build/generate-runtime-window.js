const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const RUNTIME_CFG_SECRET = 'mxt-runtime-window-v1';

function pad2(v) {
  return String(v).padStart(2, '0');
}

function formatLocalDateTimeToSecond(date) {
  const y = date.getFullYear();
  const m = pad2(date.getMonth() + 1);
  const d = pad2(date.getDate());
  const hh = pad2(date.getHours());
  const mm = pad2(date.getMinutes());
  const ss = pad2(date.getSeconds());
  return `${y}-${m}-${d} ${hh}:${mm}:${ss}`;
}

function parseDateTextToMs(text) {
  if (!text) return NaN;
  const ms = Date.parse(String(text).replace(' ', 'T'));
  return Number.isFinite(ms) ? ms : NaN;
}

function encryptRuntimePayload(data) {
  const key = crypto.createHash('sha256').update(RUNTIME_CFG_SECRET).digest();
  const iv = crypto.randomBytes(12);
  const cipher = crypto.createCipheriv('aes-256-gcm', key, iv);
  const plaintext = Buffer.from(JSON.stringify(data), 'utf-8');
  const encrypted = Buffer.concat([cipher.update(plaintext), cipher.final()]);
  const tag = cipher.getAuthTag();
  return `v1:${iv.toString('base64')}:${tag.toString('base64')}:${encrypted.toString('base64')}`;
}

function readRuntimeConfig(projectDir) {
  const cfgPath = path.join(projectDir, 'build', 'runtime-window.config.json');
  const fallback = { maxDays: 30, startTimeType: 'build_time', fixedStartAt: '' };
  try {
    if (!fs.existsSync(cfgPath)) return fallback;
    const raw = fs.readFileSync(cfgPath, 'utf-8');
    const obj = JSON.parse(raw);
    const n = Number(obj?.maxDays);
    const maxDays = Number.isFinite(n) && n > 0 ? Math.floor(n) : fallback.maxDays;
    const startTimeTypeRaw = String(obj?.startTimeType || fallback.startTimeType).trim().toLowerCase();
    const startTimeType = ['build_time', 'fixed'].includes(startTimeTypeRaw) ? startTimeTypeRaw : fallback.startTimeType;
    const fixedStartAt = String(obj?.fixedStartAt || '').trim();
    return { maxDays, startTimeType, fixedStartAt };
  } catch {
    return fallback;
  }
}

function generate() {
  const projectDir = process.cwd();
  const cfg = readRuntimeConfig(projectDir);
  const maxDays = cfg.maxDays;
  let startMs = Date.now();
  if (cfg.startTimeType === 'fixed') {
    const fixedMs = parseDateTextToMs(cfg.fixedStartAt);
    if (Number.isFinite(fixedMs)) startMs = fixedMs;
  }
  const expireMs = startMs + maxDays * 24 * 60 * 60 * 1000;
  const payload = encryptRuntimePayload({
    start_time_type: cfg.startTimeType,
    start_at: formatLocalDateTimeToSecond(new Date(startMs)),
    fixed_start_at: cfg.fixedStartAt || '',
    max_days: maxDays,
    expire_at: formatLocalDateTimeToSecond(new Date(expireMs))
  });
  const outPath = path.join(projectDir, 'dist', 'main', 'runtime-window.generated.json');
  fs.mkdirSync(path.dirname(outPath), { recursive: true });
  fs.writeFileSync(outPath, `${JSON.stringify({ enc: 'aes-256-gcm-v1', payload }, null, 2)}\n`, 'utf-8');
}

generate();
