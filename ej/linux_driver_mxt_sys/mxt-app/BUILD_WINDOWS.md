# 在 Windows x64 上构建 mxt-app CLI

Windows 版本仅支持 **USB** 连接（如 STM32 VCP 桥接），不支持 sysfs、I2C-dev、hidraw、TCP bridge 等 Linux 专用功能。

## 方式一：在 Linux 上交叉编译 (推荐)

### 步骤 1：安装 MinGW-w64 编译器

```bash
sudo apt install gcc-mingw-w64-x86-64
```

### 步骤 2：获取 Windows 版 libusb-1.0

**重要**：Ubuntu/Debian 的 apt 仓库**不提供** Windows 版 libusb。`pacman` 命令只能在 **MSYS2 环境**（Windows 上的 MSYS2）中运行，不能在 Ubuntu 上直接使用。

在 Ubuntu 上交叉编译时，请选择以下方式之一：

#### 选项 A：从 MSYS2 仓库直接下载包文件（推荐）

1. 访问 MSYS2 包仓库：https://packages.msys2.org/package/mingw-w64-x86_64-libusb
2. 下载 `.pkg.tar.zst` 文件（例如：`mingw-w64-x86_64-libusb-1.0.26-1-x86_64.pkg.tar.zst`）
3. 在 Ubuntu 上安装 `zstd` 解压工具：
   ```bash
   sudo apt install zstd
   ```
4. 解压包文件（需要两步：先解 zstd，再解 tar）：
   ```bash
   mkdir -p /tmp/libusb-extract
   # 方法 1：分步解压
   zstd -d mingw-w64-x86_64-libusb-*.pkg.tar.zst -o /tmp/libusb.tar
   tar -xf /tmp/libusb.tar -C /tmp/libusb-extract
   
   # 或方法 2：使用支持 zstd 的 tar（较新版本的 tar）
   # tar --zstd -xf mingw-w64-x86_64-libusb-*.pkg.tar.zst -C /tmp/libusb-extract
   ```
5. 复制文件到标准位置：
   ```bash
   sudo mkdir -p /opt/mingw64/{include,lib}
   sudo cp -r /tmp/libusb-extract/mingw64/include/* /opt/mingw64/include/
   sudo cp -r /tmp/libusb-extract/mingw64/lib/* /opt/mingw64/lib/
   ```
6. 设置环境变量：
   ```bash
   export LIBUSB_PREFIX=/opt/mingw64
   ```

#### 选项 B：从 Windows 机器复制（如果有）

如果你有 Windows 机器并已安装 MSYS2：
1. 在 Windows 的 MSYS2 MINGW64 终端中：
   ```bash
   pacman -S mingw-w64-x86_64-libusb
   ```
2. 将 `C:\msys64\mingw64` 目录复制到 Ubuntu（通过网络共享、USB 等）
3. 设置环境变量：
   ```bash
   export MINGW_PREFIX=/path/to/mingw64  # 复制到的路径
   export PATH=$MINGW_PREFIX/bin:$PATH
   ```

#### 选项 C：从 libusb 官方下载

1. 访问 [libusb 官方下载页面](https://libusb.info/)
2. 下载 Windows 预编译库（如果有）
3. 解压并设置 `LIBUSB_PREFIX` 环境变量指向解压目录

### 步骤 3：编译

在工程根目录执行：
```bash
chmod +x build-w64-mingw.sh
./build-w64-mingw.sh
```

如果 configure 报错找不到 libusb，请检查：
- 是否已设置 `MINGW_PREFIX` 或 `LIBUSB_PREFIX` 环境变量
- libusb 的头文件和库文件是否在正确位置

**或手动编译：**
```bash
./autogen.sh
./configure --host=x86_64-w64-mingw32 CFLAGS=-O2
make
```

生成的可执行文件在 `build-win64/mxt-app.exe`（使用脚本）或源码目录的 `mxt-app.exe`（手动编译）。

### 步骤 4：部署到 Windows

将以下文件复制到 Windows：
- `mxt-app.exe`
- **libusb-1.0.dll**（从 MSYS2 的 `mingw64/bin/` 或 libusb 运行时获取）

确保 `libusb-1.0.dll` 与 `mxt-app.exe` 在同一目录，或放在系统 PATH 中。

## 方式二：在 Windows 上本地编译 (MSYS2)

1. 安装 [MSYS2](https://www.msys2.org/)，在 **MINGW64** 终端中：
   ```bash
   pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-libusb autoconf automake libtool make
   ```

2. 在工程目录：
   ```bash
   ./autogen.sh
   ./configure --prefix=/mingw64
   make
   ```
   configure 会自动检测到 MinGW 并启用 Windows 构建（USB only）。

3. 生成的 `mxt-app.exe` 在 MSYS2 MINGW64 环境下可直接运行（libusb-1.0.dll 已由 MSYS2 提供）。

## 使用说明

- 连接设备：`mxt-app -d usb:BUS-DEVICE`，例如 `mxt-app -d usb:001-003`（总线-设备号可在 Linux 上通过 `lsusb` 或 Windows 上通过设备管理器/第三方工具查看）。
- Windows 下需安装 [Zadig](https://zadig.akeo.ie/) 等工具为 STM32 VCP 安装 WinUSB/libusb 驱动后，libusb 才能访问设备。
https://zadig.akeo.ie/
## 故障排除：运行后没有任何输出

若在 cmd 或 PowerShell 中执行 `mxt-app.exe --help` 或 `mxt-app.exe -d usb:001-005` 后没有任何输出，请按下列项检查：

1. **必须从控制台运行**  
   请从 **cmd.exe** 或 **PowerShell** 中运行，不要双击 `.exe`。双击时控制台会一闪而过，看不到输出。

2. **确认 libusb-1.0.dll**  
   若缺少 `libusb-1.0.dll`，Windows 会弹出“找不到 xxx.dll”的对话框，程序不会正常进入 main。请将 `libusb-1.0.dll` 放在与 `mxt-app.exe` 同一目录，或放入系统 PATH。

3. **重新编译**  
   当前构建已加入：`-mconsole`（控制台子系统）、启动时对 stdout/stderr 设为无缓冲、以及 `print_usage`/版本输出后的 `fflush`。若你使用的是旧版 exe，请重新执行 `build-w64-mingw.sh` 或 `make` 后再试。

4. **路径与编码**  
   若仍无输出，可尝试把 `mxt-app.exe` 和 `libusb-1.0.dll` 复制到**仅含英文的路径**（例如 `C:\tools\`）再运行，以排除当前目录名含非 ASCII 字符带来的影响。
