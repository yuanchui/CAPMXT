#!/usr/bin/env python3
"""
串口接收速率检测：每秒统计接收字节数。

依赖: pip install pyserial

示例:
  python serial_rate_monitor.py --list
  python serial_rate_monitor.py COM3
  python serial_rate_monitor.py COM3 -b 115200 -i 1
"""

from __future__ import annotations

import argparse
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("请先安装 pyserial: pip install pyserial", file=sys.stderr)
    sys.exit(1)


def list_serial_ports() -> None:
    ports = list(list_ports.comports())
    if not ports:
        print("未发现串口设备")
        return
    print(f"{'端口':<10} {'描述'}")
    print("-" * 60)
    for p in ports:
        print(f"{p.device:<10} {p.description}")


def format_rate(bps: float) -> str:
    if bps >= 1024 * 1024:
        return f"{bps / 1024 / 1024:.2f} MB/s"
    if bps >= 1024:
        return f"{bps / 1024:.2f} KB/s"
    return f"{bps:.0f} B/s"


def monitor(
    port: str,
    baudrate: int,
    interval: float,
    timeout: float,
) -> None:
    ser = serial.Serial(
        port=port,
        baudrate=baudrate,
        timeout=timeout,
    )

    total_bytes = 0
    window_bytes = 0
    window_start = time.monotonic()
    start = window_start
    peak_bps = 0.0
    sample_count = 0

    print(f"已打开 {port} @ {baudrate} bps，每 {interval:g}s 统计一次，Ctrl+C 退出")
    print(f"{'时间(s)':>10}  {'本秒':>12}  {'累计':>12}  {'峰值':>12}")
    print("-" * 52)

    try:
        while True:
            chunk = ser.read(ser.in_waiting or 1)
            if chunk:
                n = len(chunk)
                total_bytes += n
                window_bytes += n

            now = time.monotonic()
            elapsed = now - window_start
            if elapsed >= interval:
                bps = window_bytes / elapsed
                peak_bps = max(peak_bps, bps)
                sample_count += 1
                uptime = now - start
                print(
                    f"{uptime:10.1f}  {format_rate(bps):>12}  "
                    f"{format_rate(total_bytes / uptime if uptime > 0 else 0):>12}  "
                    f"{format_rate(peak_bps):>12}"
                )
                window_bytes = 0
                window_start = now
    except KeyboardInterrupt:
        uptime = time.monotonic() - start
        avg_bps = total_bytes / uptime if uptime > 0 else 0
        print("-" * 52)
        print(f"总计: {total_bytes} 字节 ({total_bytes / 1024:.2f} KB)")
        print(f"时长: {uptime:.1f} s，平均: {format_rate(avg_bps)}，峰值: {format_rate(peak_bps)}")
    finally:
        ser.close()


def main() -> None:
    parser = argparse.ArgumentParser(description="串口接收速率检测（每秒统计）")
    parser.add_argument("port", nargs="?", help="串口名，如 COM3 或 /dev/ttyUSB0")
    parser.add_argument("-b", "--baudrate", type=int, default=115200, help="波特率，默认 115200")
    parser.add_argument(
        "-i", "--interval", type=float, default=1.0, help="统计间隔（秒），默认 1"
    )
    parser.add_argument(
        "-t", "--timeout", type=float, default=0.05, help="读超时（秒），默认 0.05"
    )
    parser.add_argument("-l", "--list", action="store_true", help="列出可用串口")
    args = parser.parse_args()

    if args.list:
        list_serial_ports()
        return

    if not args.port:
        parser.print_help()
        print("\n可用串口:")
        list_serial_ports()
        sys.exit(1)

    monitor(args.port, args.baudrate, args.interval, args.timeout)


if __name__ == "__main__":
    main()
