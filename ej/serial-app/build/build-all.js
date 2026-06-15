/**
 * 顺序编译：mxt-app → prepare:cli → serial-app（main + renderer）→ [可选] 打包
 *
 * 用法:
 *   node build/build-all.js
 *   node build/build-all.js --package
 *   node build/build-all.js --skip-mxt
 *   node build/build-all.js --package --package-target=nsis:small
 */
const { spawnSync } = require('child_process');
const path = require('path');

const ROOT = path.join(__dirname, '..');

function parseArgs(argv) {
  const opts = {
    package: false,
    skipMxt: false,
    packageTarget: 'user:nsis:fast',
    strictCli: true
  };
  for (const arg of argv) {
    if (arg === '--package' || arg === '-p') opts.package = true;
    else if (arg === '--skip-mxt' || arg === '--skip-mxt-app') opts.skipMxt = true;
    else if (arg.startsWith('--package-target=')) {
      opts.packageTarget = arg.slice('--package-target='.length).trim() || opts.packageTarget;
    } else if (arg === '--no-strict-cli') opts.strictCli = false;
  }
  return opts;
}

function runStep(label, npmScript) {
  console.log('');
  console.log('========================================');
  console.log(`[build-all] ${label}`);
  console.log(`[build-all] npm run ${npmScript}`);
  console.log('========================================');

  const result = spawnSync('npm', ['run', npmScript], {
    cwd: ROOT,
    stdio: 'inherit',
    shell: true,
    env: process.env
  });

  if (result.status !== 0) {
    console.error(`[build-all] 失败: ${label} (exit ${result.status ?? 1})`);
    process.exit(result.status || 1);
  }
}

function main() {
  const opts = parseArgs(process.argv.slice(2));
  const started = Date.now();

  console.log('[build-all] Serial Terminal 顺序编译');
  console.log('[build-all] 工作目录:', ROOT);
  console.log('[build-all] 选项:', JSON.stringify(opts));

  if (!opts.skipMxt) {
    runStep('1/4 编译 mxt-app (MSYS2 MINGW64)', 'build:mxt-app');
    runStep('2/4 同步 CLI (mxt-app.exe + libusb-1.0.dll)', opts.strictCli ? 'prepare:cli:strict' : 'prepare:cli');
  } else {
    console.log('[build-all] 跳过 mxt-app，仅 prepare:cli');
    runStep('1/3 同步 CLI', opts.strictCli ? 'prepare:cli:strict' : 'prepare:cli');
  }

  const stepBase = opts.skipMxt ? 2 : 3;
  runStep(`${stepBase}/4 编译 serial-app 主进程`, 'build:main');
  runStep(`${stepBase + 1}/4 编译 serial-app 前端`, 'build:renderer');

  if (opts.package) {
    const pkgScript = `package:${opts.packageTarget}`;
    runStep('5/5 打包安装程序', pkgScript);
  }

  const sec = ((Date.now() - started) / 1000).toFixed(1);
  console.log('');
  console.log(`[build-all] 全部完成 (${sec}s)`);
  if (!opts.package) {
    console.log('[build-all] 提示: 打安装包请执行 npm run package:all');
  }
}

main();
