# Serial Terminal 编译流程

工程路径：`ej/serial-app`  
关联组件：`ej/linux_driver_mxt_sys/mxt-app`（CLI 工具）、`USB_DEVICE/`（STM32 固件，独立烧录）

---

## 一键编译（推荐）

在 **PowerShell** 中于 `ej/serial-app` 目录执行：

```powershell
# 仅编译：mxt-app → 同步 CLI → serial-app（主进程 + 前端）
npm run build:all

# 完整流水线：上述步骤 + 打 NSIS 安装包（自动递增版本号）
npm run package:all
```

**每次打包会自动：**

1. `build` 号 +1（`1.0.0` → `1.0.1` → …，状态见 `build/version-state.json`）
2. 从 `ej/linux_driver_mxt_sys/xcfg-viewer/` 同步 `xcfg_viewer_metadata.json` 与 `metadata_images/` 到 `resources/xcfg-viewer/`
3. 生成 `resources/UPGRADE_NOTES.txt`（安装目录 `resources\` 下）及 `release/UPGRADE_NOTES-<版本>.txt`
4. 安装完成后自动打开升级说明（NSIS）

打包前请编辑 **`build/UPGRADE_NOTES.txt`** 填写补充说明（可选）；**Git 变更摘要**会在打包时自动从仓库提交记录生成，并写入 `release/UPGRADE_NOTES-<版本>.txt`。

或直接运行脚本：

```powershell
.\build\build-all.ps1
.\build\build-all.ps1 -Package          # 含打包
.\build\build-all.ps1 -SkipMxtApp       # 跳过 mxt-app，只编 serial-app
```

双击：`build\build-all.bat`（默认编译，不含打包）  
双击：`build\build-all-package.bat`（编译并打包）

---

## 编译流水线

```
┌─────────────────────────────────────────────────────────────┐
│ 1. mxt-app（MSYS2 MINGW64）                                  │
│    npm run build:mxt-app                                     │
│    → ej/linux_driver_mxt_sys/mxt-app/mxt-app.exe             │
├─────────────────────────────────────────────────────────────┤
│ 2. 同步 CLI                                                  │
│    npm run prepare:cli                                       │
│    → ej/serial-app/CLI/mxt-app.exe + libusb-1.0.dll         │
├─────────────────────────────────────────────────────────────┤
│ 3. serial-app 主进程                                         │
│    npm run build:main                                        │
│    → dist/main/                                              │
├─────────────────────────────────────────────────────────────┤
│ 4. serial-app 前端                                           │
│    npm run build:renderer                                    │
│    → dist/renderer/                                          │
├─────────────────────────────────────────────────────────────┤
│ 5. 打包（可选）                                              │
│    npm run package:user:nsis:fast                           │
│    → prepare-package（版本+说明+xcfg 同步）                  │
│    → release/Serial Terminal Setup <版本>.exe               │
│    → release/UPGRADE_NOTES-<版本>.txt                       │
└─────────────────────────────────────────────────────────────┘
```

---

## 环境准备（首次）

### 1. Node.js 依赖

```powershell
cd ej\serial-app
npm install
```

### 2. MSYS2（编译 mxt-app）

1. 安装 [MSYS2](https://www.msys2.org/)
2. 打开 **MSYS2 MINGW64** 终端，安装工具链：

```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-libusb autoconf automake libtool make
```

非默认安装路径时设置环境变量：

```powershell
$env:MSYS2_ROOT = "D:\msys64"
```

---

## 分步命令

| 步骤 | 命令 | 产物 |
|------|------|------|
| 编译 mxt-app | `npm run build:mxt-app` | `mxt-app/mxt-app.exe` |
| 同步 CLI | `npm run prepare:cli` | `CLI/mxt-app.exe` + `libusb-1.0.dll` |
| 严格同步 CLI | `npm run prepare:cli:strict` | 缺文件则失败 |
| 主进程 | `npm run build:main` | `dist/main/` |
| 前端 | `npm run build:renderer` | `dist/renderer/` |
| 应用编译 | `npm run build` | 主进程 + 前端 |
| 开发调试 | `npm run dev` | 热重载 + Electron |
| NSIS 安装包 | `npm run package:user:nsis:fast` | `release/` |
| 仅同步 xcfg 资源 | `npm run sync:xcfg-resources` | `resources/xcfg-viewer/` |
| 仅准备打包元数据 | `npm run prepare:package` | 版本号 + 升级说明 |
| 便携版 | `npm run package:user:portable:fast` | `release/` |

---

## 改了什么就编译什么

| 改动位置 | 需要执行 |
|----------|----------|
| `mxt-app/src/**` | `npm run build:mxt-app` → `npm run prepare:cli` |
| `serial-app/src/main/**` | `npm run build:main` |
| `serial-app/src/renderer/**` | `npm run build:renderer` |
| 需要安装包 | `npm run package:all` 或 `npm run package:user:nsis:fast` |
| STM32 固件 `USB_DEVICE/` | CubeIDE 等工具烧录 MCU（与 npm 无关） |

---

## 环境变量

| 变量 | 说明 |
|------|------|
| `MSYS2_ROOT` | MSYS2 根目录（默认 `C:\msys64`） |
| `MXT_APP_BUILD_DIR` | 指定含 `mxt-app.exe` 的目录 |
| `LIBUSB_DLL` | 指定 `libusb-1.0.dll` 路径 |
| `SKIP_MXT_APP_BUILD` | `1` 时 `prepare:cli` 不自动编译 mxt-app |

---

## 常见问题

### mxt-app 报 Linux 头文件错误

Windows 上**不要**用 `build-w64-mingw.sh`（Linux 交叉编译）。应使用：

```powershell
npm run build:mxt-app
```

内部调用 `build-win64-msys2.sh`（MINGW64 本地编译）。

### 打包报 CLI 缺失

```powershell
npm run build:mxt-app
npm run prepare:cli:strict
npm run package:user:nsis:fast
```

### 路径含中文

若 electron-builder / rcedit 异常，可将工程复制到英文路径（如 `D:\serial-app`）再打包。

---

## 产物路径速查

| 产物 | 路径 |
|------|------|
| mxt-app | `ej/linux_driver_mxt_sys/mxt-app/mxt-app.exe` |
| CLI 暂存 | `ej/serial-app/CLI/` |
| 安装包内 CLI | `resources/CLI/`（打包后） |
| 主进程 | `ej/serial-app/dist/main/` |
| 前端 | `ej/serial-app/dist/renderer/` |
| 安装包 | `ej/serial-app/release/` |
