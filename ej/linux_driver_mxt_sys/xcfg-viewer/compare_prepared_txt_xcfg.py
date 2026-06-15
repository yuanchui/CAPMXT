#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Compare prepared TXT (exported from prepared.bin) with XCFG content.

Usage:
  python compare_prepared_txt_xcfg.py <prepared_txt> <xcfg_file>

Example:
  python compare_prepared_txt_xcfg.py xcfg/config1-prepared.txt xcfg/config1.xcfg
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Tuple


@dataclass
class ObjMeta:
    address: int
    size: int


@dataclass
class XcfgObject:
    index: int
    header: str
    address: int
    size: int
    fields: List[Tuple[int, int, int]]  # (offset, length, value)


def parse_int(s: str) -> int:
    s = (s or "").strip()
    if s.lower().startswith("0x"):
        return int(s, 16)
    return int(s, 10)


def parse_prepared_txt(path: Path) -> Tuple[List[ObjMeta], List[bytes]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()

    metas: Dict[int, ObjMeta] = {}
    obj_bytes: Dict[int, List[int]] = {}

    in_meta = False
    in_bytes = False
    current_obj = None

    meta_addr_re = re.compile(r"^OBJECT\[(\d+)\]\.ADDRESS=(\d+)$")
    meta_size_re = re.compile(r"^OBJECT\[(\d+)\]\.SIZE=(\d+)$")
    obj_re = re.compile(r"^OBJECT\[(\d+)\]\s+SIZE=(\d+)$")

    for raw in lines:
        line = raw.strip()
        if not line:
            continue

        if line == "[OBJECTS_META]":
            in_meta = True
            in_bytes = False
            continue
        if line == "[OBJECT_BYTES_HEX]":
            in_meta = False
            in_bytes = True
            current_obj = None
            continue
        if line.startswith("[") and line.endswith("]"):
            in_meta = False
            in_bytes = False
            current_obj = None
            continue

        if in_meta:
            m = meta_addr_re.match(line)
            if m:
                idx = int(m.group(1))
                addr = int(m.group(2))
                old = metas.get(idx)
                metas[idx] = ObjMeta(address=addr, size=(old.size if old else 0))
                continue
            m = meta_size_re.match(line)
            if m:
                idx = int(m.group(1))
                size = int(m.group(2))
                old = metas.get(idx)
                metas[idx] = ObjMeta(address=(old.address if old else 0), size=size)
                continue

        if in_bytes:
            m = obj_re.match(line)
            if m:
                current_obj = int(m.group(1))
                obj_bytes.setdefault(current_obj, [])
                continue
            if current_obj is None:
                continue
            # Hex byte row: "AA BB CC ..."
            parts = line.split()
            for p in parts:
                if re.fullmatch(r"[0-9A-Fa-f]{2}", p):
                    obj_bytes[current_obj].append(int(p, 16))

    if not metas:
        raise ValueError("prepared txt 未解析到 OBJECTS_META")
    if not obj_bytes:
        raise ValueError("prepared txt 未解析到 OBJECT_BYTES_HEX")

    max_idx = max(metas.keys())
    metas_list: List[ObjMeta] = []
    bytes_list: List[bytes] = []
    for i in range(max_idx + 1):
        if i not in metas:
            raise ValueError(f"prepared txt 缺少 OBJECT[{i}] 元数据")
        meta = metas[i]
        b = bytes(obj_bytes.get(i, []))
        if len(b) != meta.size:
            raise ValueError(
                f"prepared txt OBJECT[{i}] 字节长度不匹配: expect={meta.size}, got={len(b)}"
            )
        metas_list.append(meta)
        bytes_list.append(b)

    return metas_list, bytes_list


def parse_xcfg(path: Path) -> List[XcfgObject]:
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    objs: List[XcfgObject] = []

    section_re = re.compile(r"^\[(.+)\]$")
    t_re = re.compile(r"T(\d+)", re.IGNORECASE)
    inst_re = re.compile(r"INSTANCE\s+(\d+)", re.IGNORECASE)
    field_re = re.compile(r"^\s*(\d+)\s+(\d+)\s+.+=(.+)\s*$")

    i = 0
    while i < len(lines):
        line = lines[i].strip()
        m = section_re.match(line)
        if not m:
            i += 1
            continue
        section = m.group(1).strip()
        if not (t_re.search(section) and inst_re.search(section)):
            i += 1
            continue

        address = 0
        size = 0
        fields: List[Tuple[int, int, int]] = []
        i += 1
        while i < len(lines):
            cur = lines[i].strip()
            if cur.startswith("["):
                break
            if cur.startswith("OBJECT_ADDRESS="):
                address = parse_int(cur.split("=", 1)[1])
            elif cur.startswith("OBJECT_SIZE="):
                size = parse_int(cur.split("=", 1)[1])
            else:
                fm = field_re.match(cur)
                if fm:
                    off = int(fm.group(1))
                    ln = int(fm.group(2))
                    val = parse_int(fm.group(3).strip())
                    fields.append((off, ln, val))
            i += 1

        objs.append(
            XcfgObject(
                index=len(objs),
                header=section,
                address=address,
                size=size,
                fields=fields,
            )
        )

    if not objs:
        raise ValueError("xcfg 未解析到对象")
    return objs


def build_obj_bytes(size: int, fields: List[Tuple[int, int, int]]) -> bytes:
    buf = bytearray(size)
    for offset, length, value in fields:
        v = value & 0xFFFFFFFF
        for k in range(length):
            idx = offset + k
            if 0 <= idx < size:
                buf[idx] = (v >> (8 * k)) & 0xFF
    return bytes(buf)


def compare(prepared_txt: Path, xcfg_file: Path) -> int:
    metas, prepared_bytes = parse_prepared_txt(prepared_txt)
    xcfg_objs = parse_xcfg(xcfg_file)

    errors: List[str] = []
    warns: List[str] = []

    if len(metas) != len(xcfg_objs):
        errors.append(f"对象数量不同: prepared={len(metas)} xcfg={len(xcfg_objs)}")

    n = min(len(metas), len(xcfg_objs))
    mismatch_count = 0
    total_diff_bytes = 0

    for i in range(n):
        meta = metas[i]
        xo = xcfg_objs[i]

        if meta.address != xo.address:
            errors.append(
                f"OBJECT[{i}] 地址不一致: prepared={meta.address} xcfg={xo.address} ({xo.header})"
            )
        if meta.size != xo.size:
            errors.append(
                f"OBJECT[{i}] 大小不一致: prepared={meta.size} xcfg={xo.size} ({xo.header})"
            )

        xcfg_b = build_obj_bytes(xo.size, xo.fields)
        pre_b = prepared_bytes[i]

        m = min(len(pre_b), len(xcfg_b))
        diff_positions = [p for p in range(m) if pre_b[p] != xcfg_b[p]]
        if len(pre_b) != len(xcfg_b):
            warns.append(
                f"OBJECT[{i}] 字节长度不同: prepared={len(pre_b)} xcfg={len(xcfg_b)} ({xo.header})"
            )
        if diff_positions:
            mismatch_count += 1
            total_diff_bytes += len(diff_positions)
            preview = ", ".join(
                f"{p}: {pre_b[p]:02X}!={xcfg_b[p]:02X}" for p in diff_positions[:8]
            )
            if len(diff_positions) > 8:
                preview += ", ..."
            warns.append(
                f"OBJECT[{i}] 内容不一致 ({xo.header}) diff_bytes={len(diff_positions)} [{preview}]"
            )

    print("=== Compare Result ===")
    print(f"prepared_txt: {prepared_txt}")
    print(f"xcfg_file    : {xcfg_file}")
    print(f"objects_cmp  : {n}")
    print(f"obj_mismatch : {mismatch_count}")
    print(f"byte_diff_sum: {total_diff_bytes}")
    print()

    if errors:
        print("[ERRORS]")
        for e in errors:
            print(f"- {e}")
        print()

    if warns:
        print("[DETAILS]")
        for w in warns:
            print(f"- {w}")
        print()

    if not errors and mismatch_count == 0:
        print("PASS: prepared.txt 与 xcfg 按对象字节完全一致。")
        return 0

    print("FAIL: 存在不一致（见上方详情）。")
    return 2


def main() -> int:
    if len(sys.argv) != 3:
        print("Usage: python compare_prepared_txt_xcfg.py <prepared_txt> <xcfg_file>")
        return 1
    prepared_txt = Path(sys.argv[1]).expanduser().resolve()
    xcfg_file = Path(sys.argv[2]).expanduser().resolve()
    if not prepared_txt.exists():
        print(f"文件不存在: {prepared_txt}")
        return 1
    if not xcfg_file.exists():
        print(f"文件不存在: {xcfg_file}")
        return 1
    try:
        return compare(prepared_txt, xcfg_file)
    except Exception as e:
        print(f"执行失败: {e}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

