# maXTouch xcfg 配置查看器

基于网页的 xcfg 配置文件解析、查看、编辑工具，支持从设备读取和写入整个寄存器配置。

## 功能

- **解析 xcfg**：加载 `.xcfg` 文件，自动解析所有对象和字段
- **树形展示**：第一级显示对象（如 T68-串行数据命令），第二级显示 INSTANCE
- **字段编辑**：显示 OBJECT_ADDRESS、OBJECT_SIZE，以及序号、字段长度、名称、设置值（可修改）
- **读取单个对象**：输入对象号（如 T100），调用 `mxt-app -R -T 100` 读取并替换指定寄存器
- **写入单个对象**：编辑后点击「写入此对象」，调用 `mxt-app -W -T XX` 写入
- **指定保存路径**：在「保存路径」输入框填写路径（如 `/home/nano/config.xcfg`），保存时写入该路径
- **从设备读取**：调用 `mxt-app --save` 读取设备完整配置并解析
- **写入设备**：调用 `mxt-app --load` 将修改后的配置写入设备

## 运行环境

- Linux
- Python 3.6+
- Flask
- mxt-app（用于设备读写）

## 安装与运行

```bash
cd xcfg-viewer
pip3 install -r requirements.txt
python3 app.py
```

或使用启动脚本：

```bash
chmod +x run.sh
./run.sh
```

如遇 `$'\r': command not found`，说明文件含 Windows 换行符，请先执行：

```bash
sed -i 's/\r$//' run.sh
```

浏览器访问：http://127.0.0.1:5000

**从本机访问虚拟机**：需加 `--host 0.0.0.0`，例如：`python3 app.py --host 0.0.0.0 --port 5000`。若出现 502 或端口冲突，可换端口：`--port 5001`。详见下文「502 Bad Gateway 排查」。

**USB 设备访问**：若出现 `LIBUSB_ERROR_ACCESS`，需 root 权限。请用 sudo 运行：

```bash
sudo bash run.sh
```

## mxt-app 要求

写入/读取设备需要 mxt-app 工具。请先安装：

```bash
cd mxt-app
./autogen.sh && ./configure && make
sudo make install
```

安装后 `mxt-app` 为全局命令。如需指定路径，可设置环境变量：

```bash
export MXT_APP=/path/to/mxt-app
python3 app.py
```

## 从本机（Windows）访问虚拟机里的服务：502 Bad Gateway 排查

在 VMware 桥接模式下，在虚拟机里运行 `python3 app.py --host 0.0.0.0 --port 5000`，从 Windows 用 `http://<虚拟机IP>:5000/` 访问时出现 **502 Bad Gateway**，可按下面顺序排查。

**说明**：502 一般由「反向代理」在连不上后端时返回，Flask 开发服务器本身不会返回 502。因此要么是中间有代理，要么是请求根本没到 Flask。

### 1. 确认 Flask 在虚拟机内已监听并可直接访问

在虚拟机里执行：

```bash
# 看 5000 端口是否被本程序占用
ss -tlnp | grep 5000
# 或
curl -s http://127.0.0.1:5000/health
```

若 `curl http://127.0.0.1:5000/health` 在虚拟机内返回 `{"status":"ok",...}`，说明服务正常，问题在网络或防火墙。

### 2. 端口 5000 被占用（常见于 Ubuntu 等）

部分系统上 5000 被其他服务占用，Flask 可能没真正监听。改用其他端口再试：

```bash
python3 app.py --host 0.0.0.0 --port 5001
```

在 Windows 浏览器访问：`http://<虚拟机IP>:5001/`。

### 3. 虚拟机防火墙放行端口

在虚拟机（Linux）里放行对应端口，例如：

```bash
# Ubuntu/Debian (ufw)
sudo ufw allow 5000
sudo ufw reload
# 或临时关闭防火墙测试
sudo ufw disable
```

```bash
# CentOS/RHEL (firewalld)
sudo firewall-cmd --add-port=5000/tcp --permanent
sudo firewall-cmd --reload
```

### 4. 从 Windows 用 curl 测试（排除浏览器/代理）

在 Windows 命令行执行：

```bash
curl -v http://192.168.0.135:5000/health
```

- 若返回 `{"status":"ok",...}`：说明网络通，问题可能在浏览器或代理。
- 若超时或连接被拒：说明流量没到虚拟机或被防火墙拦截，继续查桥接网络和防火墙。

### 5. 桥接与 IP 确认

- 虚拟机网络设为「桥接」，并确认与 Windows 在同一网段（如 192.168.0.x）。
- 在虚拟机里执行 `ip addr` 或 `hostname -I`，用得到的 IP 在 Windows 访问。

### 6. 若中间有反向代理（Nginx/Caddy 等）

若 80/443 由 Nginx/Caddy 反代到 5000，502 表示代理连不上后端。请检查：

- 后端是否真的在跑：`curl http://127.0.0.1:5000/health`
- 代理配置里的 upstream 地址和端口是否为本机 `127.0.0.1:5000`（或实际监听端口）。

---

**快速健康检查**：服务启动后，在虚拟机内访问 `http://127.0.0.1:5000/health`，应返回 `{"status":"ok","service":"xcfg-viewer"}`。

## API

| 接口 | 方法 | 说明 |
|------|------|------|
| /api/parse-xcfg | POST | 解析 xcfg（file 或 content） |
| /api/save-xcfg | POST | 将 JSON 序列化为 xcfg；可传 path 指定保存路径 |
| /api/read-object | POST | 读取单个对象 `{object_type:100, instance:0}` |
| /api/write-object | POST | 写入单个对象 `{object_type, instance, data:[...]}` |
| /api/read-device | POST | 从设备读取完整配置 |
| /api/write-device | POST | 将配置写入设备 |
