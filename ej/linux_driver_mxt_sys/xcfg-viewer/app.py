#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
maXTouch xcfg 配置查看器 - Flask 后端
提供 xcfg 解析、保存、写入设备 API
"""

import json
import os
import re
import tempfile
import subprocess
from pathlib import Path
import platform

from flask import Flask, request, jsonify, send_from_directory
from werkzeug.utils import secure_filename

from xcfg_parser import parse_xcfg, serialize_xcfg


app = Flask(__name__, static_folder='static')
app.config['MAX_CONTENT_LENGTH'] = 16 * 1024 * 1024  # 16MB

# mxt-app 路径（可配置）
def _detect_mxt_app_path() -> str:
    """
    优先级：
    1) 环境变量 MXT_APP
    2) Windows 下：同目录 CLI/mxt-app.exe（便于直接打包分发）
    3) 默认：mxt-app（依赖 PATH）
    """
    env_path = os.environ.get('MXT_APP')
    if env_path:
        return env_path

    exe_name = 'mxt-app.exe' if platform.system().lower().startswith('win') else 'mxt-app'
    cli_path = Path(__file__).resolve().parent / 'CLI' / exe_name
    if cli_path.exists():
        return str(cli_path)

    return exe_name


MXT_APP_PATH = _detect_mxt_app_path()

# 别名/描述等元数据、服务器端 xcfg 文件保存到 Python 同目录
_APP_DIR = Path(__file__).resolve().parent
METADATA_FILE = _APP_DIR / 'xcfg_viewer_metadata.json'
METADATA_IMAGES_DIR = _APP_DIR / 'metadata_images'
METADATA_IMAGES_DIR.mkdir(parents=True, exist_ok=True)

# 服务器端 xcfg 保存目录
SERVER_XCFG_DIR = _APP_DIR / 'xcfg'
SERVER_XCFG_DIR.mkdir(parents=True, exist_ok=True)


# 支持的 VID:PID 列表（小写、无 0x 前缀）。
# 参考：mxt-app/src/libmaxtouch/usb/usb_device.c 中的 usb_supported_pid_vid 逻辑
_SUPPORTED_VIDPID = {
    '0483:5740',  # STM32 Virtual Bridge
    '03eb:211d',
    '03eb:2119',
    '03eb:6123',
}

# 动态生成 PID 范围 (0x2126-0x212D, 0x2135-0x2139, 0x213A-0x21FC, 0x8000-0x8FFF)
for pid in range(0x2126, 0x212D + 1):
    _SUPPORTED_VIDPID.add(f'03eb:{pid:04x}')
for pid in range(0x2135, 0x2139 + 1):
    _SUPPORTED_VIDPID.add(f'03eb:{pid:04x}')
for pid in range(0x213A, 0x21FC + 1):
    _SUPPORTED_VIDPID.add(f'03eb:{pid:04x}')
for pid in range(0x8000, 0x8FFF + 1):
    _SUPPORTED_VIDPID.add(f'03eb:{pid:04x}')


def _extract_supported_devices_from_q(stdout: str):
    """从 `mxt-app -q` 的输出中提取支持的 usb:BBB-AAA 设备标识列表。"""
    devices = []
    for line in (stdout or '').splitlines():
        s = line.strip()
        if not s:
            continue
        m = re.match(r'^(usb:\d{3}-\d{3})\s+([0-9A-Fa-f]{4}:[0-9A-Fa-f]{4})\b', s)
        if not m:
            continue
        dev_id = m.group(1)
        vidpid = m.group(2).lower()
        if vidpid in _SUPPORTED_VIDPID:
            devices.append(dev_id)
    return devices


def _auto_detect_device(timeout=10):
    """自动枚举并选择唯一的受支持设备；返回 (device, devices, err)。"""
    q = _run_mxt_app(['-q'], timeout=timeout, auto_device=False)
    if not q['success']:
        return None, [], q['stderr'] or q['stdout'] or '枚举设备失败'

    devices = _extract_supported_devices_from_q(q.get('stdout') or '')
    if len(devices) == 1:
        return devices[0], devices, None
    if len(devices) == 0:
        return None, devices, '未找到支持的设备（支持 VID:PID: ' + ', '.join(sorted(_SUPPORTED_VIDPID)) + '）'
    return None, devices, '检测到多个支持的设备，请指定 device 参数'


def _run_mxt_app(args, timeout=30, *, auto_device=True, device=None):
    """Run mxt-app and return {success, returncode, stdout, stderr}.

    - 若 `auto_device=True` 且当前参数未显式包含 `-d/--device`，则会尝试自动枚举并注入 `-d <usb:BBB-AAA>`。
    - 可通过显式传入 `device` 覆盖（等价于自动注入）。
    """
    exe_path = MXT_APP_PATH
    exe_dir = str(Path(exe_path).resolve().parent)

    env = os.environ.copy()
    if platform.system().lower().startswith('win'):
        env['PATH'] = exe_dir + os.pathsep + env.get('PATH', '')

    final_args = list(args)

    # 是否已显式指定设备
    has_device_arg = False
    for i, a in enumerate(final_args):
        if a in ('-d', '--device'):
            has_device_arg = True
            break
        if a.startswith('-d') and len(a) > 2:
            # 兼容 -dusb:001-002 这种写法
            has_device_arg = True
            break
        if a.startswith('--device='):
            has_device_arg = True
            break

    # 环境变量强制指定设备（优先级低于显式参数/函数入参）
    env_device = (os.environ.get('MXT_DEVICE') or '').strip() or None

    inject_device = None
    if not has_device_arg:
        if device:
            inject_device = device
        elif env_device:
            inject_device = env_device
        elif auto_device:
            d, ds, err = _auto_detect_device(timeout=10)
            if err:
                return {
                    'success': False,
                    'returncode': 1,
                    'stdout': q.get('stdout') if 'q' in locals() else '',
                    'stderr': err,
                    'devices': ds,
                }
            inject_device = d

    if inject_device:
        final_args = ['-d', inject_device] + final_args

    result = subprocess.run(
        [exe_path] + list(final_args),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        universal_newlines=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
        env=env,
    )
    return {
        'success': result.returncode == 0,
        'returncode': result.returncode,
        'stdout': result.stdout,
        'stderr': result.stderr,
    }


# In-memory latest diagnostic dump cache
_DIAG_LATEST = {
    'content': '',
    'ts': 0,
    'params': {},
}


@app.route('/')
def index():
    return send_from_directory('static', 'index.html')


@app.route('/api/metadata', methods=['GET'])
def api_metadata_get():
    """读取同目录下的 xcfg_viewer_metadata.json"""
    if not METADATA_FILE.exists():
        return jsonify({'fieldAliases': {}, 'fieldDescriptions': {}, 'objectDescriptions': {}})
    try:
        with open(METADATA_FILE, 'r', encoding='utf-8') as f:
            data = json.load(f)
        return jsonify(data)
    except Exception as e:
        return jsonify({'error': str(e)}), 500


@app.route('/api/metadata', methods=['POST'])
def api_metadata_post():
    """将别名、描述等元数据保存到 Python 同目录的 xcfg_viewer_metadata.json"""
    data = request.get_json()
    if not data:
        return jsonify({'error': '请提供 JSON 数据'}), 400
    try:
        out = {
            'fieldAliases': data.get('fieldAliases', {}),
            'fieldDescriptions': data.get('fieldDescriptions', {}),
            'objectDescriptions': data.get('objectDescriptions', {}),
        }
        with open(METADATA_FILE, 'w', encoding='utf-8') as f:
            json.dump(out, f, ensure_ascii=False, indent=2)
        return jsonify({'success': True, 'path': str(METADATA_FILE)})
    except Exception as e:
        return jsonify({'error': str(e)}), 500


@app.route('/api/upload-image', methods=['POST'])
def api_upload_image():
    """上传图片到 metadata_images 目录，用于 Markdown 描述中引用。返回可访问的 URL。"""
    if 'file' not in request.files:
        return jsonify({'error': '请选择图片文件'}), 400
    f = request.files['file']
    if f.filename == '':
        return jsonify({'error': '未选择文件'}), 400
    ext = Path(f.filename).suffix.lower() or '.png'
    if ext not in ('.png', '.jpg', '.jpeg', '.gif', '.webp', '.svg'):
        return jsonify({'error': '仅支持图片格式 png/jpg/gif/webp/svg'}), 400
    METADATA_IMAGES_DIR.mkdir(parents=True, exist_ok=True)
    base = secure_filename(Path(f.filename).stem) or 'image'
    if not base:
        base = 'image'
    # 自动生成唯一文件名：base-timestamp-random.ext
    import time
    import random
    ts = int(time.time() * 1000)
    rnd = random.randint(1000, 9999)
    name = f'{base}-{ts}-{rnd}{ext}'
    dest = METADATA_IMAGES_DIR / name
    try:
        f.save(str(dest))
        url = f'/metadata_images/{name}'
        return jsonify({'success': True, 'url': url})
    except Exception as e:
        return jsonify({'error': str(e)}), 500


@app.route('/metadata_images/<path:filename>')
def serve_metadata_image(filename):
    """提供 metadata_images 目录下的图片访问"""
    return send_from_directory(str(METADATA_IMAGES_DIR), filename)


@app.route('/api/rename-image', methods=['POST'])
def api_rename_image():
    """重命名 metadata_images 目录中的图片文件，并返回新的 URL。"""
    data = request.get_json() or {}
    old_name = (data.get('old') or '').strip()
    new_name = (data.get('new') or '').strip()

    if not old_name or not new_name:
        return jsonify({'error': '请提供 old/new 文件名'}), 400

    old_name = os.path.basename(old_name)
    new_name = os.path.basename(new_name)

    # 仅允许指定的图片扩展名
    allowed_exts = {'.png', '.jpg', '.jpeg', '.gif', '.webp', '.svg'}
    old_ext = Path(old_name).suffix.lower()
    new_ext = Path(new_name).suffix.lower()
    if old_ext not in allowed_exts or new_ext not in allowed_exts:
        return jsonify({'error': '仅支持图片格式 png/jpg/gif/webp/svg'}), 400

    # 强制保持扩展名一致，避免把 png 改成 jpg 之类引发混乱
    if old_ext != new_ext:
        return jsonify({'error': '不允许修改扩展名'}), 400

    src = METADATA_IMAGES_DIR / old_name
    dst = METADATA_IMAGES_DIR / new_name

    if not src.exists():
        return jsonify({'error': '原文件不存在: ' + old_name}), 404
    if dst.exists():
        return jsonify({'error': '目标文件已存在: ' + new_name}), 409

    try:
        os.rename(str(src), str(dst))
        return jsonify({'success': True, 'url': f'/metadata_images/{new_name}'})
    except Exception as e:
        return jsonify({'error': str(e)}), 500


@app.route('/health')
def health():
    """健康检查，用于确认服务是否可访问（排查 502 时可用）"""
    return jsonify({'status': 'ok', 'service': 'xcfg-viewer'})


@app.route('/api/parse-xcfg', methods=['POST'])
def api_parse_xcfg():
    """解析 xcfg 文件，返回 JSON 结构"""
    content = None
    if request.content_type and 'multipart/form-data' in request.content_type:
        if 'file' in request.files:
            f = request.files['file']
            if f.filename != '':
                content = f.read().decode('utf-8', errors='replace')
        elif 'content' in request.form:
            content = request.form['content']
    elif request.is_json:
        data = request.get_json()
        content = data.get('content') if data else None
    elif request.data:
        content = request.data.decode('utf-8', errors='replace')

    if not content:
        return jsonify({'error': '请上传 xcfg 文件或提供 content'}), 400

    try:
        data = parse_xcfg(content)
        return jsonify(data)
    except Exception as e:
        return jsonify({'error': str(e)}), 500


@app.route('/api/save-xcfg', methods=['POST'])
def api_save_xcfg():
    """
    将修改后的 JSON 保存为 xcfg 文件
    请求体:
      - { "config": {...} 或直接为 config, "path": "/可选/保存路径" }
      - 新增: { "config": {...}, "name": "文件名（不含路径，可不含扩展名）", "saveToServer": true }
    行为:
      - 若提供 path，则保存到指定路径；
      - 若提供 saveToServer/name，则保存到后端 xcfg 目录；
      - 同时总是返回 content 供前端触发下载
    """
    req = request.get_json()
    if not req:
        return jsonify({'error': '请提供 JSON 数据'}), 400

    data = req.get('config', req) if isinstance(req, dict) else req
    save_path = req.get('path') if isinstance(req, dict) else None
    save_to_server = bool(req.get('saveToServer')) if isinstance(req, dict) else False
    server_name = ''
    if isinstance(req, dict):
        server_name = (req.get('name') or '').strip()

    if not data:
        return jsonify({'error': '请提供 config 数据'}), 400

    try:
        content = serialize_xcfg(data)

        saved_server_path = None
        # 保存到调用方指定路径
        if save_path:
            save_path = os.path.abspath(save_path.strip())
            if not save_path.endswith('.xcfg'):
                save_path += '.xcfg'
            with open(save_path, 'w', encoding='utf-8') as f:
                f.write(content)

        # 保存到后端固定 xcfg 目录（基于文件名）
        if save_to_server:
            base_name = server_name or 'config'
            base_name = os.path.basename(base_name)
            if not base_name.lower().endswith('.xcfg'):
                base_name += '.xcfg'
            saved_server_path = SERVER_XCFG_DIR / base_name
            with open(saved_server_path, 'w', encoding='utf-8') as f:
                f.write(content)

        resp = {
            'content': content,
            'success': True,
        }
        if save_path:
            resp['path'] = save_path
        if saved_server_path:
            resp['serverPath'] = str(saved_server_path)
        return jsonify(resp)
    except Exception as e:
        return jsonify({'error': str(e)}), 500


@app.route('/api/server-xcfg-list', methods=['GET'])
def api_server_xcfg_list():
    """
    列出后端 xcfg 目录中的所有 .xcfg 文件
    返回: { files: [ { name, size, mtime } ] }
    """
    files = []
    try:
        for p in SERVER_XCFG_DIR.glob('*.xcfg'):
            stat = p.stat()
            files.append({
                'name': p.name,
                'size': stat.st_size,
                'mtime': int(stat.st_mtime),
            })
        files.sort(key=lambda x: x['mtime'], reverse=True)
        return jsonify({'files': files})
    except Exception as e:
        return jsonify({'error': str(e)}), 500


@app.route('/api/load-server-xcfg', methods=['POST'])
def api_load_server_xcfg():
    """
    从后端 xcfg 目录加载指定文件并解析为 JSON
    请求体: { "name": "xxx.xcfg" }
    """
    data = request.get_json() or {}
    name = (data.get('name') or '').strip()
    if not name:
        return jsonify({'error': '请提供文件名'}), 400

    name = os.path.basename(name)
    if not name.lower().endswith('.xcfg'):
        name += '.xcfg'
    path = SERVER_XCFG_DIR / name
    if not path.exists():
        return jsonify({'error': f'文件不存在: {name}'}), 404

    try:
        with open(path, 'r', encoding='utf-8') as f:
            content = f.read()
        cfg = parse_xcfg(content)
        return jsonify({'success': True, 'data': cfg, 'name': name})
    except Exception as e:
        return jsonify({'error': str(e)}), 500


@app.route('/api/write-device', methods=['POST'])
def api_write_device():
    """将修改后的配置写入设备。"""
    data = request.get_json() or {}

    # 兼容两种请求体：
    # 1) 直接传 config JSON（旧行为）
    # 2) { config: {...}, device: "usb:001-027" }
    device = data.get('device') if isinstance(data, dict) else None
    cfg = data.get('config', data) if isinstance(data, dict) else data

    if not cfg:
        return jsonify({'error': '请提供 JSON 数据'}), 400

    try:
        content = serialize_xcfg(cfg)
    except Exception as e:
        return jsonify({'error': f'序列化失败: {e}'}), 500

    fd, tmp_path = tempfile.mkstemp(suffix='.xcfg', prefix='mxt_')
    try:
        with os.fdopen(fd, 'w', encoding='utf-8') as f:
            f.write(content)

        result = _run_mxt_app(['--load', tmp_path], device=device, timeout=60)

        if result['success']:
            return jsonify({
                'success': True,
                'message': '配置已成功写入设备',
                'stdout': result['stdout']
            })
        else:
            return jsonify({
                'success': False,
                'error': result['stderr'] or result['stdout'] or f"退出码 {result['returncode']}",
                'returncode': result['returncode'],
                'devices': result.get('devices'),
            })
    finally:
        try:
            os.unlink(tmp_path)
        except OSError:
            pass


@app.route('/api/read-object', methods=['POST'])
def api_read_object():
    """
    从设备读取单个对象（对应 mxt-app D 命令）
    调用 mxt-app -R -T OBJ -I INST -f
    """
    data = request.get_json() or {}
    obj_type = int(data.get('object_type', 0))
    instance = int(data.get('instance', 0))
    if obj_type <= 0:
        return jsonify({'error': '请提供 object_type（如 100）'}), 400

    device = (data.get('device') or '').strip() or None

    try:
        result = _run_mxt_app(['-R', '-T', str(obj_type), '-I', str(instance), '-f', '1'], device=device, timeout=15)

        if not result['success']:
            return jsonify({
                'success': False,
                'error': result['stderr'] or result['stdout'] or '读取失败',
                'devices': result.get('devices'),
            }), 500

        # 解析输出: "00:\t0x8F\t143\t1000 1111"
        fields = []
        obj_name = None
        for line in result.stdout.splitlines():
            line = line.strip()
            if not line:
                continue
            # 对象名: "T100-多点触摸屏"
            if line.startswith('T') and '-' in line and ':' not in line:
                obj_name = line
                continue
            m = re.match(r'^(\d+):\s+0x([0-9A-Fa-f]+)', line)
            if m:
                offset = int(m.group(1))
                value = int(m.group(2), 16)
                fields.append({
                    'offset': offset,
                    'length': 1,
                    'name': 'UNKNOWN[%d]' % offset,
                    'value': value
                })

        return jsonify({
            'success': True,
            'object_type': obj_type,
            'instance': instance,
            'object_name': obj_name or 'T%d' % obj_type,
            'fields': fields
        })
    except subprocess.TimeoutExpired:
        return jsonify({'error': '读取超时'}), 500
    except Exception as e:
        return jsonify({'error': str(e)}), 500


@app.route('/api/write-object', methods=['POST'])
def api_write_object():
    """
    向设备写入单个对象（替换指定寄存器）
    调用 mxt-app -W -T OBJ -I INST 0xXX 0xXX ...
    """
    data = request.get_json() or {}
    obj_type = int(data.get('object_type', 0))
    instance = int(data.get('instance', 0))
    bytes_data = data.get('data', [])
    if obj_type <= 0:
        return jsonify({'error': '请提供 object_type'}), 400
    if not bytes_data:
        return jsonify({'error': '请提供 data（字节数组）'}), 400

    # 转为十六进制参数（mxt-app 的 mxt_convert_hex 只接受纯 hex 字符串，不接受 0x 前缀）
    hex_args = []

    def _to_u8(v):
        if isinstance(v, int):
            return v & 0xFF
        if isinstance(v, str):
            s = v.strip()
            if not s:
                raise ValueError('空字符串')
            # 支持 "0xNN" / "NN" / "123"，但最终都归一成 0..255
            if s.lower().startswith('0x'):
                return int(s, 16) & 0xFF
            # 如果包含任何 a-f/A-F，按 hex 解析；否则按十进制解析
            if re.search(r'[a-fA-F]', s):
                return int(s, 16) & 0xFF
            return int(s, 10) & 0xFF
        raise ValueError(f'不支持的数据类型: {type(v)}')

    try:
        for b in bytes_data:
            u8 = _to_u8(b)
            # 关键：不要带 0x
            hex_args.append('%02X' % u8)
    except Exception as e:
        return jsonify({'success': False, 'error': f'写入数据格式错误: {e}'}), 400

    device = (data.get('device') or '').strip() or None

    try:
        cmd = ['-W', '-T', str(obj_type), '-I', str(instance)] + hex_args
        result = _run_mxt_app(cmd, device=device, timeout=15)

        if result['success']:
            return jsonify({'success': True, 'message': '写入完成'})
        else:
            return jsonify({
                'success': False,
                'error': result['stderr'] or result['stdout'] or '写入失败',
                'devices': result.get('devices'),
            }), 500
    except subprocess.TimeoutExpired:
        return jsonify({'error': '写入超时'}), 500
    except Exception as e:
        return jsonify({'error': str(e)}), 500


@app.route('/api/read-device', methods=['POST'])
def api_read_device():
    """
    从设备读取配置并保存为 xcfg
    调用 mxt-app --save 读取设备配置
    """
    filename = request.json.get('filename', 'device_config.xcfg') if request.is_json else 'device_config.xcfg'
    filename = secure_filename(filename) or 'device_config.xcfg'

    fd, tmp_path = tempfile.mkstemp(suffix='.xcfg', prefix='mxt_read_')
    os.close(fd)

    try:
        device = None

        # 允许前端显式指定 device（例如 usb:001-004），否则自动枚举并选择支持的 VID:PID。
        if request.is_json:
            device = (request.json.get('device') or '').strip() or None

        # 支持的 VID:PID 列表（小写、无 0x 前缀）
        supported_vidpid = {
            '0483:5740',
        }

        if not device:
            q = _run_mxt_app(['-q'], timeout=10)
            if not q['success']:
                return jsonify({
                    'success': False,
                    'error': q['stderr'] or q['stdout'] or '枚举设备失败'
                }), 500

            # 解析 -q 输出行：usb:001-004 0483:5740  ...
            candidates = []
            for line in (q['stdout'] or '').splitlines():
                s = line.strip()
                if not s:
                    continue
                m = re.match(r'^(usb:\d{3}-\d{3})\s+([0-9A-Fa-f]{4}:[0-9A-Fa-f]{4})\b', s)
                if not m:
                    continue
                dev_id = m.group(1)
                vidpid = m.group(2).lower()
                if vidpid in supported_vidpid:
                    candidates.append(dev_id)

            if len(candidates) == 1:
                device = candidates[0]
            elif len(candidates) == 0:
                return jsonify({
                    'success': False,
                    'error': '未找到支持的设备（支持 VID:PID: ' + ', '.join(sorted(supported_vidpid)) + '）'
                }), 404
            else:
                return jsonify({
                    'success': False,
                    'error': '检测到多个支持的设备，请指定 device 参数',
                    'devices': candidates
                }), 409

        # mxt-app --save FILE --format 3 保存为 xcfg
        result = _run_mxt_app(['-d', device, '--save', tmp_path, '--format', '3'], timeout=30)

        if not result['success']:
            return jsonify({
                'success': False,
                'error': result['stderr'] or result['stdout'] or f"退出码 {result['returncode']}"
            }), 500

        with open(tmp_path, 'r', encoding='utf-8', errors='replace') as f:
            content = f.read()

        data = parse_xcfg(content)
        return jsonify({'success': True, 'data': data, 'content': content})
    except subprocess.TimeoutExpired:
        return jsonify({'error': '读取超时'}), 500
    except Exception as e:
        return jsonify({'error': str(e)}), 500
    finally:
        try:
            os.unlink(tmp_path)
        except OSError:
            pass


@app.route('/api/info', methods=['POST'])
def api_info():
    """读取信息块与配置 CRC（等价于交互菜单 I）"""
    try:
        # -i 会输出 info block + objects 列表；CRC 可通过 --info-crc 输出（不同版本可能不支持），这里用交互实现不了，所以先返回 -i 输出
        result = _run_mxt_app(['--info'], timeout=20)
        if not result['success']:
            return jsonify({
                'success': False,
                'error': result['stderr'] or result['stdout'] or '读取失败',
                'stdout': result['stdout'],
                'stderr': result['stderr'],
            }), 500
        return jsonify({'success': True, 'stdout': result['stdout']})
    except subprocess.TimeoutExpired:
        return jsonify({'error': '读取超时'}), 500


@app.route('/api/backup', methods=['POST'])
def api_backup():
    """备份配置到 NVM（等价于交互菜单 B）"""
    try:
        result = _run_mxt_app(['--backup'], timeout=30)
        if not result['success']:
            return jsonify({
                'success': False,
                'error': result['stderr'] or result['stdout'] or '备份失败',
                'stdout': result['stdout'],
                'stderr': result['stderr'],
            }), 500
        return jsonify({'success': True, 'stdout': result['stdout']})
    except subprocess.TimeoutExpired:
        return jsonify({'error': '备份超时'}), 500


@app.route('/api/reset', methods=['POST'])
def api_reset():
    """复位设备（等价于交互菜单 R）"""
    try:
        result = _run_mxt_app(['--reset'], timeout=20)
        if not result['success']:
            return jsonify({
                'success': False,
                'error': result['stderr'] or result['stdout'] or '复位失败',
                'stdout': result['stdout'],
                'stderr': result['stderr'],
            }), 500
        return jsonify({'success': True, 'stdout': result['stdout']})
    except subprocess.TimeoutExpired:
        return jsonify({'error': '复位超时'}), 500


@app.route('/api/calibrate', methods=['POST'])
def api_calibrate():
    """校准设备（等价于交互菜单 C）"""
    try:
        result = _run_mxt_app(['--calibrate'], timeout=30)
        if not result['success']:
            return jsonify({
                'success': False,
                'error': result['stderr'] or result['stdout'] or '校准失败',
                'stdout': result['stdout'],
                'stderr': result['stderr'],
            }), 500
        return jsonify({'success': True, 'stdout': result['stdout']})
    except subprocess.TimeoutExpired:
        return jsonify({'error': '校准超时'}), 500


@app.route('/api/send-command', methods=['POST'])
def api_send_command():
    """发送一次字符串指令（默认 mode0）"""
    data = request.get_json() or {}
    command = (data.get('command') or 'mode0').strip()
    if not command:
        return jsonify({'error': 'command 不能为空'}), 400

    try:
        args = command.split()
        result = _run_mxt_app(args, timeout=20)
        if not result['success']:
            return jsonify({
                'success': False,
                'error': result['stderr'] or result['stdout'] or '发送失败',
                'stdout': result['stdout'],
                'stderr': result['stderr'],
            }), 500
        return jsonify({'success': True, 'stdout': result['stdout']})
    except subprocess.TimeoutExpired:
        return jsonify({'error': '发送超时'}), 500
    except Exception as e:
        return jsonify({'error': str(e)}), 500


@app.route('/api/dump', methods=['POST'])
def api_dump():
    """导出诊断数据（等价于交互菜单 U），用命令行参数方式实现"""
    data = request.get_json() or {}

    mode = (data.get('mode') or '').lower()  # m/s/k/a
    kind = (data.get('kind') or '').lower()  # d/r/s
    filename = data.get('filename') or 'data.csv'
    instance = int(data.get('instance', 0))
    frames = int(data.get('frames', 1))
    fmt = int(data.get('format', 0))

    if mode not in {'m', 's', 'k', 'a'}:
        return jsonify({'error': 'mode 仅支持 m/s/k/a'}), 400

    args = ['--debug-dump', filename, '--frames', str(frames), '--instance', str(instance), '--format', str(fmt)]

    if mode == 'm':
        # mutual: 默认 deltas，可加 --references
        if kind == 'r':
            args.append('--references')
        elif kind != 'd':
            return jsonify({'error': 'mutual kind 仅支持 d/r'}), 400
    elif mode == 's':
        if kind == 'd':
            args.append('--self-cap-deltas')
        elif kind == 'r':
            args.append('--self-cap-refs')
        else:
            return jsonify({'error': 'self kind 仅支持 d/r'}), 400
    elif mode == 'k':
        if kind == 'd':
            args.append('--key-array-deltas')
        elif kind == 'r':
            args.append('--key-array-refs')
        elif kind == 's':
            args.append('--key-array-signals')
        else:
            return jsonify({'error': 'key kind 仅支持 d/r/s'}), 400
    elif mode == 'a':
        if kind == 'd':
            args.append('--active-stylus-deltas')
        elif kind == 'r':
            args.append('--active-stylus-refs')
        else:
            return jsonify({'error': 'stylus kind 仅支持 d/r'}), 400

    try:
        result = _run_mxt_app(args, timeout=120)
        if not result['success']:
            return jsonify({
                'success': False,
                'error': result['stderr'] or result['stdout'] or '导出失败',
                'stdout': result['stdout'],
                'stderr': result['stderr'],
            }), 500
        return jsonify({'success': True, 'stdout': result['stdout'], 'filename': filename})
    except subprocess.TimeoutExpired:
        return jsonify({'error': '导出超时'}), 500


def _build_debug_dump_args(mode: str, kind: str, frames: int, instance: int, fmt: int):
    args = ['--debug-dump', '-', '--frames', str(frames), '--instance', str(instance), '--format', str(fmt)]

    if mode == 'm':
        if kind == 'r':
            args.append('--references')
        else:
            # default deltas
            pass
    elif mode == 's':
        if kind == 'd':
            args.append('--self-cap-deltas')
        elif kind == 'r':
            args.append('--self-cap-refs')
    elif mode == 'k':
        if kind == 'd':
            args.append('--key-array-deltas')
        elif kind == 'r':
            args.append('--key-array-refs')
        elif kind == 's':
            args.append('--key-array-signals')
    elif mode == 'a':
        if kind == 'd':
            args.append('--active-stylus-deltas')
        elif kind == 'r':
            args.append('--active-stylus-refs')

    return args


def _u16_to_i16(v: int) -> int:
    """将可能是无符号 16 位数值转换为有符号 int16。"""
    v = int(v) & 0xFFFF
    return v - 0x10000 if (v & 0x8000) else v


def _convert_mutual_refs_csv_to_signed(content: str) -> str:
    """
    针对 Mutual Reference/Refs 输出，将每一行数据中的数值列转换为有符号 int16。
    仅处理形如 "HH:MM:SS(.xxx),frame,..." 的数据行，其余行原样保留。
    """
    if not content:
        return content

    lines = content.splitlines()
    out_lines = []
    time_re = re.compile(r'^\d{2}:\d{2}:\d{2}(?:\.\d+)?$')

    for line in lines:
        raw = line.rstrip('\r\n')
        if not raw:
            out_lines.append(raw)
            continue

        cols = raw.split(',')
        if len(cols) < 3:
            out_lines.append(raw)
            continue

        first = cols[0].strip()
        if not time_re.match(first):
            # 非数据行（例如 header），原样返回
            out_lines.append(raw)
            continue

        # 从第三列开始做 uint16->int16 转换
        for i in range(2, len(cols)):
            s = cols[i].strip()
            if not s:
                continue
            try:
                v = _u16_to_i16(int(s, 10))
            except ValueError:
                # 不是纯十进制数字则跳过
                continue
            cols[i] = str(v)

        out_lines.append(','.join(cols))

    return '\n'.join(out_lines)


@app.route('/api/diag/capture', methods=['POST'])
def api_diag_capture():
    """采集一次诊断数据到 stdout 并缓存为 latest。"""
    data = request.get_json() or {}
    mode = (data.get('mode') or 'm').lower()
    kind = (data.get('kind') or 'd').lower()
    frames = int(data.get('frames', 1))
    instance = int(data.get('instance', 0))
    fmt = int(data.get('format', 0))

    if mode not in {'m', 's', 'k', 'a'}:
        return jsonify({'error': 'mode 仅支持 m/s/k/a'}), 400

    args = _build_debug_dump_args(mode, kind, frames, instance, fmt)

    try:
        result = _run_mxt_app(args, timeout=max(30, frames * 10))
        if not result['success']:
            return jsonify({
                'success': False,
                'error': result['stderr'] or result['stdout'] or '采集失败',
                'stdout': result['stdout'],
                'stderr': result['stderr'],
            }), 500

        content = result['stdout'] or ''
        # 对 Mutual Reference/Refs 在服务端就做一次有符号 int16 归一，避免前端/日志看到一堆 655xx。
        if mode == 'm' and kind == 'r':
            content = _convert_mutual_refs_csv_to_signed(content)

        _DIAG_LATEST['content'] = content
        _DIAG_LATEST['ts'] = int(__import__('time').time() * 1000)
        _DIAG_LATEST['params'] = {
            'mode': mode,
            'kind': kind,
            'frames': frames,
            'instance': instance,
            'format': fmt,
        }

        return jsonify({'success': True, 'content': _DIAG_LATEST['content'], 'ts': _DIAG_LATEST['ts'], 'params': _DIAG_LATEST['params']})
    except subprocess.TimeoutExpired:
        return jsonify({'error': '采集超时'}), 500


@app.route('/api/diag/latest', methods=['GET'])
def api_diag_latest():
    """给 Kimi 前端读取的 latest 数据。"""
    return jsonify({'success': True, 'content': _DIAG_LATEST['content'], 'ts': _DIAG_LATEST['ts'], 'params': _DIAG_LATEST['params']})


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', default='0.0.0.0', help='监听地址（0.0.0.0 允许外网/虚拟机桥接访问）')
    parser.add_argument('--port', type=int, default=5000, help='端口（若 5000 被占用可改为 5001）')
    parser.add_argument('--debug', action='store_true')
    args = parser.parse_args()
    # threaded=True 避免单请求阻塞导致代理返回 502
    app.run(host=args.host, port=args.port, debug=args.debug, threaded=True)
