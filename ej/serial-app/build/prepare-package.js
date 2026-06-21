/**
 * 打包前准备：版本号递增、同步 xcfg 资源、生成升级说明
 */
const fs = require('fs');
const path = require('path');
const { syncXcfgViewerResources } = require('./sync-xcfg-viewer-resources');

const ROOT = path.join(__dirname, '..');
const VERSION_STATE = path.join(__dirname, 'version-state.json');
const UPGRADE_NOTES_SRC = path.join(__dirname, 'UPGRADE_NOTES.txt');
const UPGRADE_NOTES_DEST = path.join(ROOT, 'resources', 'UPGRADE_NOTES.txt');
const RELEASE_HISTORY = path.join(__dirname, 'RELEASE_HISTORY.txt');
const PACKAGE_JSON = path.join(ROOT, 'package.json');

function readJson(filePath, fallback) {
  try {
    return JSON.parse(fs.readFileSync(filePath, 'utf-8'));
  } catch (_) {
    return fallback;
  }
}

function writeJson(filePath, data) {
  fs.writeFileSync(filePath, JSON.stringify(data, null, 2) + '\n', 'utf-8');
}

function bumpVersion(options = {}) {
  const noBump = Boolean(options.noBump);
  const state = readJson(VERSION_STATE, { major: 1, minor: 0, build: 0 });
  const pkg = readJson(PACKAGE_JSON, { version: '1.0.0' });

  if (!noBump) {
    state.build = (Number(state.build) || 0) + 1;
    writeJson(VERSION_STATE, state);
  }

  const version = `${state.major}.${state.minor}.${state.build}`;
  pkg.version = version;
  writeJson(PACKAGE_JSON, pkg);

  console.log(`[prepare-package] 版本号: ${version}${noBump ? ' (未递增)' : ' (build+1)'}`);
  return { version, state };
}

function listPackageContents(xcfgSync) {
  const lines = [
    '- Serial Terminal 主程序',
    '- mxt-app CLI（WinUSB / 虚拟串口）',
    '- xcfg 配置查看器',
    `- xcfg 字段说明元数据（xcfg_viewer_metadata.json）`,
  ];
  if (xcfgSync.imageCount > 0) {
    lines.push(`- xcfg 说明配图（${xcfgSync.imageCount} 个文件）`);
  } else {
    lines.push('- xcfg 说明配图（无或源目录为空）');
  }
  lines.push('- libusb-1.0.dll、libgcc_s_seh-1.dll、libstdc++-6.dll、libwinpthread-1.dll（mxt-app 运行时）');
  return lines;
}

function generateUpgradeNotes(version, xcfgSync) {
  const now = new Date();
  const ts = now.toISOString().replace('T', ' ').slice(0, 19);
  let userNotes = '';
  try {
    userNotes = fs.readFileSync(UPGRADE_NOTES_SRC, 'utf-8').trim();
  } catch (_) {}

  const contents = listPackageContents(xcfgSync);
  const parts = [
    '========================================',
    `Serial Terminal 升级说明`,
    `版本: ${version}`,
    `打包时间: ${ts}`,
    '========================================',
    '',
    '【本包包含】',
    ...contents.map((l) => l),
    '',
    '【更新说明】',
    userNotes || '（未填写 build/UPGRADE_NOTES.txt）',
    '',
    '【xcfg 资源来源】',
    `  ${path.join('..', 'linux_driver_mxt_sys', 'xcfg-viewer')}`,
    '',
    '安装后路径:',
    '  程序目录\\CLI\\mxt-app.exe（与 Serial Terminal.exe 同级，优先使用）',
    '  程序目录\\resources\\CLI\\（备用）',
    '  程序目录\\resources\\UPGRADE_NOTES.txt',
    '  程序目录\\resources\\xcfg-viewer\\xcfg_viewer_metadata.json',
    '  程序目录\\resources\\xcfg-viewer\\metadata_images\\',
    '  用户数据（首次运行复制）: %APPDATA%\\serial-terminal\\xcfg-viewer\\',
    ''
  ];

  const text = parts.join('\r\n');
  fs.mkdirSync(path.dirname(UPGRADE_NOTES_DEST), { recursive: true });
  fs.writeFileSync(UPGRADE_NOTES_DEST, text, 'utf-8');

  const historyBlock = [
    `--- ${version} @ ${ts} ---`,
    userNotes || '（无补充说明）',
    '包含: ' + contents.join('; '),
    ''
  ].join('\n');
  fs.appendFileSync(RELEASE_HISTORY, historyBlock, 'utf-8');

  console.log('[prepare-package] 升级说明:', UPGRADE_NOTES_DEST);
  return text;
}

function preparePackage(options = {}) {
  const { version } = bumpVersion(options);
  const xcfgSync = syncXcfgViewerResources();
  generateUpgradeNotes(version, xcfgSync);
  return { version, xcfgSync };
}

module.exports = { preparePackage, bumpVersion, generateUpgradeNotes };

if (require.main === module) {
  const noBump = process.argv.includes('--no-bump');
  preparePackage({ noBump });
}
