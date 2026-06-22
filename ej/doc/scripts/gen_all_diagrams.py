# -*- coding: utf-8 -*-
"""生成技术文档全部架构图 PNG。"""

from PIL import Image, ImageDraw
from diagram_common import *


def gen_arch_dataflow():
    W, H = 920, 720
    img = Image.new("RGB", (W, H), C_BG)
    draw = ImageDraw.Draw(img)
    f_body = load_font(13)
    f_small = load_font(11)
    f_cap = load_font(12, bold=True)
    margin = 36

    pc_box = (margin, 24, W - margin, 148)
    draw_round_rect(draw, pc_box, C_PC)
    draw.text((pc_box[0] + 16, pc_box[1] + 10), "PC (Windows)", font=f_cap, fill=C_LABEL)

    st_box = (pc_box[0] + 24, pc_box[1] + 38, pc_box[0] + 280, pc_box[1] + 108)
    com_box = (pc_box[2] - 300, pc_box[1] + 38, pc_box[2] - 24, pc_box[1] + 108)
    draw_round_rect(draw, st_box, C_BOX, (180, 180, 180))
    draw_round_rect(draw, com_box, C_BOX, (180, 180, 180))
    draw_center_text(draw, st_box, ["Serial Terminal", "+ mxt-app CLI"], f_body)
    draw_center_text(draw, com_box, ["COM 口", "（文本命令 / 二进制流）"], f_body)

    ay = (st_box[1] + st_box[3]) // 2
    ax0, ax1 = st_box[2] + 8, com_box[0] - 8
    draw_arrow_h(draw, ax0, ax1, ay, "USB CDC", f_small, bidirectional=True)

    pc_cx = W // 2
    draw_arrow_down(draw, pc_cx, pc_box[3], 188, "USB 2.0 Full Speed（12 Mbps，64B/包）", f_small)

    mcu_box = (margin, 200, W - margin, 520)
    draw_round_rect(draw, mcu_box, C_MCU)
    draw.text((mcu_box[0] + 16, mcu_box[1] + 10), "STM32F103C8（桥接 MCU）", font=f_cap, fill=C_LABEL)

    inner_x0, inner_x1 = mcu_box[0] + 20, mcu_box[2] - 20
    row_h, y = 58, mcu_box[1] + 42
    rows = [
        ("USB CDC 虚拟串口", "I2C2", "maXTouch 配置 / T6 / T37 / Bootloader"),
        ("SPI1 从机 + DMA Ring", "SCK/MOSI", "maXTouch SPI Debug（Master）"),
        ("SSN 合成（TIM1+EXTI）", "PA10→PA9", "监视 MISO，虚拟片选"),
        ("CHG 中断", "PA3", "触摸消息变更通知"),
    ]
    for left, mid, right in rows:
        lb = (inner_x0, y, inner_x0 + 200, y + row_h)
        rb = (inner_x1 - 420, y, inner_x1, y + row_h)
        draw_round_rect(draw, lb, C_BOX, (180, 180, 180))
        draw_round_rect(draw, rb, C_BOX, (180, 180, 180))
        draw_center_text(draw, lb, [left], f_body)
        tw, th = text_size(draw, mid, f_small)
        draw.text(((inner_x0 + inner_x1) // 2 - tw // 2, y + (row_h - th) // 2), mid, font=f_small, fill=C_SUB)
        draw_center_text(draw, rb, [right], f_small)
        y += row_h + 10

    draw_arrow_down(draw, pc_cx, 520, 558, "I2C + SPI + CHG / RST", f_small)

    touch_box = (margin, 570, W - margin, 690)
    draw_round_rect(draw, touch_box, C_TOUCH)
    draw.text((touch_box[0] + 16, touch_box[1] + 12), "mXT640UD-CCUBHA1 触摸控制器", font=f_cap, fill=C_LABEL)
    modules = "T5 消息  |  T6 命令  |  T37 诊断  |  T100 触摸  |  T44 计数"
    tw, _ = text_size(draw, modules, f_body)
    draw.text(((W - tw) // 2, touch_box[1] + 52), modules, font=f_body, fill=C_LABEL)
    save_img(img, "arch_dataflow.png")


def gen_ssn_state_machine():
    W, H = 880, 200
    img = Image.new("RGB", (W, H), C_BG)
    draw = ImageDraw.Draw(img)
    f_cap = load_font(13, bold=True)
    f_body = load_font(12)
    f_small = load_font(11)

    draw.text((36, 16), "虚拟 SSN 状态机", font=f_cap, fill=C_LABEL)

    y0, y1 = 70, 150
    gap_box = (40, y0, 240, y1)
    active_box = (320, y0, 520, y1)
    gap2_box = (600, y0, 800, y1)

    draw_round_rect(draw, gap_box, C_GAP)
    draw_round_rect(draw, active_box, C_ACTIVE)
    draw_round_rect(draw, gap2_box, C_GAP)
    draw_center_text(draw, gap_box, ["帧间 (gap)", "PA9 = HIGH"], f_body)
    draw_center_text(draw, active_box, ["帧内 (active)", "PA9 = LOW"], f_body)
    draw_center_text(draw, gap2_box, ["帧间 (gap)", "PA9 = HIGH"], f_body)

    ay = (y0 + y1) // 2
    draw_arrow_h(draw, gap_box[2] + 4, active_box[0] - 4, ay, "EnterActive", f_small)
    draw_arrow_h(draw, active_box[2] + 4, gap2_box[0] - 4, ay, "EndActive", f_small)

    note = "MXT_SSN_IsSelected() == 1 表示当前处于帧内（SSN 有效）"
    tw, _ = text_size(draw, note, f_small)
    draw.text(((W - tw) // 2, 168), note, font=f_small, fill=C_SUB)
    save_img(img, "ssn_state_machine.png")


def gen_spi_pipeline():
    W, H = 920, 340
    img = Image.new("RGB", (W, H), C_BG)
    draw = ImageDraw.Draw(img)
    f_cap = load_font(12, bold=True)
    f_body = load_font(12)
    f_small = load_font(11)

    draw.text((36, 16), "SPI 接收与 USB 发送半流水线", font=load_font(14, bold=True), fill=C_LABEL)

    y0, y1 = 56, 130
    b1 = (36, y0, 200, y1)
    b2 = (240, y0, 420, y1)
    b3 = (460, y0, 640, y1)
    draw_round_rect(draw, b1, C_SPI)
    draw_round_rect(draw, b2, C_MCU)
    draw_round_rect(draw, b3, C_SPI)
    draw_center_text(draw, b1, ["DMA Ring", "[1024 bytes]"], f_body)
    draw_center_text(draw, b2, ["SPI1 Slave RX", "（硬件）"], f_body)
    draw_center_text(draw, b3, ["帧提取", "2 槽 × 514B"], f_body)

    ay = (y0 + y1) // 2
    draw_arrow_h(draw, b1[2] + 4, b2[0] - 4, ay, "SSN 帧内持续写入", f_small)
    draw_arrow_h(draw, b2[2] + 4, b3[0] - 4, ay, "SSN 帧间 gap 提取", f_small)

    usb_box = (300, 200, 620, 280)
    draw_round_rect(draw, usb_box, C_USB)
    draw_center_text(draw, usb_box, ["USB CDC 64B 分包", "乒乓缓冲发送"], f_body)
    draw_arrow_down(draw, (b3[0] + b3[2]) // 2, y1 + 8, usb_box[1] - 4, None, None)

    notes = [
        "帧内：只采集 SPI，不 flush USB",
        "帧间：提取上一帧 + 排空 USB 队列",
        "最多 2 帧排队；USB 慢则 g_spi_raw_usb_drop++",
    ]
    ny = 296
    for n in notes:
        draw.text((48, ny), "• " + n, font=f_small, fill=C_SUB)
        ny += 18
    save_img(img, "spi_pipeline.png")


def gen_mode3_packet():
    W, H = 920, 220
    img = Image.new("RGB", (W, H), C_BG)
    draw = ImageDraw.Draw(img)
    f_cap = load_font(13, bold=True)
    f_small = load_font(11)

    draw.text((36, 16), "Mode3 标准行包（40 字节）", font=f_cap, fill=C_LABEL)

    fields = [
        ("AA 10 33", 3, C_PC),
        ("LEN", 1, C_GAP),
        ("FRAME_ID", 1, C_MCU),
        ("ROW_ID", 1, C_TOUCH),
        ("DATA[32]", 32, C_SPI),
        ("CRC16", 2, C_USB),
    ]
    x, y0, y1 = 36, 60, 130
    for label, nbytes, color in fields:
        w = max(52, nbytes * 8 + 16)
        box = (x, y0, x + w, y1)
        draw_round_rect(draw, box, color)
        draw_center_text(draw, box, [label, f"{nbytes}B"], f_small)
        x += w + 6

    draw.text((36, 150), "CRC16-CCITT-FALSE，范围 packet[0..37]；DATA 为 16×int16 大端", font=f_small, fill=C_SUB)
    draw.text((36, 172), "CHGNO 扩展：DATA 后插入 TOUCH_ID + X + Y + ACTION，总长 46 字节", font=f_small, fill=C_SUB)
    save_img(img, "mode3_packet.png")


def gen_usb_tx_channels():
    W, H = 920, 360
    img = Image.new("RGB", (W, H), C_BG)
    draw = ImageDraw.Draw(img)
    f_cap = load_font(13, bold=True)
    f_body = load_font(11)

    draw.text((36, 16), "USB CDC 发送通道", font=f_cap, fill=C_LABEL)

    channels = [
        ("通道 A", "文本响应", "USB_SendString → 2048B 环形缓冲 → 64B 分包", C_PC),
        ("通道 B", "SPI Mode3", "g_spi_tx_buf → 双缓冲 flush", C_SPI),
        ("通道 B2", "SPISTART 原始流", "g_spi_raw_slots → 88 77 66 组帧", C_MCU),
        ("通道 C", "阻塞原始发送", "CFG/ENC 应答、桥接二进制", C_USB),
    ]
    y = 52
    sink = (300, 280, 620, 330)
    for title, subtitle, desc, color in channels:
        box = (36, y, 860, y + 52)
        draw_round_rect(draw, box, color)
        draw.text((box[0] + 12, box[1] + 8), title, font=load_font(12, bold=True), fill=C_LABEL)
        draw.text((box[0] + 90, box[1] + 8), subtitle, font=f_body, fill=C_LABEL)
        draw.text((box[0] + 12, box[1] + 28), desc, font=f_body, fill=C_SUB)
        cx = (box[0] + box[2]) // 2
        draw.line([(cx, box[3]), (cx, sink[1] - 8)], fill=C_ARROW, width=1)
        y += 58

    draw_round_rect(draw, sink, C_TOUCH)
    draw_center_text(draw, sink, ["CDC_Transmit_FS()", "TxState 流控，BUSY 时保留待重试"], f_body)
    save_img(img, "usb_tx_channels.png")


def gen_project_structure():
    W, H = 920, 400
    img = Image.new("RGB", (W, H), C_BG)
    draw = ImageDraw.Draw(img)
    f_cap = load_font(13, bold=True)
    f_mono = load_font(11)

    draw.text((36, 16), "工程目录结构（电容2.7/）", font=f_cap, fill=C_LABEL)
    box = (36, 48, 884, 360)
    draw_round_rect(draw, box, (248, 250, 252))

    lines = [
        "Core/              STM32 HAL：main、GPIO、SPI、I2C、TIM、DMA",
        "USB_DEVICE/        USB CDC + App/mxt/ 触摸桥接核心",
        "MDK-ARM/           Keil 工程 STMUSBATMXT640.uvprojx",
        "Drivers/           STM32F1 HAL / CMSIS",
        "Middlewares/       ST USB Device Library",
        "ej/doc/            技术文档与协议说明",
        "ej/serial-app/     Electron PC 端 Serial Terminal",
        "ej/linux_driver_mxt_sys/   mxt-app CLI、xcfg-viewer",
        "tools/             Python 辅助脚本",
        "readme.txt         SSN / SPI / USB 快速说明",
    ]
    y = 64
    for ln in lines:
        draw.text((52, y), ln, font=f_mono, fill=C_LABEL)
        y += 28
    save_img(img, "project_structure.png")


def gen_serial_app_structure():
    W, H = 920, 380
    img = Image.new("RGB", (W, H), C_BG)
    draw = ImageDraw.Draw(img)
    f_cap = load_font(13, bold=True)
    f_mono = load_font(11)

    draw.text((36, 16), "serial-app 架构（ej/serial-app/）", font=f_cap, fill=C_LABEL)
    outer = (36, 48, 884, 340)
    draw_round_rect(draw, outer, C_PC)

    src_box = (52, 68, 420, 200)
    res_box = (440, 68, 868, 200)
    build_box = (52, 220, 868, 310)
    draw_round_rect(draw, src_box, C_BOX, (180, 180, 180))
    draw_round_rect(draw, res_box, C_BOX, (180, 180, 180))
    draw_round_rect(draw, build_box, C_BOX, (180, 180, 180))

    draw.text((src_box[0] + 10, src_box[1] + 8), "src/", font=load_font(12, bold=True), fill=C_LABEL)
    src_lines = [
        "main/    IPC、串口、CFG/ENC 烧录",
        "  index.ts, cfg_protocol, enc_protocol",
        "  mxt_serial_bridge, xcfg_codec",
        "renderer/  前端 UI（Vite + 矩阵显示）",
        "preload/   Electron 预加载桥接",
    ]
    y = src_box[1] + 30
    for ln in src_lines:
        draw.text((src_box[0] + 14, y), ln, font=f_mono, fill=C_LABEL)
        y += 22

    draw.text((res_box[0] + 10, res_box[1] + 8), "resources/", font=load_font(12, bold=True), fill=C_LABEL)
    res_lines = [
        "xcfg-viewer/     配置查看器",
        "xcfg-templates/  产线模板 xcfg",
        "CLI/             mxt-app.exe 等",
    ]
    y = res_box[1] + 30
    for ln in res_lines:
        draw.text((res_box[0] + 14, y), ln, font=f_mono, fill=C_LABEL)
        y += 26

    draw.text((build_box[0] + 10, build_box[1] + 8), "build/", font=load_font(12, bold=True), fill=C_LABEL)
    draw.text((build_box[0] + 14, build_box[1] + 32), "打包脚本、prepare-cli、mxt-app 集成、NSIS 安装包", font=f_mono, fill=C_LABEL)
    save_img(img, "serial_app_structure.png")


def main():
    gen_arch_dataflow()
    gen_ssn_state_machine()
    gen_spi_pipeline()
    gen_mode3_packet()
    gen_usb_tx_channels()
    gen_project_structure()
    gen_serial_app_structure()
    print("All diagrams generated.")


if __name__ == "__main__":
    main()
