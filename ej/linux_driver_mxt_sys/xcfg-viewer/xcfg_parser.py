#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
xcfg 配置文件解析器
解析 maXTouch .xcfg 格式，支持读取和写入
"""

import re
from typing import List, Dict, Any, Optional, Tuple


def parse_xcfg(content: str) -> Dict[str, Any]:
    """
    解析 xcfg 文件内容，返回结构化数据
    格式:
      [Txx-名称 INSTANCE n]
      OBJECT_ADDRESS=addr
      OBJECT_SIZE=size
      offset len name=value
      ...
    """
    result = {
        'header': {},
        'application_header': {},
        'objects': []
    }
    lines = content.split('\n')
    i = 0

    while i < len(lines):
        line = lines[i]
        stripped = line.strip()

        # 跳过空行
        if not stripped:
            i += 1
            continue

        # 解析 [SECTION] 头部
        if stripped.startswith('['):
            section_match = re.match(r'\[([^\]]+)\]', stripped)
            if section_match:
                section = section_match.group(1).strip()

                # 忽略 COMMENTS
                if section == 'COMMENTS':
                    i += 1
                    while i < len(lines) and not lines[i].strip().startswith('['):
                        i += 1
                    continue

                if section == 'APPLICATION_INFO_HEADER':
                    i += 1
                    while i < len(lines):
                        l = lines[i].strip()
                        if l.startswith('['):
                            break
                        if '=' in l:
                            k, _, v = l.partition('=')
                            result['application_header'][k.strip()] = v.strip()
                        i += 1
                    continue

                if section == 'VERSION_INFO_HEADER':
                    i += 1
                    while i < len(lines):
                        l = lines[i].strip()
                        if l.startswith('['):
                            break
                        if '=' in l:
                            k, _, v = l.partition('=')
                            result['header'][k.strip()] = v.strip()
                        i += 1
                    continue

                # 解析对象段 [Txx-名称 INSTANCE n]
                inst_match = re.search(r'INSTANCE\s+(\d+)', section, re.IGNORECASE)
                obj_match = re.search(r'T(\d+)', section, re.IGNORECASE)

                if inst_match and obj_match:
                    obj_id = int(obj_match.group(1))
                    instance = int(inst_match.group(1))
                    obj_name = section

                    obj = {
                        'header': obj_name,
                        'object_id': obj_id,
                        'instance': instance,
                        'object_address': 0,
                        'object_size': 0,
                        'fields': []
                    }

                    i += 1
                    # 读取 OBJECT_ADDRESS 和 OBJECT_SIZE
                    while i < len(lines):
                        l = lines[i].strip()
                        if l.startswith('['):
                            break
                        if l.startswith('OBJECT_ADDRESS='):
                            obj['object_address'] = int(l.split('=', 1)[1].strip())
                        elif l.startswith('OBJECT_SIZE='):
                            obj['object_size'] = int(l.split('=', 1)[1].strip())
                        elif l:
                            # 解析字段行: offset len name=value
                            field = _parse_field_line(l)
                            if field is not None:
                                obj['fields'].append(field)
                        i += 1

                    result['objects'].append(obj)
                    continue

            i += 1
            continue

        i += 1

    return result


def _parse_field_line(line: str) -> Optional[Dict[str, Any]]:
    """解析字段行: offset len name=value"""
    if not line or '=' not in line:
        return None
    # 格式: "0 1 UNKNOWN[0]=0" 或 "10 2 REGNAME=256"
    parts = line.split('=', 1)
    if len(parts) != 2:
        return None
    left, value_str = parts[0].strip(), parts[1].strip()
    tokens = left.split()
    if len(tokens) < 3:
        return None
    try:
        offset = int(tokens[0])
        length = int(tokens[1])
        name = ' '.join(tokens[2:])
        if value_str.startswith('0x') or value_str.startswith('0X'):
            value = int(value_str, 16)
        elif value_str.isdigit() or (value_str.startswith('-') and value_str[1:].isdigit()):
            value = int(value_str)
        else:
            value = 0
        return {
            'offset': offset,
            'length': length,
            'name': name,
            'value': value
        }
    except (ValueError, IndexError):
        return None


def serialize_xcfg(data: Dict[str, Any]) -> str:
    """
    将解析后的数据序列化回 xcfg 格式
    """
    lines = []

    # 写入 header（如果有）
    if data.get('header'):
        lines.append('[VERSION_INFO_HEADER]')
        for k, v in data['header'].items():
            lines.append(f'{k}={v}')
        lines.append('')

    # APPLICATION_INFO_HEADER
    app_header = data.get('application_header', {})
    if not app_header and data.get('header'):
        app_header = {
            'NAME': data['header'].get('NAME', 'libmaxtouch'),
            'VERSION': data['header'].get('VERSION', '1.0')
        }
    if app_header:
        lines.append('[APPLICATION_INFO_HEADER]')
        lines.append(f"NAME={app_header.get('NAME', 'libmaxtouch')}")
        lines.append(f"VERSION={app_header.get('VERSION', '1.0')}")
        lines.append('')

    # 写入对象
    for obj in data.get('objects', []):
        lines.append(f"[{obj['header']}]")
        lines.append(f"OBJECT_ADDRESS={obj['object_address']}")
        lines.append(f"OBJECT_SIZE={obj['object_size']}")
        for f in obj['fields']:
            val = f['value']
            if isinstance(val, str):
                try:
                    val = int(val, 0)
                except ValueError:
                    val = 0
            lines.append(f"{f['offset']} {f['length']} {f['name']}={val}")
        lines.append('')

    return '\n'.join(lines)


def fields_to_bytes(fields: List[Dict]) -> bytes:
    """将字段列表转换为字节数组（按 offset 写入）"""
    max_offset = 0
    for f in fields:
        end = f['offset'] + f['length']
        if end > max_offset:
            max_offset = end
    buf = bytearray(max_offset)
    for f in fields:
        val = f['value']
        if isinstance(val, str):
            try:
                val = int(val, 0)
            except ValueError:
                val = 0
        length = f['length']
        for i in range(length):
            buf[f['offset'] + i] = (val >> (i * 8)) & 0xFF
    return bytes(buf)


def bytes_to_fields(data: bytes, base_fields: List[Dict]) -> List[Dict]:
    """从字节数组更新字段值（保持 base_fields 结构）"""
    result = []
    for f in base_fields:
        fc = f.copy()
        offset = f['offset']
        length = f['length']
        if offset + length <= len(data):
            val = 0
            for i in range(length):
                val |= data[offset + i] << (i * 8)
            fc['value'] = val
        result.append(fc)
    return result
