#!/usr/bin/env bash
# 在服务器上首次执行：Python3 静态 HTTP 服务，监听指定端口（默认 19876）
set -euo pipefail

WEB_ROOT="/var/www/mxt-runtime"
INSTALL_DIR="/opt/mxt-runtime"
SERVICE_NAME="mxt-runtime"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HTTP_PORT="${HTTP_PORT:-19876}"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "请用 root 执行: sudo HTTP_PORT=${HTTP_PORT} bash setup-server.sh"
  exit 1
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y python3

mkdir -p "$WEB_ROOT" "$INSTALL_DIR"
chmod 755 "$WEB_ROOT"

cp "$SCRIPT_DIR/serve_runtime.py" "$INSTALL_DIR/serve_runtime.py"
chmod 755 "$INSTALL_DIR/serve_runtime.py"

sed -e "s/__HTTP_PORT__/${HTTP_PORT}/g" -e "s|__WEB_ROOT__|${WEB_ROOT}|g" \
  "$SCRIPT_DIR/mxt-runtime.service" \
  > "/etc/systemd/system/${SERVICE_NAME}.service"

systemctl daemon-reload
systemctl enable "${SERVICE_NAME}"
systemctl restart "${SERVICE_NAME}"

if command -v ufw >/dev/null 2>&1; then
  ufw allow "${HTTP_PORT}/tcp" || true
fi

SERVER_IP="$(hostname -I | awk '{print $1}')"
echo ""
echo "=========================================="
echo " Python 服务已就绪 (HTTP :${HTTP_PORT})"
echo " 程序: ${INSTALL_DIR}/serve_runtime.py"
echo " 数据: ${WEB_ROOT}/runtime-window.generated.json"
echo " 访问: http://${SERVER_IP}:${HTTP_PORT}/runtime-window.generated.json"
echo " 状态: systemctl status ${SERVICE_NAME}"
echo ""
echo " 云安全组请放行 TCP ${HTTP_PORT}"
echo " 下一步（开发机）: npm run deploy:runtime-window"
echo "=========================================="
