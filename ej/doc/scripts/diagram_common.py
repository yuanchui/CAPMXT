# -*- coding: utf-8 -*-
"""文档架构图共用样式与绘图工具。"""

from PIL import Image, ImageDraw, ImageFont
import os

# 配色（与 arch_dataflow 一致）
C_BG = (255, 255, 255)
C_BORDER = (55, 65, 81)
C_PC = (219, 234, 254)
C_MCU = (254, 243, 199)
C_TOUCH = (220, 252, 231)
C_SPI = (237, 233, 254)
C_USB = (255, 228, 230)
C_BOX = (255, 255, 255)
C_ARROW = (75, 85, 99)
C_LABEL = (31, 41, 55)
C_SUB = (107, 114, 128)
C_ACTIVE = (254, 226, 226)
C_GAP = (224, 242, 254)

IMAGES_DIR = os.path.join(os.path.dirname(__file__), "..", "images")


def load_font(size: int, bold: bool = False):
    names = [
        "C:/Windows/Fonts/msyhbd.ttc" if bold else "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/simsun.ttc",
    ]
    for p in names:
        if os.path.isfile(p):
            try:
                return ImageFont.truetype(p, size)
            except OSError:
                pass
    return ImageFont.load_default()


def text_size(draw, text, font):
    bb = draw.textbbox((0, 0), text, font=font)
    return bb[2] - bb[0], bb[3] - bb[1]


def draw_round_rect(draw, xy, fill, outline=C_BORDER, radius=8, width=2):
    draw.rounded_rectangle(xy, radius=radius, fill=fill, outline=outline, width=width)


def draw_center_text(draw, box, lines, font, fill=C_LABEL, line_gap=6):
    x0, y0, x1, y1 = box
    heights = [text_size(draw, ln, font)[1] for ln in lines]
    total_h = sum(heights) + line_gap * (max(0, len(lines) - 1))
    y = y0 + (y1 - y0 - total_h) // 2
    for ln, lh in zip(lines, heights):
        tw, _ = text_size(draw, ln, font)
        draw.text((x0 + (x1 - x0 - tw) // 2, y), ln, font=font, fill=fill)
        y += lh + line_gap


def draw_arrow_h(draw, x0, x1, y, label=None, font=None, bidirectional=False):
    draw.line([(x0, y), (x1, y)], fill=C_ARROW, width=2)
    draw.polygon([(x1, y), (x1 - 10, y - 5), (x1 - 10, y + 5)], fill=C_ARROW)
    if bidirectional:
        draw.polygon([(x0, y), (x0 + 10, y - 5), (x0 + 10, y + 5)], fill=C_ARROW)
    if label and font:
        tw, th = text_size(draw, label, font)
        draw.text(((x0 + x1 - tw) // 2, y - th - 6), label, font=font, fill=C_SUB)


def draw_arrow_down(draw, cx, y0, y1, label=None, font=None):
    draw.line([(cx, y0), (cx, y1 - 10)], fill=C_ARROW, width=2)
    draw.polygon([(cx, y1), (cx - 7, y1 - 12), (cx + 7, y1 - 12)], fill=C_ARROW)
    if label and font:
        tw, th = text_size(draw, label, font)
        draw.text((cx - tw // 2, (y0 + y1) // 2 - th // 2 - 8), label, font=font, fill=C_SUB)


def save_img(img, name):
    os.makedirs(IMAGES_DIR, exist_ok=True)
    path = os.path.join(IMAGES_DIR, name)
    img.save(path, "PNG", optimize=True)
    print(path)
    return path
