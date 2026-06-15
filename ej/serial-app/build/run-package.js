/**
 * 统一打包入口：prepare-package → 编译 → electron-builder
 *
 * 用法:
 *   node build/run-package.js --win --nsis --compression=store --buildMode=user
 *   node build/run-package.js --win --portable --compression=store --buildMode=user
 */
const { spawnSync } = require('child_process');
const path = require('path');
const { preparePackage } = require('./prepare-package');

const ROOT = path.join(__dirname, '..');

function parseArgs(argv) {
  const opts = {
    platform: 'win',
    target: 'nsis',
    compression: 'store',
    buildMode: 'user',
    offline: true,
    noBump: false
  };
  for (const arg of argv) {
    if (arg === '--win') opts.platform = 'win';
    else if (arg === '--nsis') opts.target = 'nsis';
    else if (arg === '--portable') opts.target = 'portable';
    else if (arg === '--dir') opts.target = 'dir';
    else if (arg.startsWith('--compression=')) opts.compression = arg.split('=')[1] || 'store';
    else if (arg.startsWith('--buildMode=')) opts.buildMode = arg.split('=')[1] || 'user';
    else if (arg === '--online') opts.offline = false;
    else if (arg === '--no-bump') opts.noBump = true;
  }
  return opts;
}

function run(cmd, args, env = {}) {
  const r = spawnSync(cmd, args, {
    cwd: ROOT,
    stdio: 'inherit',
    shell: true,
    env: { ...process.env, ...env }
  });
  if (r.status !== 0) process.exit(r.status || 1);
}

function main() {
  const opts = parseArgs(process.argv.slice(2));

  console.log('[run-package] 打包选项:', opts);

  const { version } = preparePackage({ noBump: opts.noBump });
  console.log(`[run-package] 开始打包 v${version}`);

  run('npm', ['run', 'build:main']);
  run('npm', ['run', 'build:renderer']);
  run('node', ['build/generate-runtime-window.js']);

  const ebArgs = [
    '--win', opts.target,
    `--config.compression=${opts.compression}`,
    `--config.extraMetadata.buildMode=${opts.buildMode}`
  ];

  const env = opts.offline ? { ELECTRON_BUILDER_OFFLINE: 'true' } : {};
  run('npx', ['electron-builder', ...ebArgs], env);

  console.log(`[run-package] 完成 v${version}`);
  console.log(`[run-package] 升级说明: release/UPGRADE_NOTES-${version}.txt（若 after-pack 已生成）`);
}

main();
