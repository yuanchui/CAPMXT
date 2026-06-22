# electron-builder 打包失败：`Cannot read properties of undefined (reading 'ReadWrite')`

> 适用工程：`ej/serial-app`  
> 涉及版本：`electron-builder@26.15.x`、`pnpm@11.x`  
> 记录日期：2026-06-22

---

## 现象

执行 NSIS 打包时，在 `electron-builder` 下载/解压 Electron 阶段失败：

```text
⨯ Cannot read properties of undefined (reading 'ReadWrite')  failedTask=build
TypeError: Cannot read properties of undefined (reading 'ReadWrite')
    at resolveCacheMode (.../app-builder-lib/out/util/electronGet.js:69:36)
    at buildElectronArtifactConfig (.../app-builder-lib/out/util/electronGet.js:486:31)
    at downloadElectronArtifactZip (.../app-builder-lib/out/util/electronGet.js:583:18)
    at unpack (.../app-builder-lib/out/electron/ElectronFramework.ts:247:54)
```

典型命令：

```powershell
pnpm install
npm run package:user:nsis:fast
```

构建日志中 `@electron/rebuild` 与 native 依赖重建均正常，失败发生在 `packaging platform=win32` 之后。

---

## 根因

`electron-builder@26.15.3` 内部的 `app-builder-lib` 在下载 Electron 及相关工具时，会调用 `@electron/get` 的 `ElectronDownloadCacheMode` 枚举：

```javascript
// app-builder-lib/out/util/electronGet.js
function resolveCacheMode() {
  // ...
  return get_1.ElectronDownloadCacheMode.ReadWrite;
}
```

但 `app-builder-lib` 声明的依赖为 `"@electron/get": "^3.0.0"`，pnpm 实际解析到 **`@electron/get@3.0.0`**。该版本 **不包含** `ElectronDownloadCacheMode` 导出（该枚举自 **4.0.0** 起提供），因此 `ElectronDownloadCacheMode` 为 `undefined`，访问 `.ReadWrite` 时抛出 TypeError。

这是 **electron-builder 与 @electron/get 版本约束过宽** 导致的依赖解析问题，并非项目业务代码错误。

---

## 解决方案

在 `pnpm-workspace.yaml` 中强制将 `@electron/get` 提升到 4.x：

```yaml
allowBuilds:
  '@serialport/bindings-cpp': true
  electron: true
  electron-winstaller: true
  esbuild: true
  usb: true

overrides:
  '@electron/get': ^4.0.0
```

然后重新安装依赖并打包：

```powershell
cd ej\serial-app
pnpm install
npm run package:user:nsis:fast
```

若不想再次递增版本号，可加 `--no-bump`：

```powershell
npm run package:user:nsis:fast -- --no-bump
```

---

## 注意事项（pnpm 11）

**pnpm 11 不再读取 `package.json` 中的 `pnpm.overrides` 字段。**

若把 override 写在 `package.json` 里，pnpm 会提示：

```text
[WARN] The "pnpm" field in package.json is no longer read by pnpm.
The following keys were ignored: "pnpm.overrides".
```

override 必须写在 **`pnpm-workspace.yaml` 根级**，或通过 [pnpm 11 迁移指南](https://pnpm.io/migration) 迁移其他配置。

---

## 验证修复

修复后可通过以下方式确认：

1. **依赖版本**：`app-builder-lib` 下解析到的 `@electron/get` 应为 **4.x**（如 `4.0.3`），而非 `3.0.0`。
2. **打包日志**：应出现 `downloading label=electron` 并成功 `downloaded electron zip extracted successfully`，随后继续 NSIS 构建。
3. **产物**：`release\Serial Terminal Setup <版本>.exe` 正常生成。

---

## 相关警告（可忽略）

打包过程中 npm 可能输出：

```text
npm warn Unknown project config "electron_mirror"
npm warn Unknown project config "electron_builder_binaries_mirror"
```

这是 **npm 11** 不再识别 `.npmrc` 中部分旧配置项的提示，**不影响 pnpm 安装与 electron-builder 打包**。Electron 镜像仍由以下配置生效：

- `.npmrc` 中的 `electron_mirror`（pnpm 安装 electron 时使用）
- `package.json` → `build.electronDownload.mirror`（electron-builder 下载时使用）

---

## 参考

- [electron-builder CHANGELOG 26.15.x](https://github.com/electron-userland/electron-builder/blob/master/CHANGELOG.md) — 26.15 起迁移至 `@electron/get` 下载流水线
- [@electron/get ElectronDownloadCacheMode](https://packages.electronjs.org/get/v4.0.0/enums/ElectronDownloadCacheMode.html) — 缓存模式枚举（4.0.0+）
- [pnpm v10 → v11 迁移](https://pnpm.io/migration) — `overrides` 配置位置变更
