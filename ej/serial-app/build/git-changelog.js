/**
 * 从 git 收集自上次打包以来的变更摘要，供 UPGRADE_NOTES 使用。
 */
const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..');
const REPO_ROOT = path.join(ROOT, '..', '..');
const RELEASE_HISTORY = path.join(__dirname, 'RELEASE_HISTORY.txt');

const PLACEHOLDER_NOTE =
  '（打包前可在此填写补充说明；Git 变更摘要会在打包时自动生成）';
const LEGACY_PLACEHOLDER =
  '（打包前请在此填写本版本更新说明，留空则仅显示版本号与打包内容清单）';

/** 参与摘要的路径前缀（相对仓库根目录，正斜杠） */
const TRACKED_PREFIXES = [
  'ej/serial-app/',
  'ej/linux_driver_mxt_sys/',
  'USB_DEVICE/',
  'Core/',
  'MDK-ARM/',
  'ej/doc/',
];

const AREA_LABELS = [
  { prefix: 'ej/serial-app/', label: '上位机 Serial Terminal' },
  { prefix: 'ej/linux_driver_mxt_sys/', label: 'mxt-app / xcfg-viewer' },
  { prefix: 'USB_DEVICE/', label: 'STM32 USB 固件' },
  { prefix: 'Core/', label: 'STM32 Core' },
  { prefix: 'MDK-ARM/', label: 'Keil 工程' },
  { prefix: 'ej/doc/', label: '项目文档' },
];

function normalizeRepoPath(filePath) {
  return filePath.replace(/\\/g, '/');
}

function isGitRepo(dir) {
  try {
    execFileSync('git', ['rev-parse', '--git-dir'], {
      cwd: dir,
      encoding: 'utf-8',
      stdio: ['ignore', 'pipe', 'pipe'],
    });
    return true;
  } catch (_) {
    return false;
  }
}

function execGit(args, options = {}) {
  const cwd = options.cwd || REPO_ROOT;
  return execFileSync('git', args, {
    cwd,
    encoding: 'utf-8',
    stdio: ['ignore', 'pipe', 'pipe'],
    maxBuffer: 8 * 1024 * 1024,
  }).trim();
}

function getHeadCommit() {
  try {
    return execGit(['rev-parse', 'HEAD']);
  } catch (_) {
    return '';
  }
}

function isValidCommit(ref) {
  if (!ref) return false;
  try {
    execGit(['cat-file', '-e', `${ref}^{commit}`]);
    return true;
  } catch (_) {
    return false;
  }
}

function parseReleaseHistorySince(version) {
  try {
    const text = fs.readFileSync(RELEASE_HISTORY, 'utf-8');
    const re = new RegExp(`--- ${version.replace(/\./g, '\\.')} @ ([0-9-: ]+) ---`, 'g');
    let match;
    let lastTs = '';
    while ((match = re.exec(text)) !== null) {
      lastTs = match[1];
    }
    return lastTs.trim();
  } catch (_) {
    return '';
  }
}

function resolveBaseRef(options = {}) {
  const { lastPackCommit, sinceVersion } = options;

  if (isValidCommit(lastPackCommit)) {
    return { ref: lastPackCommit, label: `commit ${lastPackCommit.slice(0, 7)}` };
  }

  if (sinceVersion) {
    const sinceTs = parseReleaseHistorySince(sinceVersion);
    if (sinceTs) {
      return { ref: null, since: sinceTs, label: `v${sinceVersion} 打包时间 (${sinceTs})` };
    }
  }

  return { ref: null, label: '最近 15 条相关提交' };
}

function buildLogArgs(base) {
  const args = ['log', '--no-merges', '--pretty=format:%h|%s'];
  if (base.ref) {
    args.push(`${base.ref}..HEAD`);
  } else if (base.since) {
    args.push(`--since=${base.since}`);
  } else {
    args.push('-15');
  }
  args.push('--', ...TRACKED_PREFIXES.map((p) => p.slice(0, -1)));
  return args;
}

function buildDiffArgs(base) {
  const args = ['diff', '--name-only'];
  if (base.ref) {
    args.push(`${base.ref}..HEAD`);
  } else {
    args.push('HEAD~15..HEAD');
  }
  args.push('--', ...TRACKED_PREFIXES.map((p) => p.slice(0, -1)));
  return args;
}

function buildNameOnlyLogArgs(base) {
  const args = ['log', '--name-only', '--pretty=format:'];
  if (base.ref) {
    args.push(`${base.ref}..HEAD`);
  } else if (base.since) {
    args.push(`--since=${base.since}`);
  } else {
    args.push('-15');
  }
  args.push('--', ...TRACKED_PREFIXES.map((p) => p.slice(0, -1)));
  return args;
}

function classifyFile(filePath) {
  const p = normalizeRepoPath(filePath);
  for (const { prefix, label } of AREA_LABELS) {
    if (p.startsWith(prefix)) return label;
  }
  return '其他';
}

function summarizeChangedFiles(base) {
  let raw = '';
  try {
    if (base.since && !base.ref) {
      raw = execGit(buildNameOnlyLogArgs(base));
    } else {
      raw = execGit(buildDiffArgs(base));
    }
  } catch (_) {
    return [];
  }
  if (!raw) return [];

  const seen = new Set();
  const counts = new Map();
  for (const line of raw.split('\n')) {
    const file = line.trim();
    if (!file || seen.has(file)) continue;
    seen.add(file);
    const area = classifyFile(file);
    counts.set(area, (counts.get(area) || 0) + 1);
  }

  return [...counts.entries()]
    .sort((a, b) => b[1] - a[1])
    .map(([area, count]) => `${area}（${count} 个文件）`);
}

function summarizeCommits(base, maxItems = 12) {
  let raw = '';
  try {
    raw = execGit(buildLogArgs(base));
  } catch (_) {
    return [];
  }
  if (!raw) return [];

  const lines = [];
  for (const row of raw.split('\n')) {
    const pipe = row.indexOf('|');
    if (pipe <= 0) continue;
    const hash = row.slice(0, pipe);
    const subject = row.slice(pipe + 1).trim();
    if (!subject) continue;
    lines.push(`- [${hash}] ${subject}`);
    if (lines.length >= maxItems) break;
  }
  return lines;
}

function stripPlaceholder(notes) {
  if (!notes) return '';
  return notes
    .split('\n')
    .filter((line) => {
      const t = line.trim();
      return t !== PLACEHOLDER_NOTE && t !== LEGACY_PLACEHOLDER;
    })
    .join('\n')
    .trim();
}

/**
 * @param {object} options
 * @param {string} [options.lastPackCommit]
 * @param {string} [options.sinceVersion] 上一发布版本号，如 1.0.48
 * @param {string} [options.currentVersion] 当前打包版本
 */
function summarizeGitChanges(options = {}) {
  if (!isGitRepo(REPO_ROOT)) {
    return { lines: ['（当前目录不在 git 仓库内，无法生成变更摘要）'], baseLabel: '' };
  }

  let base = resolveBaseRef(options);
  let commits = summarizeCommits(base);
  let files = summarizeChangedFiles(base);

  if (!commits.length && (base.ref || base.since)) {
    const priorLabel = base.label;
    base = { ref: null, label: `${priorLabel} 以来无新提交，展示近期相关提交` };
    commits = summarizeCommits(base);
    files = summarizeChangedFiles(base);
  }

  const lines = [];
  lines.push(`（${base.label}）`);

  if (commits.length) {
    lines.push('');
    lines.push('提交记录:');
    lines.push(...commits);
  } else {
    lines.push('');
    lines.push('- 无新的 git 提交（或变更不在跟踪目录内）');
  }

  if (files.length) {
    lines.push('');
    lines.push('变更范围:');
    for (const item of files) {
      lines.push(`- ${item}`);
    }
  }

  return { lines, baseLabel: base.label, commitCount: commits.length };
}

function formatUpgradeNotesSection(userNotes, gitSummary, currentVersion) {
  const manual = stripPlaceholder(userNotes);
  const parts = [];

  if (manual) {
    parts.push(manual);
    parts.push('');
  }

  parts.push(`【Git 变更摘要 · v${currentVersion}】`);
  parts.push(...gitSummary.lines);

  return parts.join('\r\n').trim();
}

function recordPackCommit(versionStatePath) {
  const head = getHeadCommit();
  if (!head || !versionStatePath) return head;

  try {
    const state = JSON.parse(fs.readFileSync(versionStatePath, 'utf-8'));
    state.lastPackCommit = head;
    state.lastPackVersion = state.major != null
      ? `${state.major}.${state.minor}.${state.build}`
      : undefined;
    fs.writeFileSync(versionStatePath, JSON.stringify(state, null, 2) + '\n', 'utf-8');
    console.log(`[git-changelog] 已记录打包基准 commit: ${head.slice(0, 7)}`);
  } catch (err) {
    console.warn('[git-changelog] 无法写入 lastPackCommit:', err.message);
  }
  return head;
}

module.exports = {
  PLACEHOLDER_NOTE,
  summarizeGitChanges,
  formatUpgradeNotesSection,
  stripPlaceholder,
  recordPackCommit,
  getHeadCommit,
};
