#!/bin/sh
# maXTouch xcfg 配置查看器 - Linux 启动脚本
cd "$(dirname "$0")" || exit 1

# 检查 Python
if ! command -v python3 >/dev/null 2>&1; then
    echo "错误: 未找到 python3"
    exit 1
fi

# 安装依赖（如需要）
if ! python3 -c "import flask" 2>/dev/null; then
    echo "正在安装依赖..."
    pip3 install -r requirements.txt
fi

# mxt-app 使用全局安装（sudo make install）
export MXT_APP="${MXT_APP:-mxt-app}"

echo "xcfg 配置查看器启动中..."
echo "访问 http://127.0.0.1:5000"
echo "mxt-app: $MXT_APP"
echo ""
echo "提示: 若出现 LIBUSB_ERROR_ACCESS，请用 sudo 运行: sudo bash run.sh"
echo ""

exec python3 app.py --host 0.0.0.0 --port 5000
