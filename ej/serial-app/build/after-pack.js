/**
 * 打包后：升级说明 + 校验 mxt-app CLI 是否随安装包完整落地
 */
const fs = require('fs');
const path = require('path');
const { recordPackCommit } = require('./git-changelog');

module.exports = async function afterPack(context) {
  const root = path.join(__dirname, '..');
  const pkg = JSON.parse(fs.readFileSync(path.join(root, 'package.json'), 'utf-8'));
  const version = pkg.version || '0.0.0';
  const src = path.join(root, 'resources', 'UPGRADE_NOTES.txt');
  const outDir = path.join(root, 'release');
  const dest = path.join(outDir, `UPGRADE_NOTES-${version}.txt`);

  if (fs.existsSync(src)) {
    fs.mkdirSync(outDir, { recursive: true });
    fs.copyFileSync(src, dest);
    console.log('[after-pack] 升级说明已输出:', dest);
  } else {
    console.warn('[after-pack] 未找到 resources/UPGRADE_NOTES.txt');
  }

  recordPackCommit(path.join(__dirname, 'version-state.json'));

  const { CLI_BUNDLE_FILES } = require('./prepare-cli');
  const appOutDir = context?.appOutDir;
  if (!appOutDir || !fs.existsSync(appOutDir)) return;

  const cliDirs = [
    path.join(appOutDir, 'CLI'),
    path.join(appOutDir, 'resources', 'CLI')
  ];
  let ok = true;
  for (const cliDir of cliDirs) {
    const missing = CLI_BUNDLE_FILES.filter((name) => !fs.existsSync(path.join(cliDir, name)));
    if (missing.length) {
      console.error(`[after-pack] 校验失败 ${cliDir} 缺少: ${missing.join(', ')}`);
      ok = false;
    } else {
      console.log('[after-pack] CLI 完整:', cliDir);
    }
  }
  if (!ok) {
    throw new Error('打包产物缺少 mxt-app CLI 依赖，请重新运行 npm run prepare:cli:strict 后再打包');
  }
};
