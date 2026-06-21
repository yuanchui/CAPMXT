/**
 * 打包前将 mxt-app 复制到 serial-app/CLI/
 *
 * 查找顺序:
 * 1. MXT_APP_BUILD_DIR / mxt-app 构建产物（优先 .libs 完整版）
 * 2. 已存在于 CLI/mxt-app.exe（仅当无更新构建产物时）
 *
 * Windows 本地编译: npm run build:mxt-app  （MSYS2，见 build-win64-msys2.bat）
 * Linux 交叉编译:   ./build-w64-mingw.sh  （仅 Linux/WSL）
 */
const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..');
const CLI_DIR = path.join(ROOT, 'CLI');
const MXT_APP_ROOT = path.join(ROOT, '..', 'linux_driver_mxt_sys', 'mxt-app');

function fileExists(p) {
  try {
    return Boolean(p && fs.existsSync(p));
  } catch (_) {
    return false;
  }
}

const MIN_MXT_APP_BYTES = 100000;

function mxtAppSize(p) {
  try {
    return fs.statSync(p).size;
  } catch (_) {
    return 0;
  }
}

function isUsableMxtAppExe(p) {
  return fileExists(p) && mxtAppSize(p) >= MIN_MXT_APP_BYTES;
}

function findMxtAppExe() {
  const envDir = (process.env.MXT_APP_BUILD_DIR || '').trim();
  if (envDir) {
    const p = path.join(path.resolve(envDir), 'mxt-app.exe');
    if (isUsableMxtAppExe(p)) return { exe: p, dir: path.dirname(p), source: envDir };
  }

  const candidates = [
    path.join(MXT_APP_ROOT, '.libs', 'mxt-app.exe'),
    path.join(MXT_APP_ROOT, 'build-win64', 'mxt-app.exe'),
    path.join(MXT_APP_ROOT, 'mxt-app.exe'),
    path.join(MXT_APP_ROOT, 'src', 'mxt-app', 'mxt-app.exe')
  ];

  for (const p of candidates) {
    if (isUsableMxtAppExe(p)) {
      return { exe: p, dir: path.dirname(p), source: p };
    }
  }

  const staged = path.join(CLI_DIR, 'mxt-app.exe');
  if (fileExists(staged)) {
    return { exe: staged, dir: CLI_DIR, source: 'CLI (已存在)' };
  }

  return null;
}

const MINGW_RUNTIME_DLLS = [
  'libgcc_s_seh-1.dll',
  'libstdc++-6.dll',
  'libwinpthread-1.dll'
];

const CLI_BUNDLE_FILES = ['mxt-app.exe', 'libusb-1.0.dll', ...MINGW_RUNTIME_DLLS];

function verifyCliBundle(cliDir, strict) {
  const missing = CLI_BUNDLE_FILES.filter((name) => !fileExists(path.join(cliDir, name)));
  if (missing.length) {
    const msg = `CLI 目录不完整，缺少: ${missing.join(', ')}（目录: ${cliDir}）`;
    if (strict) throw new Error(msg);
    console.warn('[prepare-cli]', msg);
    return false;
  }
  const manifest = CLI_BUNDLE_FILES.map((name) => {
    const p = path.join(cliDir, name);
    return `${name}\t${mxtAppSize(p)}`;
  }).join('\n');
  fs.writeFileSync(path.join(cliDir, 'CLI_BUNDLE.txt'), manifest + '\n', 'utf-8');
  return true;
}

function findMingwBinDir() {
  const candidates = [
    process.env.MINGW_PREFIX ? path.join(process.env.MINGW_PREFIX, 'bin') : null,
    process.env.MSYS2_ROOT ? path.join(process.env.MSYS2_ROOT, 'mingw64', 'bin') : null,
    process.platform === 'win32' ? 'C:/msys64/mingw64/bin' : null,
    process.platform === 'win32' ? 'D:/msys64/mingw64/bin' : null
  ].filter(Boolean);

  for (const dir of candidates) {
    if (fileExists(path.join(dir, 'libgcc_s_seh-1.dll'))) return dir;
  }
  return null;
}

function copyRuntimeDlls(cliDir, strict) {
  const mingwBin = findMingwBinDir();
  if (!mingwBin) {
    const warn = '未找到 MinGW 运行时目录，mxt-app 在部分机器上可能无法启动。';
    if (strict) throw new Error(warn);
    console.warn('[prepare-cli]', warn);
    return;
  }

  for (const name of MINGW_RUNTIME_DLLS) {
    const src = path.join(mingwBin, name);
    const dest = path.join(cliDir, name);
    if (!fileExists(src)) {
      const warn = `缺少 MinGW 运行时: ${src}`;
      if (strict) throw new Error(warn);
      console.warn('[prepare-cli]', warn);
      continue;
    }
    if (!fileExists(dest) || mxtAppSize(dest) !== mxtAppSize(src)) {
      fs.copyFileSync(src, dest);
      console.log('[prepare-cli] copied', name, '->', dest);
    }
  }
}

function findLibusbDll(mxtExeDir) {
  const candidates = [
    path.join(mxtExeDir, 'libusb-1.0.dll'),
    path.join(CLI_DIR, 'libusb-1.0.dll'),
    process.env.LIBUSB_DLL ? path.resolve(process.env.LIBUSB_DLL) : null,
    process.env.MINGW_PREFIX ? path.join(process.env.MINGW_PREFIX, 'bin', 'libusb-1.0.dll') : null,
    process.env.MSYS2_ROOT ? path.join(process.env.MSYS2_ROOT, 'mingw64', 'bin', 'libusb-1.0.dll') : null,
    process.platform === 'win32' ? 'C:/msys64/mingw64/bin/libusb-1.0.dll' : null,
    process.platform === 'win32' ? 'D:/msys64/mingw64/bin/libusb-1.0.dll' : null
  ].filter(Boolean);

  for (const p of candidates) {
    if (fileExists(p)) return p;
  }
  return null;
}

function buildHelpMessage() {
  if (process.platform === 'win32') {
    return [
      'Windows 本地编译（推荐）:',
      '  1. 安装 MSYS2: https://www.msys2.org/',
      '  2. MSYS2 MINGW64 终端: pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-libusb autoconf automake libtool make',
      '  3. cd ej/serial-app && npm run build:mxt-app',
      '  4. npm run package:user:nsis:fast',
      '',
      '或运行: ej/linux_driver_mxt_sys/mxt-app/build-win64-msys2.bat',
      '',
      '若已编译，可设置 MXT_APP_BUILD_DIR 指向含 mxt-app.exe 的目录，',
      '或手动复制到 ej/serial-app/CLI/mxt-app.exe'
    ].join('\n');
  }
  return [
    'Linux/WSL 交叉编译:',
    '  cd ej/linux_driver_mxt_sys/mxt-app',
    '  ./build-w64-mingw.sh',
    '',
    '或设置 MXT_APP_BUILD_DIR 指向 build-win64 目录'
  ].join('\n');
}

function tryAutoBuildMxtApp() {
  if (process.env.SKIP_MXT_APP_BUILD === '1') return { ok: false, skipped: true };
  if (process.platform !== 'win32') return { ok: false, skipped: true };

  try {
    const { buildMxtAppWithMsys2, findMsys2 } = require('./build-mxt-app');
    if (!findMsys2()) return { ok: false, skipped: true, reason: 'no-msys2' };
    console.log('[prepare-cli] 未找到 mxt-app.exe，尝试 MSYS2 本地编译...');
    return buildMxtAppWithMsys2({ quiet: false });
  } catch (e) {
    return { ok: false, error: e?.message || String(e) };
  }
}

function prepareCli(options = {}) {
  const strict = Boolean(options.strict);
  fs.mkdirSync(CLI_DIR, { recursive: true });

  let found = findMxtAppExe();

  if (!found && strict && process.env.SKIP_MXT_APP_BUILD !== '1') {
    const built = tryAutoBuildMxtApp();
    if (built.ok) found = findMxtAppExe();
    else if (built.error) console.warn('[prepare-cli] 自动编译失败:', built.error);
  }

  if (!found) {
    const msg = ['未找到 mxt-app.exe', buildHelpMessage()].join('\n\n');
    if (strict) throw new Error(msg);
    console.warn('[prepare-cli]\n' + msg);
    return { ok: false, warning: msg };
  }

  const destExe = path.join(CLI_DIR, 'mxt-app.exe');
  const shouldCopyExe = path.resolve(found.exe) !== path.resolve(destExe)
    || mxtAppSize(destExe) !== mxtAppSize(found.exe);
  if (shouldCopyExe) {
    fs.copyFileSync(found.exe, destExe);
    console.log('[prepare-cli] copied mxt-app.exe from', found.source || found.exe);
    console.log('[prepare-cli]  ->', destExe, `(${mxtAppSize(destExe)} bytes)`);
  } else {
    console.log('[prepare-cli] 使用已有 CLI/mxt-app.exe');
  }

  const libusb = findLibusbDll(found.dir);
  const destDll = path.join(CLI_DIR, 'libusb-1.0.dll');
  if (libusb && (!fileExists(destDll) || path.resolve(libusb) !== path.resolve(destDll))) {
    fs.copyFileSync(libusb, destDll);
    console.log('[prepare-cli] copied libusb-1.0.dll ->', destDll);
  } else if (!fileExists(destDll)) {
    const warn = [
      '未找到 libusb-1.0.dll，Windows 上 mxt-app 可能无法启动。',
      'MSYS2 安装后通常在 C:\\msys64\\mingw64\\bin\\libusb-1.0.dll',
      '可设置 LIBUSB_DLL 或 MSYS2_ROOT。'
    ].join(' ');
    if (strict) throw new Error(warn);
    console.warn('[prepare-cli]', warn);
  }

  copyRuntimeDlls(CLI_DIR, strict);

  verifyCliBundle(CLI_DIR, strict);

  return { ok: true, cliDir: CLI_DIR, mxtApp: destExe, source: found.source };
}

module.exports = { prepareCli, CLI_DIR, findMxtAppExe, MXT_APP_ROOT, CLI_BUNDLE_FILES, verifyCliBundle };

if (require.main === module) {
  const strict = process.argv.includes('--strict');
  const result = prepareCli({ strict });
  if (!result.ok) process.exit(strict ? 1 : 0);
}
