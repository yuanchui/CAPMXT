# 图标文件说明

完整编译流程见项目根目录 **[BUILD.md](../BUILD.md)**（含 `npm run build:all` 一键脚本）。

请将以下图标文件放置在此目录中：

## Windows
- **文件名**: `icon.ico`
- **格式**: ICO 格式
- **推荐尺寸**: 256x256 像素（包含多个尺寸的 ICO 文件最佳）

## macOS
- **文件名**: `icon.icns`
- **格式**: ICNS 格式
- **推荐尺寸**: 512x512 像素

## Linux
- **文件名**: `icon.png`
- **格式**: PNG 格式
- **推荐尺寸**: 512x512 像素

## 图标转换工具

如果您有 PNG 图片，可以使用以下工具转换为 ICO：
- 在线工具: https://convertio.co/zh/png-ico/
- 或使用 ImageMagick: `magick convert icon.png -define icon:auto-resize=256,128,64,48,32,16 icon.ico`

## 当前配置

图标路径已在 `package.json` 中配置：
- Windows: `build/icon.ico`
- macOS: `build/icon.icns`
- Linux: `build/icon.png`

打包时请勿设置 `win.signAndEditExecutable: false`，否则 electron-builder 会跳过对 `Serial Terminal.exe` 的 rcedit 步骤，**任务栏/资源管理器仍显示 Electron 默认图标**。

### rcedit 报错 `Fatal error: Unable to commit changes`

Electron 39+ 在 **`asar: true`** 时，会先用 **resedit** 向 exe 写入 **ASAR 完整性**，再用 **rcedit** 改版本信息与图标；不少环境下 rcedit 会报 **Unable to commit changes**。当前默认 **`"asar": false`**，避免该冲突（应用以 `resources/app` 目录发布，略多文件；原生模块仍建议保留 **`asarUnpack`** 配置以备改回 asar）。

其他常见原因：**工程路径**含中文（尽量用 `D:\serial-app` 等英文路径）；**`author.name` / `build.copyright`** 含非 ASCII（需 ASCII，开始菜单子文件夹可用 **`nsis.menuCategory`**）；**杀毒软件**占用 exe。

### pnpm 打包时 npm 的 “Unknown env config” 警告

使用 `pnpm run package:...` 时，若脚本内部曾调用 `npm run build`，pnpm 注入的环境变量可能触发 npm 的告警。已将各 `package:*` 脚本改为直接执行 `tsc` 与 `vite build`，一般不再出现；可忽略，不影响产物。

`extraResources` 会将 `build/icon.ico` 复制到安装目录的 `resources/icon.ico`，主进程窗口在运行时使用该路径，与安装包内嵌图标一致。

## mxt-app CLI（自动打进安装包）

打包时会通过 `build/prepare-cli.js` 将 mxt-app 复制到 `CLI/`，再由 `extraResources` 安装到 `resources/CLI/`：

- `mxt-app.exe`
- `libusb-1.0.dll`（若能在 MSYS2 或构建目录中找到）

### Windows 本地编译（在 Windows 上打包时用此方式）

`build-w64-mingw.sh` 是 **Linux/WSL 交叉编译**脚本，在 Windows PowerShell 中无法使用。

1. 安装 [MSYS2](https://www.msys2.org/)
2. 打开 **MSYS2 MINGW64** 终端，安装依赖：
   ```bash
   pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-libusb autoconf automake libtool make
   ```
3. 在 `ej/serial-app` 执行：
   ```powershell
   npm run build:mxt-app
   npm run package:user:nsis:fast
   ```

或直接运行：`ej/linux_driver_mxt_sys/mxt-app/build-win64-msys2.bat`

若已安装 MSYS2 到非默认路径，设置 `MSYS2_ROOT` 后再编译。

### Linux/WSL 交叉编译

```bash
cd ej/linux_driver_mxt_sys/mxt-app
./build-w64-mingw.sh
```

产物：`build-win64/mxt-app.exe`

### 手动同步（可选）

```bash
cd ej/serial-app
npm run prepare:cli
```

也可将 `mxt-app.exe` 和 `libusb-1.0.dll` 直接放到 `ej/serial-app/CLI/`。

### 环境变量

| 变量 | 说明 |
|------|------|
| `MXT_APP_BUILD_DIR` | 覆盖 mxt-app 构建目录（含 `mxt-app.exe`） |
| `MSYS2_ROOT` | MSYS2 安装根目录（默认 `C:\msys64`） |
| `LIBUSB_DLL` | 指定 `libusb-1.0.dll` 完整路径 |
| `SKIP_MXT_APP_BUILD` | `1` 时打包前不尝试自动 MSYS2 编译 |

`npm run package:*` 在 `beforePack` 阶段会 **严格检查**（缺少 `mxt-app.exe` 则失败；Windows 上若检测到 MSYS2 会先尝试自动编译）。开发模式 `npm run dev` 仅警告，不阻断启动。

## Windows 代码签名（可选）

有 Authenticode 证书时，在打包前设置环境变量。**`$env:...` 仅适用于 PowerShell**；若在 **CMD（命令提示符）** 里粘贴会报「文件名、目录名或卷标语法不正确」，请改用 `set`。

PowerShell：

```powershell
$env:CSC_LINK = "C:\path\to\your-certificate.pfx"
$env:CSC_KEY_PASSWORD = "证书密码"
pnpm run package:user:nsis:fast
```

CMD：

```bat
set "CSC_LINK=C:\path\to\your-certificate.pfx"
set "CSC_KEY_PASSWORD=证书密码"
pnpm run package:user:nsis:fast
```

或使用 `WIN_CSC_LINK` / `WIN_CSC_KEY_PASSWORD`。证书也可放在证书存储中，并在 `package.json` 的 `win.signtoolOptions` 中配置 `certificateSubjectName` 等，详见 [electron-builder 代码签名](https://www.electron.build/code-signing)。

未配置证书时构建仍会成功，仅不会对 exe 做数字签名；`forceCodeSigning` 保持 `false` 即可。

### 时间戳服务器连不上（SignTool timestamp）

若出现 `The specified timestamp server either could not be reached or returned an invalid response`，多为网络无法访问时间戳服务。`win.signtoolOptions` 已配置 **Microsoft** 时间戳；在国内仍可能失败。

**默认**：所有 `package:*`（含 `package:user:nsis:small` 等）已通过 **`cmd /c "set ELECTRON_BUILDER_OFFLINE=true&& ..."`** 在打包时 **跳过时间戳**（自签名开发包足够；签名有效，证书过期后 Windows 可能不再信任，属预期）。

**需要带时间戳时**（网络可访问 TSA）：使用 **`package:user:nsis:small:online`** / **`package:user:nsis:fast:online`**，或先 **不要** 设置 `ELECTRON_BUILDER_OFFLINE` 并自行调用 electron-builder。

脚本依赖 Windows **CMD**；若在 macOS/Linux 交叉打包，请在本机用 `ELECTRON_BUILDER_OFFLINE=true` 环境变量等价设置。

打包全程请勿删除 `CSC_LINK` 指向的 PFX；长时间重试时若文件被移走，会报 `File not found: ...dev-signing.pfx`。

### 开发用自签名 PFX（自动生成）

在项目根目录执行（需 Windows PowerShell）：

```powershell
pnpm run cert:dev
```

可选参数示例：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File build/generate-dev-signing-cert.ps1 -OutPfx E:\certs\dev.pfx -Password "你的密码"
```

会在指定路径生成 **自签名** 代码签名证书并导出为 PFX；**不能消除 SmartScreen 警告**，仅便于本地验证签名流程。生成的 `build/dev-signing.pfx` 已加入 `.gitignore`，勿提交仓库。脚本结尾会同时打印 **PowerShell** 与 **CMD** 的环境变量写法，请按你当前窗口类型选用。



