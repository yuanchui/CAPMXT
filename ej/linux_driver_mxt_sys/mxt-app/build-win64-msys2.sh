#!/usr/bin/env bash
# Windows 本地编译 mxt-app（在 MSYS2 MINGW64 环境中运行）
set -e

export MSYSTEM=MINGW64
export PATH="/mingw64/bin:/usr/bin:$PATH"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

if [ -f Makefile ] || [ -f config.status ]; then
  make distclean 2>/dev/null || true
  rm -f config.status config.log Makefile
fi

./autogen.sh
./configure --host=x86_64-w64-mingw32 --prefix=/mingw64 CFLAGS="-O2 -g0 -DWIN32_LEAN_AND_MEAN"
make -j"$(nproc 2>/dev/null || echo 2)"

if [ -f .libs/mxt-app.exe ]; then
  cp -f .libs/mxt-app.exe ./mxt-app.exe
fi

test -f mxt-app.exe
ls -la mxt-app.exe .libs/mxt-app.exe 2>/dev/null || ls -la mxt-app.exe
echo "=== 编译完成: $SCRIPT_DIR/mxt-app.exe ==="
