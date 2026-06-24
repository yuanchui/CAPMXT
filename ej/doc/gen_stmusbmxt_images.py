#!/usr/bin/env python3
"""Generate PNG diagrams for STMUSBMXT technical docs (640U/641, test-V1.7)."""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).resolve().parent / "images"
OUT.mkdir(parents=True, exist_ok=True)

C_BG = (255, 255, 255)
C_LABEL = (31, 41, 55)
C_SUB = (75, 85, 99)
C_ARROW = (100, 116, 139)
C_PC = (219, 234, 254)
C_MCU = (254, 243, 199)
C_TOUCH = (220, 252, 231)
C_SPI = (237, 233, 254)
C_USB = (255, 228, 230)
C_GAP = (241, 245, 249)
C_ACTIVE = (254, 226, 226)
C_BOX = (248, 250, 252)


def load_font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    candidates = [
        ("C:/Windows/Fonts/msyhbd.ttc", 0),
        ("C:/Windows/Fonts/msyh.ttc", 0),
        ("C:/Windows/Fonts/simhei.ttf", None),
        ("C:/Windows/Fonts/arial.ttf", None),
        ("msyh.ttc", 0),
        ("arial.ttf", None),
    ]
    for path, index in candidates:
        if bold and "msyh.ttc" in path and "msyhbd" not in path:
            continue
        try:
            if index is None:
                return ImageFont.truetype(path, size=size)
            return ImageFont.truetype(path, size=size, index=index)
        except (OSError, TypeError):
            continue
    return ImageFont.load_default()


def text_size(draw: ImageDraw.ImageDraw, text: str, font) -> tuple[int, int]:
    box = draw.textbbox((0, 0), text, font=font)
    return box[2] - box[0], box[3] - box[1]


def draw_round_rect(draw, box, fill, outline=None, r=8):
    if outline:
        draw.rounded_rectangle(box, radius=r, fill=fill, outline=outline, width=1)
    else:
        draw.rounded_rectangle(box, radius=r, fill=fill)


def draw_center_text(draw, box, lines, font):
    tw = max(text_size(draw, ln, font)[0] for ln in lines)
    th = sum(text_size(draw, ln, font)[1] for ln in lines) + (len(lines) - 1) * 4
    y = box[1] + (box[3] - box[1] - th) // 2
    for ln in lines:
        w, h = text_size(draw, ln, font)
        draw.text((box[0] + (box[2] - box[0] - w) // 2, y), ln, font=font, fill=C_LABEL)
        y += h + 4


def draw_arrow_h(draw, x0, x1, y, label=None, font=None, bidirectional=False):
    draw.line([(x0, y), (x1, y)], fill=C_ARROW, width=2)
    draw.polygon([(x1, y), (x1 - 8, y - 4), (x1 - 8, y + 4)], fill=C_ARROW)
    if bidirectional:
        draw.polygon([(x0, y), (x0 + 8, y - 4), (x0 + 8, y + 4)], fill=C_ARROW)
    if label and font:
        tw, _ = text_size(draw, label, font)
        draw.text(((x0 + x1 - tw) // 2, y - 18), label, font=font, fill=C_SUB)


def draw_arrow_down(draw, x, y0, y1, label=None, font=None):
    draw.line([(x, y0), (x, y1)], fill=C_ARROW, width=2)
    draw.polygon([(x, y1), (x - 4, y1 - 8), (x + 4, y1 - 8)], fill=C_ARROW)
    if label and font:
        tw, _ = text_size(draw, label, font)
        draw.text((x - tw // 2, (y0 + y1) // 2 - 8), label, font=font, fill=C_SUB)


def save(name: str, img: Image.Image) -> None:
    path = OUT / name
    img.save(path, "PNG")
    print(f"Wrote {path}")


def gen_arch_dataflow() -> None:
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
    draw_center_text(draw, com_box, ["COM 口", "（文本 / Mode3 二进制）"], f_body)

    ay = (st_box[1] + st_box[3]) // 2
    draw_arrow_h(draw, st_box[2] + 8, com_box[0] - 8, ay, "USB CDC", f_small, bidirectional=True)
    draw_arrow_down(draw, W // 2, pc_box[3], 188, "USB 2.0 Full Speed（64B/包）", f_small)

    mcu_box = (margin, 200, W - margin, 520)
    draw_round_rect(draw, mcu_box, C_MCU)
    draw.text((mcu_box[0] + 16, mcu_box[1] + 10), "STM32F103C8（test-V1.7 桥接 MCU）", font=f_cap, fill=C_LABEL)

    inner_x0, inner_x1 = mcu_box[0] + 20, mcu_box[2] - 20
    row_h, y = 58, mcu_box[1] + 42
    rows = [
        ("USB CDC 虚拟串口", "I2C2", "T6 DEBUGCTRL / T37 / CFG"),
        ("SPI1 从机 + IT 逐字节", "PA4 NSS", "640U/641 硬件片选"),
        ("SPI 接收队列", "2048 深", "NSS 边沿组 640B 帧"),
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

    draw_arrow_down(draw, W // 2, 520, 558, "I2C + SPI + CHG / RST", f_small)

    touch_box = (margin, 570, W - margin, 690)
    draw_round_rect(draw, touch_box, C_TOUCH)
    draw.text((touch_box[0] + 16, touch_box[1] + 12), "ATMXT640U / ATMXT641 触摸控制器", font=f_cap, fill=C_LABEL)
    modules = "T5 消息  |  T6 命令  |  T37 诊断  |  T100 触摸  |  SPI Debug Master"
    tw, _ = text_size(draw, modules, f_body)
    draw.text(((W - tw) // 2, touch_box[1] + 52), modules, font=f_body, fill=C_LABEL)
    save("arch_dataflow.png", img)


def gen_project_structure() -> None:
    W, H = 920, 400
    img = Image.new("RGB", (W, H), C_BG)
    draw = ImageDraw.Draw(img)
    f_cap = load_font(13, bold=True)
    f_mono = load_font(11)

    draw.text((36, 16), "工程目录结构（ej/test-V1.7/）", font=f_cap, fill=C_LABEL)
    box = (36, 48, 884, 360)
    draw_round_rect(draw, box, (248, 250, 252))

    lines = [
        "Core/              main、GPIO、I2C2、SPI1（硬件 NSS）",
        "USB_DEVICE/App/mxt/  命令、SPI 流、I2C、USB 桥接",
        "MDK-ARM/           Keil 工程 STMUSBATMXT640.uvprojx",
        "Drivers/           STM32F1 HAL / CMSIS",
        "Middlewares/       ST USB Device Library",
        "README.md          本工程快速说明",
        "ej/doc/            STMUSBMXT 技术文档与协议",
        "ej/serial-app/     Electron PC 端 Serial Terminal",
        "ej/linux_driver_mxt_sys/   mxt-app CLI",
    ]
    y = 64
    for ln in lines:
        draw.text((52, y), ln, font=f_mono, fill=C_LABEL)
        y += 28
    save("project_structure.png", img)


def gen_spi_pipeline() -> None:
    W, H = 920, 340
    img = Image.new("RGB", (W, H), C_BG)
    draw = ImageDraw.Draw(img)
    f_body = load_font(12)
    f_small = load_font(11)

    draw.text((36, 16), "SPI 接收与 USB 发送（640U/641 硬件 NSS）", font=load_font(14, bold=True), fill=C_LABEL)

    y0, y1 = 56, 130
    b1 = (36, y0, 200, y1)
    b2 = (240, y0, 420, y1)
    b3 = (460, y0, 640, y1)
    draw_round_rect(draw, b1, C_SPI)
    draw_round_rect(draw, b2, C_MCU)
    draw_round_rect(draw, b3, C_SPI)
    draw_center_text(draw, b1, ["IT 队列", "[2048 × 1B]"], f_body)
    draw_center_text(draw, b2, ["SPI1 Slave RX", "PA4 硬件 NSS"], f_body)
    draw_center_text(draw, b3, ["NSS 组帧", "640B / 扫描"], f_body)

    ay = (y0 + y1) // 2
    draw_arrow_h(draw, b1[2] + 4, b2[0] - 4, ay, "逐字节入队", f_small)
    draw_arrow_h(draw, b2[2] + 4, b3[0] - 4, ay, "第一次 NSS↓", f_small)

    usb_box = (300, 200, 620, 280)
    draw_round_rect(draw, usb_box, C_USB)
    draw_center_text(draw, usb_box, ["USB CDC 64B 分包", "4096B TX 缓冲"], f_body)
    draw_arrow_down(draw, (b3[0] + b3[2]) // 2, y1 + 8, usb_box[1] - 4, None, None)

    notes = [
        "三次 SSN/扫描：第一次下降沿起收集 640B",
        "SPISTART3：16 行 × 40B Mode3 → 640B USB/帧",
        "队列满 rx_ovf++；USB 慢 tx_drop++",
    ]
    ny = 296
    for n in notes:
        draw.text((48, ny), "• " + n, font=f_small, fill=C_SUB)
        ny += 18
    save("spi_pipeline.png", img)


def gen_mode3_packet() -> None:
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
    save("mode3_packet.png", img)


def gen_usb_tx_channels() -> None:
    W, H = 920, 360
    img = Image.new("RGB", (W, H), C_BG)
    draw = ImageDraw.Draw(img)
    f_cap = load_font(13, bold=True)
    f_body = load_font(11)

    draw.text((36, 16), "USB CDC 发送通道", font=f_cap, fill=C_LABEL)

    channels = [
        ("通道 A", "文本响应", "USB_SendString → 消息缓冲 → 64B 分包", C_PC),
        ("通道 B", "SPI Mode3/HEX", "g_spi_hex_tx_buf[4096] → flush", C_SPI),
        ("通道 C", "阻塞原始发送", "CFG 应答、桥接二进制", C_USB),
    ]
    y = 52
    sink = (300, 260, 620, 310)
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
    save("usb_tx_channels.png", img)


def main() -> None:
    gen_arch_dataflow()
    gen_project_structure()
    gen_spi_pipeline()
    gen_mode3_packet()
    gen_usb_tx_channels()
    print("Done.")


if __name__ == "__main__":
    main()
