/**
 * 在 Windows 上通过 MSYS2 MINGW64 编译 mxt-app（本地编译，非 Linux 交叉编译）
 */
const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

const MXT_APP_ROOT = path.join(__dirname, '..', '..', 'linux_driver_mxt_sys', 'mxt-app');

function findMsys2() {
  const roots = [
    process.env.MSYS2_ROOT,
    'C:\\msys64',
    'D:\\msys64',
    'E:\\msys64'
  ].filter(Boolean);

  for (const root of roots) {
    const bash = path.join(root, 'usr', 'bin', 'bash.exe');
    if (fs.existsSync(bash)) return { root, bash };
  }
  return null;
}

function toUnixPath(winPath, msysRoot) {
  const cygpath = path.join(msysRoot, 'usr', 'bin', 'cygpath.exe');
  if (fs.existsSync(cygpath)) {
    const r = spawnSync(cygpath, ['-u', winPath], { encoding: 'utf8' });
    if (r.status === 0 && r.stdout.trim()) return r.stdout.trim();
  }
  return winPath.replace(/\\/g, '/').replace(/^([A-Za-z]):/, (_, d) => `/${d.toLowerCase()}`);
}

function buildMxtAppWithMsys2(options = {}) {
  if (process.platform !== 'win32') {
    return {
      ok: false,
      error: 'build-mxt-app.js 仅用于 Windows 本地编译；Linux 请使用 mxt-app/build-w64-mingw.sh'
    };
  }

  const msys = findMsys2();
  if (!msys) {
    return {
      ok: false,
      error: [
        '未找到 MSYS2（需要 MINGW64 工具链）。',
        '1. 安装 MSYS2: https://www.msys2.org/',
        '2. 在 MSYS2 MINGW64 终端执行:',
        '   pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-libusb autoconf automake libtool make',
        '3. 运行: npm run build:mxt-app'
      ].join('\n')
    };
  }

  const buildScript = path.join(MXT_APP_ROOT, 'build-win64-msys2.sh');
  if (!fs.existsSync(buildScript)) {
    return { ok: false, error: `缺少编译脚本: ${buildScript}` };
  }

  const unixDir = toUnixPath(MXT_APP_ROOT, msys.root);
  const command = `cd '${unixDir}' && bash ./build-win64-msys2.sh`;

  if (!options.quiet) {
    console.log('[build-mxt-app] MSYS2:', msys.root);
    console.log('[build-mxt-app] 编译目录:', MXT_APP_ROOT);
    console.log('[build-mxt-app] 使用 MINGW64 + --host=x86_64-w64-mingw32');
  }

  const result = spawnSync(msys.bash, ['-lc', command], {
    stdio: options.quiet ? 'pipe' : 'inherit',
    encoding: 'utf8',
    windowsHide: true,
    env: {
      ...process.env,
      MSYSTEM: 'MINGW64',
      MSYS2_PATH_TYPE: 'inherit',
      CHOST: 'x86_64-w64-mingw32'
    }
  });

  if (result.status !== 0) {
    return {
      ok: false,
      error: (result.stderr || result.stdout || 'MSYS2 make 失败').trim()
    };
  }

  return { ok: true, mxtAppRoot: MXT_APP_ROOT };
}

module.exports = { buildMxtAppWithMsys2, findMsys2, MXT_APP_ROOT };

if (require.main === module) {
  const r = buildMxtAppWithMsys2();
  if (!r.ok) {
    console.error('[build-mxt-app]', r.error);
    process.exit(1);
  }
  console.log('[build-mxt-app] 编译完成');
}
