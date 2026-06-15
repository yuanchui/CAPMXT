/**
 * 打包后：将升级说明复制到 release 目录（与安装包同目录）
 */
const fs = require('fs');
const path = require('path');

module.exports = async function afterPack(context) {
  const root = path.join(__dirname, '..');
  const pkg = JSON.parse(fs.readFileSync(path.join(root, 'package.json'), 'utf-8'));
  const version = pkg.version || '0.0.0';
  const src = path.join(root, 'resources', 'UPGRADE_NOTES.txt');
  const outDir = path.join(root, 'release');
  const dest = path.join(outDir, `UPGRADE_NOTES-${version}.txt`);

  if (!fs.existsSync(src)) {
    console.warn('[after-pack] 未找到 resources/UPGRADE_NOTES.txt');
    return;
  }

  fs.mkdirSync(outDir, { recursive: true });
  fs.copyFileSync(src, dest);
  console.log('[after-pack] 升级说明已输出:', dest);
};
