/**
 * 从 xcfg-viewer 源目录同步元数据与配图到 serial-app/resources/xcfg-viewer/
 * 打包时经 extraResources 安装到 resources/xcfg-viewer/
 */
const fs = require('fs');
const path = require('path');

const ROOT = path.join(__dirname, '..');
const XCFG_SOURCE = path.join(ROOT, '..', 'linux_driver_mxt_sys', 'xcfg-viewer');
const DEST = path.join(ROOT, 'resources', 'xcfg-viewer');

function copyFileEnsureDir(src, dest) {
  fs.mkdirSync(path.dirname(dest), { recursive: true });
  fs.copyFileSync(src, dest);
}

function copyDirRecursive(srcDir, destDir) {
  if (!fs.existsSync(srcDir)) return { copied: 0, skipped: true };
  fs.mkdirSync(destDir, { recursive: true });
  let copied = 0;
  for (const name of fs.readdirSync(srcDir)) {
    const src = path.join(srcDir, name);
    const dest = path.join(destDir, name);
    const st = fs.statSync(src);
    if (st.isDirectory()) {
      copied += copyDirRecursive(src, dest).copied;
    } else {
      copyFileEnsureDir(src, dest);
      copied += 1;
    }
  }
  return { copied, skipped: false };
}

function syncXcfgViewerResources() {
  const metaSrc = path.join(XCFG_SOURCE, 'xcfg_viewer_metadata.json');
  const imgSrc = path.join(XCFG_SOURCE, 'metadata_images');
  const metaDest = path.join(DEST, 'xcfg_viewer_metadata.json');
  const imgDest = path.join(DEST, 'metadata_images');
  const cxfgdataSrc = path.join(XCFG_SOURCE, '.cxfgdata');
  const cxfgdataDest = path.join(DEST, '.cxfgdata');

  if (!fs.existsSync(metaSrc)) {
    throw new Error(`缺少元数据源文件: ${metaSrc}`);
  }

  copyFileEnsureDir(metaSrc, metaDest);
  if (fs.existsSync(cxfgdataSrc)) {
    copyFileEnsureDir(cxfgdataSrc, cxfgdataDest);
    console.log('[sync-xcfg] 已同步:', cxfgdataDest);
  }
  const imgResult = copyDirRecursive(imgSrc, imgDest);

  console.log('[sync-xcfg] 已同步:', metaDest);
  if (imgResult.skipped) {
    console.warn('[sync-xcfg] 源目录无 metadata_images，已跳过:', imgSrc);
  } else {
    console.log(`[sync-xcfg] 已同步 ${imgResult.copied} 个配图 ->`, imgDest);
  }

  return {
    metadata: metaDest,
    imagesDir: imgDest,
    imageCount: imgResult.copied || 0
  };
}

module.exports = { syncXcfgViewerResources };

if (require.main === module) {
  syncXcfgViewerResources();
}
