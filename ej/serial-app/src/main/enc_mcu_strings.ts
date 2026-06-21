/**
 * ENC 烧录前 MCU 字符串命令交互（mode1）— 与 mxt_cmd.c 中
 * FINDIIC/INFO/ENCRESETBL/FINDBL/ENCUNLOCK/BRIDGEBIN 对应。
 */
import { MODE1_TEXT, MODE_SWITCH_TO_STRING_BYTES } from './bridge_mode';
import { ENC_POST_STRING_IO_TIMEOUT_MS } from './enc_protocol';

export interface McuStringSession {
  sendLine: (line: string, timeoutMs?: number) => Promise<void>;
  sendRaw: (data: Uint8Array, timeoutMs?: number) => Promise<void>;
  waitText: (hints: string[], timeoutMs: number) => Promise<string>;
  waitRxIdle?: (quietMs?: number, maxWaitMs?: number) => Promise<void>;
  drainRx?: () => string;
  trimRxAfter?: (hints: string[]) => void;
  flushBridgeLogs?: (text: string, direction: 'tx' | 'rx') => void;
}

export interface ChipInfoParsed {
  familyId: number;
  variantId: number;
  versionMajor: number;
  versionMinor: number;
  build: number;
  matrixX: number;
  matrixY: number;
  numObjects: number;
  rawLine: string;
}

const INFO_RE =
  /Family=0x([0-9A-Fa-f]{2}).*Variant=0x([0-9A-Fa-f]{2}).*Version=(\d+)\.(\d+).*Build=(\d+).*Matrix X=(\d+).*Y=(\d+).*NumObj=(\d+)/;

/** 是否已有包含 hint 的完整行（必须以换行结束，避免 USB 分包半行误判） */
export function hasCompleteMatchingLine(acc: string, hints: string[]): boolean {
  if (!acc) return false;
  const endsWithNl = acc.endsWith('\n') || acc.endsWith('\r');
  const lines = acc.split(/\r\n|\n|\r/);
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    if (!line) continue;
    const isComplete = i < lines.length - 1 || endsWithNl;
    if (isComplete && hints.some((h) => line.includes(h))) return true;
  }
  return false;
}

export function extractMatchingLine(text: string, hints: string[]): string | null {
  const endsWithNl = text.endsWith('\n') || text.endsWith('\r');
  const lines = text.split(/\r\n|\n|\r/);
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    if (!line) continue;
    const isComplete = i < lines.length - 1 || endsWithNl;
    if (isComplete && hints.some((h) => line.includes(h))) return line;
  }
  return null;
}

export function consumeMatchingLine(acc: string, hints: string[]): { line: string | null; rest: string } {
  const line = extractMatchingLine(acc, hints);
  if (!line) return { line: null, rest: acc };
  const idx = acc.indexOf(line);
  if (idx < 0) return { line, rest: acc };
  let end = idx + line.length;
  if (acc[end] === '\r') end++;
  if (acc[end] === '\n') end++;
  return { line, rest: acc.slice(end) };
}

export function parseInfoBlockLine(text: string): ChipInfoParsed | null {
  const m = text.match(INFO_RE);
  if (!m) return null;
  return {
    familyId: parseInt(m[1], 16),
    variantId: parseInt(m[2], 16),
    versionMajor: parseInt(m[3], 10),
    versionMinor: parseInt(m[4], 10),
    build: parseInt(m[5], 10),
    matrixX: parseInt(m[6], 10),
    matrixY: parseInt(m[7], 10),
    numObjects: parseInt(m[8], 10),
    rawLine: m[0]
  };
}

export function extractChipInfoFromText(text: string): ChipInfoParsed | null {
  for (const line of text.split(/\r\n|\n|\r/)) {
    const info = parseInfoBlockLine(line);
    if (info) return info;
  }
  return parseInfoBlockLine(text);
}

export function formatChipInfo(info: ChipInfoParsed): string {
  return [
    `Family ID: 0x${info.familyId.toString(16).toUpperCase()}`,
    `Variant ID: 0x${info.variantId.toString(16).toUpperCase()}`,
    `Version: ${info.versionMajor}.${info.versionMinor}`,
    `Build: 0x${info.build.toString(16).toUpperCase()}`,
    `Matrix: ${info.matrixX}×${info.matrixY}`,
    `Objects: ${info.numObjects}`
  ].join(', ');
}

export async function mcuEncStopSpiIfNeeded(session: McuStringSession): Promise<void> {
  await session.sendLine('SPISTOP', 3000);
  await session.waitText(['INFO: SPI stream STOP'], 2000).catch(() => '');
  await session.waitRxIdle?.(80, 800);
}

export async function mcuEncStopDiagnosticOutput(session: McuStringSession): Promise<void> {
  await session.sendLine('STOP', 3000);
  await session.waitText(['OK: Diagnostic output stopped'], 2000).catch(() => '');
  await session.waitRxIdle?.(80, 800);
}

export async function ensureMcuStringMode(session: McuStringSession): Promise<void> {
  session.drainRx?.();
  await mcuEncStopSpiIfNeeded(session);
  await mcuEncStopDiagnosticOutput(session);
  session.drainRx?.();
  await session.sendLine(MODE1_TEXT.trimEnd(), 3000);
  await session.waitText(['OK: Switched to string mode'], 3000).catch(() => '');
  await session.waitRxIdle?.(120, 1200);
}

export async function mcuReadChipInfo(session: McuStringSession): Promise<ChipInfoParsed | null> {
  try {
    await session.waitRxIdle?.(80, 500);
    session.trimRxAfter?.(['Info Block:', 'ERR: Read Info Block', 'OK: Switched to string mode', 'INFO: SPI stream STOP', 'OK: Diagnostic output stopped']);
    await session.sendLine('INFO', 5000);
    const text = await session.waitText(['Info Block:', 'ERR: Read Info Block'], 10000);
    const matchedLine = extractMatchingLine(text, ['Info Block:', 'ERR: Read Info Block']);
    if (matchedLine) session.flushBridgeLogs?.(matchedLine, 'rx');
    await session.waitRxIdle?.(80, 800);
    return extractChipInfoFromText(text);
  } catch {
    return null;
  }
}

/**
 * enc.txt 阶段 F：末帧复位后 MCU 仍可能在二进制桥模式。
 * 不可先发 SPISTOP（会被当二进制包）；须先 02 01 10 20 或等 ENC END 清理后再切 mode1。
 */
export async function mcuExitEncBinaryBridge(session: McuStringSession): Promise<void> {
  const ioTimeout = ENC_POST_STRING_IO_TIMEOUT_MS;
  session.drainRx?.();
  await session.waitRxIdle?.(120, 1200).catch(() => {});

  for (let i = 0; i < 4; i++) {
    try {
      await session.sendRaw(MODE_SWITCH_TO_STRING_BYTES, ioTimeout);
      await session.waitRxIdle?.(150, 2500);
      break;
    } catch {
      await new Promise((r) => setTimeout(r, 400));
    }
  }

  try {
    await session.sendLine(MODE1_TEXT.trimEnd(), ioTimeout);
    await session.waitText(['OK: Switched to string mode'], 8000).catch(() => '');
    await session.waitRxIdle?.(120, 1500);
  } catch {
    /* 已在字符串模式时可能无 OK 文本 */
  }
}

/** enc.txt 阶段 F：0x27 NAK → 扫 0x4B ACK → 读 Info Block */
export async function mcuVerifyAppAfterEncBurn(
  session: McuStringSession,
  maxWaitMs = 45000
): Promise<ChipInfoParsed | null> {
  const deadline = Date.now() + maxWaitMs;
  let attempt = 0;
  while (Date.now() < deadline) {
    if (attempt > 0) await new Promise((r) => setTimeout(r, 1000));
    attempt += 1;
    session.drainRx?.();
    try {
      await mcuExitEncBinaryBridge(session);
    } catch {
      /* USB 繁忙时忽略，下一轮重试 */
    }
    try {
      await mcuEncStopDiagnosticOutput(session);
    } catch {
      /* STOP 非必须 */
    }
    const app = await mcuFindIicAddress(session);
    if (app == null) continue;
    const info = await mcuReadChipInfo(session);
    if (info) return info;
  }
  return null;
}

export async function mcuFindIicAddress(session: McuStringSession): Promise<number | null> {
  const lineTimeout = ENC_POST_STRING_IO_TIMEOUT_MS;
  for (let attempt = 0; attempt < 4; attempt++) {
    if (attempt > 0) await new Promise((r) => setTimeout(r, 500));
    await session.waitRxIdle?.(80, 800);
    session.trimRxAfter?.(['OK: FINDIIC', 'ERR: FINDIIC', 'Info Block:', 'ERROR: Unknown command']);
    try {
      await session.sendLine('FINDIIC', lineTimeout);
    } catch {
      continue;
    }
    try {
      const text = await session.waitText(['OK: FINDIIC', 'ERR: FINDIIC', 'ERROR: Unknown command'], 15000);
      const matchedLine = extractMatchingLine(text, ['OK: FINDIIC', 'ERR: FINDIIC', 'ERROR: Unknown command']);
      if (matchedLine) session.flushBridgeLogs?.(matchedLine, 'rx');
      const m = text.match(/FINDIIC addr=0x([0-9A-Fa-f]{2})/);
      if (m) return parseInt(m[1], 16);
      if (text.includes('ERR: FINDIIC') || text.includes('ERROR: Unknown command')) return null;
    } catch {
      /* retry */
    }
  }
  return null;
}

export type EncEnterBootloaderResult =
  | { ok: true; app: number; bootloader: number; blStatus: number }
  | { ok: false; reason: string; phase?: 'reset' | 'findbl' | 'enterbl' };

function explainEncMcuStatus(status: number, context: 'reset' | 'findbl' | 'enterbl'): string {
  switch (status) {
    case 0x81:
      if (context === 'reset') {
        return '未找到应用模式地址或 T6 对象（0x81）';
      }
      return 'Bootloader 地址扫描超时，0x24~0x27 均无有效 BL 状态（0x81）';
    case 0x01:
      if (context === 'reset') {
        return 'T6 写 RESET=0xA5 失败，I2C NACK（0x01）';
      }
      return 'Bootloader 状态读失败，I2C NACK（0x01）';
    case 0x83:
      return 'T6 已写 0xA5 但芯片仍在应用模式 0x4B（0x83，未进 Bootloader）';
    default:
      return `MCU 状态码 0x${status.toString(16).toUpperCase()}`;
  }
}

/** @deprecated 使用 explainEncMcuStatus */
export function explainEncEnterBlMcuStatus(status: number): string {
  return explainEncMcuStatus(status, 'enterbl');
}

async function waitMcuCommandLine(
  session: McuStringSession,
  cmd: string,
  okHints: string[],
  errHints: string[],
  timeoutMs: number
): Promise<{ ok: boolean; text: string; matchedLine: string | null }> {
  await session.waitRxIdle?.(150, 1500);
  session.trimRxAfter?.([...okHints, ...errHints, 'ERROR: Unknown command']);
  await session.sendLine(cmd, 20000);
  const hints = [...okHints, ...errHints, 'ERROR: Unknown command'];
  const text = await session.waitText(hints, timeoutMs);
  const matchedLine = extractMatchingLine(text, hints);
  if (matchedLine) session.flushBridgeLogs?.(matchedLine, 'rx');
  await session.waitRxIdle?.(120, 2000);
  if (text.includes('ERROR: Unknown command')) {
    return { ok: false, text, matchedLine };
  }
  const ok = okHints.some((h) => text.includes(h));
  return { ok, text, matchedLine };
}

/** 阶段 B+C：T6 写 0xA5 并在 MCU 内轮询 Bootloader（enc.txt 231~243 行） */
export async function mcuEncResetBootloader(session: McuStringSession): Promise<
  | { ok: true; app: number; t6: number; bootloader?: number; blStatus?: number }
  | { ok: false; reason: string }
> {
  const { ok, text } = await waitMcuCommandLine(
    session,
    'ENCRESETBL',
    ['OK: ENCRESETBL'],
    ['ERR: ENCRESETBL'],
    45000
  );
  if (text.includes('ERROR: Unknown command')) {
    return { ok: false, reason: 'MCU 不识别 ENCRESETBL（请重新烧录 STM32 固件）' };
  }
  if (ok) {
    const full = text.match(
      /app=0x([0-9A-Fa-f]{2}).*t6=0x([0-9A-Fa-f]{4}).*bl=0x([0-9A-Fa-f]{2}).*bl_status=0x([0-9A-Fa-f]{2})/i
    );
    if (full) {
      return {
        ok: true,
        app: parseInt(full[1], 16),
        t6: parseInt(full[2], 16),
        bootloader: parseInt(full[3], 16),
        blStatus: parseInt(full[4], 16)
      };
    }
    const m = text.match(/app=0x([0-9A-Fa-f]{2}).*t6=0x([0-9A-Fa-f]{4})/i);
    if (m) {
      return { ok: true, app: parseInt(m[1], 16), t6: parseInt(m[2], 16) };
    }
    return { ok: true, app: 0, t6: 0 };
  }
  const errM = text.match(/ERR: ENCRESETBL failed status=0x([0-9A-Fa-f]{2})/i);
  const status = errM ? parseInt(errM[1], 16) : 0xff;
  return { ok: false, reason: explainEncMcuStatus(status, 'reset') };
}

/** 阶段 C：扫描 Bootloader 地址并读 E0 状态（enc.txt 第 234~243 行） */
export async function mcuEncFindBootloader(
  session: McuStringSession,
  blHint: number
): Promise<
  | { ok: true; bootloader: number; blStatus: number }
  | { ok: false; reason: string }
> {
  const hintCandidates: number[] = [];
  const pushHint = (h: number) => {
    if (h > 0 && !hintCandidates.includes(h)) hintCandidates.push(h);
  };
  pushHint(blHint);
  pushHint(0x27);
  pushHint(0x25);
  pushHint(0x26);
  pushHint(0x24);
  hintCandidates.push(0);

  let lastReason = 'MCU 无应答（45s 超时）';
  for (let attempt = 0; attempt < hintCandidates.length; attempt++) {
    if (attempt > 0) await new Promise((r) => setTimeout(r, 1500));
    const hint = hintCandidates[attempt];
    const cmd = hint ? `FINDBL ${hint.toString(16)}` : 'FINDBL';
    const { ok, text } = await waitMcuCommandLine(
      session,
      cmd,
      ['OK: FINDBL'],
      ['ERR: FINDBL'],
      45000
    );
    if (text.includes('ERROR: Unknown command')) {
      return { ok: false, reason: 'MCU 不识别 FINDBL（请重新烧录 STM32 固件）' };
    }
    const m = text.match(/bootloader=0x([0-9A-Fa-f]{2}).*bl_status=0x([0-9A-Fa-f]{2})/i);
    if (ok && m) {
      return {
        ok: true,
        bootloader: parseInt(m[1], 16),
        blStatus: parseInt(m[2], 16)
      };
    }
    const errM = text.match(/ERR: FINDBL failed status=0x([0-9A-Fa-f]{2})/i);
    if (errM) {
      const status = parseInt(errM[1], 16);
      lastReason = explainEncMcuStatus(status, 'findbl');
      if (status !== 0x81) break;
      continue;
    }
    lastReason = `MCU 应答无法解析: ${text.trim().slice(0, 120)}`;
  }
  return { ok: false, reason: lastReason };
}

export async function mcuEncEnterBootloader(
  session: McuStringSession,
  blHint: number
): Promise<EncEnterBootloaderResult> {
  const reset = await mcuEncResetBootloader(session);
  if (!reset.ok) {
    return { ok: false, reason: reset.reason, phase: 'reset' };
  }

  const found = await mcuEncFindBootloader(session, blHint);
  if (!found.ok) {
    return { ok: false, reason: found.reason, phase: 'findbl' };
  }

  return {
    ok: true,
    app: reset.app,
    bootloader: found.bootloader,
    blStatus: found.blStatus
  };
}

export async function mcuEncUnlock(session: McuStringSession): Promise<number | null> {
  await session.waitRxIdle?.(80, 500);
  session.trimRxAfter?.(['OK: ENCUNLOCK', 'ERR: ENCUNLOCK']);
  await session.sendLine('ENCUNLOCK', 10000);
  const text = await session.waitText(['OK: ENCUNLOCK', 'ERR: ENCUNLOCK'], 20000);
  const matchedLine = extractMatchingLine(text, ['OK: ENCUNLOCK', 'ERR: ENCUNLOCK']);
  if (matchedLine) session.flushBridgeLogs?.(matchedLine, 'rx');
  const m = text.match(/bl_status=0x([0-9A-Fa-f]{2})/);
  return m ? parseInt(m[1], 16) : null;
}

export async function mcuSwitchBinaryBridge(session: McuStringSession): Promise<boolean> {
  await session.waitRxIdle?.(120, 1500);
  session.drainRx?.();
  const { ok, text } = await waitMcuCommandLine(
    session,
    'BRIDGEBIN',
    ['OK: Switched to I2C-USB bridge mode'],
    ['ERROR: Unknown command'],
    10000
  );
  if (text.includes('ERROR: Unknown command')) {
    return false;
  }
  return ok || text.includes('OK: Switched to I2C-USB bridge mode');
}
