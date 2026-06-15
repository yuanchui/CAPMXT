#!/bin/bash
#-----------------------------------------------------------------------------
# 交叉编译 mxt-app 为 Windows x64 CLI（仅 USB 支持）
# 在 Linux 上需要安装 MinGW-w64 和 libusb-1.0 的 Windows 库。
#
# 示例（以 Debian/Ubuntu 为例）:
#   sudo apt install gcc-mingw-w64-x86-64
#   # libusb for MinGW：需单独提供，见 BUILD_WINDOWS.md
#
# 若使用 MSYS2 的 MinGW 工具链与库，可设置：
#   export PATH="/path/to/msys2/mingw64/bin:$PATH"
#   ./configure --host=x86_64-w64-mingw32 ...
#-----------------------------------------------------------------------------

set -e

HOST=x86_64-w64-mingw32
BUILD_DIR=build-win64
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== 配置 Windows x64 交叉编译 (host=$HOST) ==="

# 若在 MSYS2 环境下，可通过 pkg-config 找到 libusb
# 交叉编译时往往需要显式指定 libusb 的 prefix
if [ -n "$MINGW_PREFIX" ]; then
  export PKG_CONFIG_PATH="${MINGW_PREFIX}/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
  export PKG_CONFIG_LIBDIR="${MINGW_PREFIX}/lib/pkgconfig"
fi

# 如未安装到标准 prefix，可设置 LIBUSB_PREFIX，例如：
# export LIBUSB_PREFIX=/opt/mingw64
if [ -n "$LIBUSB_PREFIX" ]; then
  export CPPFLAGS="-I${LIBUSB_PREFIX}/include ${CPPFLAGS:-}"
  export LDFLAGS="-L${LIBUSB_PREFIX}/lib ${LDFLAGS:-}"
fi

# 若源码目录已配置过（例如之前做过本机 make），先 distclean，否则无法在子目录中 configure
if [ -f "$SCRIPT_DIR/config.status" ]; then
  echo "清理源码目录中的旧配置..."
  (cd "$SCRIPT_DIR" && make distclean 2>/dev/null) || true
fi

# 在源码目录生成 configure（不执行 autogen 末尾的 configure）
autoreconf -v --install 2>/dev/null || true

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

if ! ../configure \
  --host=$HOST \
  --prefix=/usr/local \
  --enable-debug=no \
  CFLAGS="-O2" \
  "${@}"; then
  echo ""
  echo "=== 配置失败：未找到 libusb-1.0 ==="
  echo "Ubuntu/Debian 的 apt 仓库不提供 Windows 版 libusb，请选择以下方式之一："
  echo ""
  echo "方式 1：使用 MSYS2（推荐）"
  echo "  1. 安装 MSYS2: https://www.msys2.org/"
  echo "  2. 在 MSYS2 MINGW64 终端中: pacman -S mingw-w64-x86_64-libusb"
  echo "  3. 设置环境变量后重新运行此脚本："
  echo "     export MINGW_PREFIX=/path/to/msys2/mingw64"
  echo "     export PATH=\$MINGW_PREFIX/bin:\$PATH"
  echo ""
  echo "方式 2：手动下载 Windows 版 libusb"
  echo "  1. 从 https://libusb.info/ 或 MSYS2 获取 libusb-1.0 的 Windows 库"
  echo "  2. 解压到某个目录（如 /opt/mingw64）"
  echo "  3. 设置环境变量后重新运行："
  echo "     export LIBUSB_PREFIX=/opt/mingw64"
  echo ""
  echo "方式 3：在 Windows 上使用 MSYS2 本地编译（见 BUILD_WINDOWS.md）"
  exit 1
fi

# 交叉编译时 libtool 会因 -L/opt/mingw64 等路径无法转换为“宿主路径”而报错，可安全忽略。
# 过滤该两行提示，避免干扰；若 make 失败会保留完整输出。
make -j"$(nproc 2>/dev/null || echo 2)" 2>&1 | tee make.log | sed \
  -e '/Could not determine the host path corresponding to/d' \
  -e '/Continuing, but uninstalled executables may not work/d'
code=${PIPESTATUS[0]}
if [ $code -ne 0 ]; then
  echo "=== make 失败，完整输出如下 ==="
  cat make.log
  exit $code
fi
echo ""
echo "=== 编译完成 ==="
echo "可执行文件: $SCRIPT_DIR/$BUILD_DIR/mxt-app.exe"
echo "复制到 Windows 时需同时提供 libusb-1.0.dll（可从 MinGW 或 libusb 官方运行时获取）。"
