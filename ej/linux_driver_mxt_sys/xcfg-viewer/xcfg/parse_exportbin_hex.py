import argparse
import re
from pathlib import Path

EMBEDDED_HEX_STREAM = """
A0 A6 01 10 AA 28 A1 25 00 FA 00 00 00 38 10 09 00 00 FF FF 00 00 00 00 FF FF FF FF 00 00 02 00 02 00 FE FF FF FF 00 00 01 00 00 00 00 00 FF FF 00 00 00 00 00 00 01 00 02 00 03 00 01 00 FF FF FD FF 00 00 FC FF A1 25 00 FA 00 38 00 38 00 00 01 00 00 00 01 00 00 00 00 00 FF FF 02 00 01 00 00 00 01 00 FF FF FB FF 00 00 01 00 FF FF FE FF 00 00 00 00 FE FF 01 00 00 00 FE FF FF FF 00 00 FE FF FF FF FF FF A1 25 00 FA 00 70 00 12 FF FF FF FF FF FF 00 00 00 00 01 00 FE FF 00 00 FF FF A1 2C 00 7C 01 00 00 01 0C A1 05 00 7D 01 00 00 0B 2D 59 26 02 7F 02 00 00 00 00 2E A1 06 00 88 01 00 00 07 00 00 00 00 00 00 00 A1 44 00 8F 01 00 00 38 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 A1 44 00 8F 01 38 00 11 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 A1 26 00 D8 01 00 00 38 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 A1 26 00 D8 01 38 00 08 00 00 00 00 00 00 00 00 A1 47 00 18 02 00 00 38 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 A1 47 00 18 02 38 00 38 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 A1 47 00 18 02 70 00 38 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 A1 47 00 18 02 A8 00 20 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 A1 6E 00 E0 02 00 00 28 86 00 8D 00 8C 00 94 00 8D 00 93 00 93 00 95 00 99 00 96 00 8F 00 82 00 8F 00 8C 00 99 00 8C 00 93 00 85 00 89 00 69 00 A1 6E 01 08 03 00 00 28 8C 00 93 00 90 00 94 00 8E 00 92 00 85 00 90 00 92 00 8E 00 89 00 73 00 69 00 93 00 95 00 87 00 87 00 7A 00 00 00 00 00 A1 6E 02 30 03 00 00 28 A4 00 A6 00 A6 00 A8 00 A5 00 00 00 A2 00 9E 00 A5 00 A0 00 A1 00 83 00 81 00 A6 00 A4 00 99 00 96 00 8A 00 52 00 00 00 A1 6E 03 58 03 00 00 28 94 00 8B 00 8E 00 96 00 9D 00 8C 00 91 00 8F 00 9C 00 9F 00 96 00 7D 00 84 00 96 00 9A 00 81 00 84 00 82 00 78 00 5F 00 A1 6E 04 80 03 00 00 28 97 00 91 00 91 00 96 00 9B 00 8C 00 83 00 8D 00 95 00 96 00 8D 00 71 00 63 00 96 00 95 00 7D 00 7D 00 78 00 00 00 00 00 A1 6E 05 A8 03 00 00 28 AF 00 A5 00 A6 00 AC 00 B0 00 00 00 A1 00 A1 00 AD 00 AE 00 A8 00 81 00 7A 00 A9 00 AA 00 96 00 8D 00 91 00 4E 00 00 00 A1 6E 06 D0 03 00 00 28 94 00 8B 00 8E 00 96 00 9D 00 8C 00 91 00 8F 00 9C 00 9F 00 96 00 7D 00 84 00 96 00 9A 00 81 00 84 00 82 00 78 00 5F 00 A1 6E 07 F8 03 00 00 28 97 00 91 00 91 00 96 00 9B 00 8C 00 83 00 8D 00 95 00 96 00 8D 00 71 00 63 00 96 00 95 00 7D 00 7D 00 78 00 00 00 00 00 A1 6E 08 20 04 00 00 28 AF 00 A5 00 A6 00 AC 00 B0 00 00 00 A1 00 A1 00 AD 00 AE 00 A8 00 81 00 7A 00 A9 00 AA 00 96 00 8D 00 91 00 4E 00 00 00 A1 6E 09 48 04 00 00 28 0C 00 0C 00 0C 00 0C 00 0C 00 0C 00 0C 00 0C 00 0C 00 0C 00 0C 00 0C 00 0C 00 0C 00 0C 00 0C 00 0C 00 0C 00 0C 00 0D 00 A1 6E 0A 70 04 00 00 28 F9 00 F9 00 00 00 F9 00 00 00 F9 00 F9 00 F9 00 F9 00 F9 00 F9 00 F9 00 F9 00 F9 00 F9 00 F9 00 F9 00 F9 00 00 00 00 00 A1 6E 0B 98 04 00 00 28 F9 00 00 00 F9 00 00 00 F9 00 00 00 F9 00 F9 00 F9 00 F9 00 F9 00 F9 00 F9 00 F9 00 F9 00 F9 00 F9 00 F9 00 F9 00 00 00 A1 07 00 C0 04 00 00 05 20 FF 0A 42 00 A1 08 00 C5 04 00 00 0F 22 00 14 14 00 00 00 00 00 00 01 01 01 01 80 A1 0F 00 D4 04 00 00 0B 00 20 00 01 03 00 05 3C 02 0A 00 A1 12 00 DF 04 00 00 02 00 00 A1 13 00 E1 04 00 00 06 01 00 00 3F 00 00 A1 19 00 E7 04 00 00 10 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 A1 28 00 F7 04 00 00 07 00 00 00 00 00 00 00 A1 2A 00 FE 04 00 00 0E 00 00 00 00 00 00 00 00 00 00 00 00 00 00 A1 2B 00 0C 05 00 00 11 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 A1 2E 00 1D 05 00 00 0C 00 00 14 14 00 00 00 00 00 00 00 00 A1 2F 00 29 05 00 00 2F 00 00 23 FF 04 16 00 BE 00 80 20 10 00 00 00 00 00 00 00 00 00 00 BC 12 A8 1F 00 00 B8 27 BC 11 BF 26 BC 1F 03 03 C8 41 00 00 00 00 00 00 00 A1 38 00 58 05 00 00 24 01 00 01 1B 03 03 03 03 03 03 03 03 03 03 03 03 03 03 03 03 03 03 03 03 03 03 03 03 03 03 03 03 03 03 03 03 A1 3D 00 7C 05 00 00 05 00 00 00 00 00 A1 3D 01 81 05 00 00 05 00 00 00 00 00 A1 3D 02 86 05 00 00 05 00 00 00 00 00 A1 3D 03 8B 05 00 00 05 00 00 00 00 00 A1 3D 04 90 05 00 00 05 00 00 00 00 00 A1 3D 05 95 05 00 00 05 00 00 00 00 00 A1 41 00 9A 05 00 00 17 00 03 00 00 00 00 00 00 00 00 06 00 00 00 00 00 00 21 00 00 00 00 00 A1 41 01 B1 05 00 00 17 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 A1 41 02 C8 05 00 00 17 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 A1 46 00 DF 05 00 00 0A 00 00 00 00 00 00 00 00 00 00 A1 46 01 E9 05 00 00 0A 00 00 00 00 00 00 00 00 00 00 A1 46 02 F3 05 00 00 0A 00 00 00 00 00 00 00 00 00 00 A1 46 03 FD 05 00 00 0A 00 00 00 00 00 00 00 00 00 00 A1 46 04 07 06 00 00 0A 00 00 00 00 00 00 00 00 00 00 A1 46 05 11 06 00 00 0A 00 00 00 00 00 00 00 00 00 00 A1 46 06 1B 06 00 00 0A 00 00 00 00 00 00 00 00 00 00 A1 46 07 25 06 00 00 0A 00 00 00 00 00 00 00 00 00 00 A1 46 08 2F 06 00 00 0A 00 00 00 00 00 00 00 00 00 00 A1 46 09 39 06 00 00 0A 00 00 00 00 00 00 00 00 00 00 A1 46 0A 43 06 00 00 0A 00 00 00 00 00 00 00 00 00 00 A1 46 0B 4D 06 00 00 0A 00 00 00 00 00 00 00 00 00 00 A1 46 0C 57 06 00 00 0A 00 00 00 00 00 00 00 00 00 00 A1 46 0D 61 06 00 00 0A 00 00 00 00 00 00 00 00 00 00 A1 46 0E 6B 06 00 00 0A 00 00 00 00 00 00 00 00 00 00 A1 46 0F 75 06 00 00 0A 00 00 00 00 00 00 00 00 00 00 A1 46 10 7F 06 00 00 0A 00 00 00 00 00 00 00 00 00 00 A1 46 11 89 06 00 00 0A 00 00 00 00 00 00 00 00 00 00 A1 46 12 93 06 00 00 0A 00 00 00 00 00 00 00 00 00 00 A1 46 13 9D 06 00 00 0A 00 00 00 00 00 00 00 00 00 00 A1 48 00 A7 06 00 00 38 01 00 00 01 00 FF 01 01 00 06 16 16 02 05 32 82 00 0B 00 1D 06 18 00 0B 11 80 1C 1C 1C 1C 10 12 12 12 12 10 00 00 0C 00 0F 18 00 0B 11 80 22 28 26 24 16 14 14 14 14 14 A1 48 00 A7 06 38 00 21 00 03 20 E3 0F 18 00 0B 11 80 26 2C 2A 28 1A 18 18 18 18 18 00 04 00 E3 00 00 00 05 06 3F 3F 3F 3F A1 4D 00 00 07 00 00 02 05 02 A1 4E 00 02 07 00 00 0C 85 05 02 0A 0A 02 24 00 8C 00 00 00 A1 4F 00 0E 07 00 00 04 00 00 00 00 A1 4F 01 12 07 00 00 04 00 00 00 00 A1 4F 02 16 07 00 00 04 00 00 00 00 A1 50 00 1A 07 00 00 0E 10 01 C8 96 1E C5 00 64 01 45 00 0A 00 02 A1 51 00 28 07 00 00 12 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 A1 51 01 3A 07 00 00 12 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 A1 5D 00 4C 07 00 00 1E 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 A1 64 00 6A 07 00 00 38 8F 20 00 00 00 00 0A 88 00 20 27 03 03 7F 07 38 14 1E 2C 00 14 27 06 06 37 04 36 22 0B 07 41 0A 08 00 00 14 3C 05 00 02 02 01 00 08 42 DC 64 0A 00 03 00 00 00 02 B8 14 A1 64 00 6A 07 38 00 06 9E 2C B3 22 00 00 A1 68 00 A8 07 00 00 0B 00 10 32 0A 0A 02 0F 32 0A 0A 02 A1 6C 00 B3 07 00 00 38 00 00 00 01 05 0B 00 35 04 02 00 00 01 00 39 18 65 00 A1 28 30 20 30 18 28 30 20 30 18 00 00 0F 00 01 39 18 65 00 A1 30 38 28 38 20 30 38 28 38 20 00 04 14 22 01 39 18 A1 6C 00 B3 07 38 00 13 65 00 A1 40 48 38 50 28 40 48 38 50 28 00 04 00 22 00 00 A1 6D 00 FE 07 00 00 09 02 00 05 00 00 00 00 00 03 A1 6F 00 07 08 00 00 1E 00 00 32 46 30 30 0A 03 00 FF 00 00 00 00 00 00 00 00 28 00 33 46 00 00 00 00 00 19 19 32 A1 6F 01 25 08 00 00 1E 00 00 32 32 20 20 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 A1 70 00 43 08 00 00 05 00 3A 3A 26 26 A1 71 00 48 08 00 00 03 00 03 02 A1 73 00 4B 08 00 00 14 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 A1 74 00 5F 08 00 00 38 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 A1 74 00 5F 08 38 00 38 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 A1 74 00 5F 08 70 00 38 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 A1 74 00 5F 08 A8 00 38 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 A1 74 00 5F 08 E0 00 1F 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 A1 79 00 5E 09 00 00 03 00 00 00 A2 00 
"""


def parse_hex_stream(text: str):
    bs = bytes(int(x, 16) for x in re.findall(r"\b[0-9A-Fa-f]{2}\b", text))
    i = 0
    family = variant = version = build = n_obj = None
    objects = {}  # (type,inst,addr)->bytearray
    status = None

    while i < len(bs):
        cmd = bs[i]
        if cmd == 0xA0:
            if i + 6 > len(bs):
                raise ValueError("A0 frame truncated")
            family, variant, version, build, n_obj = bs[i + 1 : i + 6]
            i += 6
        elif cmd == 0xA1:
            if i + 8 > len(bs):
                raise ValueError("A1 header truncated")
            obj_type = bs[i + 1]
            inst = bs[i + 2]
            addr = bs[i + 3] | (bs[i + 4] << 8)
            off = bs[i + 5] | (bs[i + 6] << 8)
            ln = bs[i + 7]
            if i + 8 + ln > len(bs):
                raise ValueError("A1 payload truncated")
            payload = bs[i + 8 : i + 8 + ln]
            key = (obj_type, inst, addr)
            buf = objects.setdefault(key, bytearray())
            need = off + ln
            if len(buf) < need:
                buf.extend(b"\x00" * (need - len(buf)))
            buf[off : off + ln] = payload
            i += 8 + ln
        elif cmd == 0xA2:
            if i + 2 > len(bs):
                raise ValueError("A2 frame truncated")
            status = bs[i + 1]
            i += 2
            break
        else:
            # 跳过无效字节，兼容日志中混入文本
            i += 1

    if family is None:
        raise ValueError("No A0 frame found")
    if status is None:
        raise ValueError("No A2 frame found")
    return {
        "family": family,
        "variant": variant,
        "version": version,
        "build": build,
        "n_obj": n_obj,
        "status": status,
        "objects": objects,
    }


# maXTouch 对象类型名称映射 (与 config1.xcfg 一致)
OBJECT_TYPE_NAMES = {
    5: "消息处理器",
    6: "命令处理器",
    7: "功耗配置",
    8: "采集配置",
    15: "触摸按键阵列",
    18: "通信配置",
    19: "GPIO/PWM",
    25: "自检",
    26: "用户数据",
    37: "调试诊断",
    38: "用户数据",
    40: "握持抑制",
    42: "触摸抑制",
    43: "数字化仪",
    44: "消息计数",
    46: "CTE配置",
    47: "触摸笔",
    56: "无屏蔽层",
    61: "定时器",
    65: "Lens Bending",
    68: "串行数据命令",
    70: "动态配置控制器",
    71: "动态配置容器",
    72: "噪声抑制",
    77: "CTE扫描配置",
    78: "手套检测",
    79: "触摸事件触发",
    80: "重传补偿",
    81: "解锁手势",
    93: "触摸序列记录",
    100: "多点触摸屏",
    104: "辅助触摸配置",
    108: "自容噪声抑制",
    109: "自容全局配置",
    110: "自容调谐参数",
    111: "自容配置",
    112: "自容握持抑制",
    113: "接近测量配置",
    115: "符号手势",
    116: "符号手势配置",
    121: "传感器校正",
}

# 参考 config1.xcfg：以下对象通常不参与配置文件导出
NON_CONFIG_OBJECT_TYPES = {5, 6, 37, 44}


def write_prepared(result, out_path: Path):
    """输出格式与 config1.xcfg 完全一致"""
    items = sorted(result["objects"].items(), key=lambda kv: (kv[0][2], kv[0][0], kv[0][1]))
    # 跳过不参与配置导出的对象，保证与参考 xcfg 结构一致
    items = [it for it in items if it[0][0] not in NON_CONFIG_OBJECT_TYPES]
    with out_path.open("w", encoding="utf-8", newline="\n") as f:
        # [VERSION_INFO_HEADER] - 与 config1.xcfg 一致
        f.write("[VERSION_INFO_HEADER]\n")
        f.write(f"BUILD={result['build']}\n")
        f.write("CHECKSUM=0x0\n")  # bin 流无校验和，占位
        f.write(f"FAMILY_ID={result['family']}\n")
        f.write("INFO_BLOCK_CHECKSUM=0x0\n")  # bin 流无，占位
        f.write(f"VARIANT={result['variant']}\n")
        f.write(f"VERSION={result['version']}\n")
        f.write("\n")

        # [APPLICATION_INFO_HEADER] - 与 config1.xcfg 一致
        f.write("[APPLICATION_INFO_HEADER]\n")
        f.write("NAME=libmaxtouch\n")
        f.write("VERSION=1.46\n")
        f.write("\n")

        # 各对象: [T{type}-{name} INSTANCE {inst}] / OBJECT_ADDRESS / OBJECT_SIZE / k 1 UNKNOWN[k]=v
        for (obj_type, inst, addr), data in items:
            name = OBJECT_TYPE_NAMES.get(obj_type, "OBJECT")
            f.write(f"[T{obj_type}-{name} INSTANCE {inst}]\n")
            f.write(f"OBJECT_ADDRESS={addr}\n")
            f.write(f"OBJECT_SIZE={len(data)}\n")
            for k in range(len(data)):
                f.write(f"{k} 1 UNKNOWN[{k}]={data[k]}\n")
            f.write("\n")


def main():
    ap = argparse.ArgumentParser(description="Parse A0/A1/A2 hex stream to xcfg format (same as config1.xcfg)")
    ap.add_argument("input", nargs="?", help="input text file with hex bytes")
    ap.add_argument(
        "-o",
        "--output",
        default="config1-from-bin.xcfg",
        help="output xcfg file path",
    )
    ap.add_argument(
        "--from-var",
        action="store_true",
        help="parse hex from EMBEDDED_HEX_STREAM variable",
    )
    args = ap.parse_args()

    # 默认行为：未传 input 时，直接使用内置变量，便于“直接赋值运行”
    use_var = args.from_var or (args.input is None)

    if use_var:
        txt = EMBEDDED_HEX_STREAM
    else:
        txt = Path(args.input).read_text(encoding="utf-8", errors="ignore")

    result = parse_hex_stream(txt)
    out = Path(args.output)
    write_prepared(result, out)
    exported_count = sum(1 for k in result["objects"].keys() if k[0] not in NON_CONFIG_OBJECT_TYPES)
    print(f"ok: wrote {out}")
    print(
        f"meta: family={result['family']} variant={result['variant']} version={result['version']} build={result['build']} raw_objects={len(result['objects'])} exported_objects={exported_count} status={result['status']}"
    )


if __name__ == "__main__":
    main()
