#!/usr/bin/env python3
"""Regenerate meta/banner.png (256x128) and meta/icon.png (48x48).

Flat dark theme matching the in-app palette. Shared mark = Wi-Fi arcs + a
bold accent "+" (the app is "WiFiManager+"). The banner puts a raised/embossed
wordmark on a flat panel so it reads as layered.

NOTE: this produces a *flat* PNG banner. Real stereoscopic depth / animation
needs an authored .cgfx (see README / drop meta/banner.cgfx and the Makefile
will use it). Run: python3 meta/make_art.py
"""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

BG      = (15, 20, 27)
PANEL   = (32, 44, 58)     # flat plate behind the wordmark
PANEL_E = (12, 17, 23)     # panel edge
SURFACE = (27, 36, 48)
ACCENT  = (58, 155, 255)
WHITE   = (242, 245, 248)
DIM     = (150, 165, 180)
EXTRUDE = (9, 13, 19)      # wordmark drop / extrusion colour
SS = 4                      # supersampling factor


def mark(d, cx, cy, r, plus_r):
    """Wi-Fi arcs opening upward + a bold accent '+' up and to the right."""
    aw = max(2, int(r * 0.15))
    for i, rad in enumerate((r, r * 0.72, r * 0.46)):
        col = WHITE if i != 1 else ACCENT   # outer wave white so it doesn't blend
        d.arc([cx - rad, cy - rad, cx + rad, cy + rad], 215, 325, fill=col, width=aw)
    d.ellipse([cx - aw, cy - aw, cx + aw, cy + aw], fill=ACCENT)   # centre dot, well clear of the inner wave

    px, py = cx + r * 0.95, cy - r * 0.75
    t = max(2, int(plus_r * 0.34))
    d.rectangle([px - plus_r, py - t, px + plus_r, py + t], fill=ACCENT)
    d.rectangle([px - t, py - plus_r, px + t, py + plus_r], fill=ACCENT)


def embossed_text(d, xy, text, font, fill, depth):
    x, y = xy
    for o in range(depth, 0, -1):
        d.text((x + o, y + o), text, font=font, fill=EXTRUDE)
    d.text((x, y), text, font=font, fill=fill)


def make_icon():
    W = H = 48 * SS
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([2 * SS, 2 * SS, W - 2 * SS, H - 2 * SS], 10 * SS, fill=SURFACE)
    mark(d, W * 0.44, H * 0.60, W * 0.32, W * 0.14)
    img = img.resize((48, 48), Image.LANCZOS)
    img.save(os.path.join(HERE, "icon.png"))


def make_banner():
    W, H = 256 * SS, 128 * SS
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)

    d.rectangle([0, 0, W, 3 * SS], fill=SURFACE)
    d.rectangle([0, H - 6 * SS, W, H], fill=ACCENT)               # accent baseline

    # flat panel behind the wordmark
    px0, py0, px1, py1 = int(W * 0.285), int(H * 0.19), int(W * 0.97), int(H * 0.81)
    d.rounded_rectangle([px0 - 2, py0 - 2, px1 + 2, py1 + 2], 12 * SS, fill=PANEL_E)
    d.rounded_rectangle([px0, py0, px1, py1], 12 * SS, fill=PANEL)

    mark(d, W * 0.135, H * 0.52, W * 0.092, W * 0.040)

    try:
        f_title = ImageFont.truetype(FONT_BOLD, 19 * SS)
        f_plus  = ImageFont.truetype(FONT_BOLD, 21 * SS)
        f_sub   = ImageFont.truetype(FONT_BOLD, 7 * SS)
    except OSError:
        f_title = f_plus = ImageFont.load_default()
        f_sub   = ImageFont.load_default()

    gap = 4 * SS
    w_title = d.textlength("WiFiManager", font=f_title)
    w_plus  = d.textlength("+", font=f_plus)
    total   = w_title + gap + w_plus
    pcx     = (px0 + px1) / 2
    tx      = int(pcx - total / 2)
    ty      = int(H * 0.28)

    embossed_text(d, (tx, ty), "WiFiManager", f_title, WHITE, depth=2 * SS)
    embossed_text(d, (int(tx + w_title + gap), ty - 2 * SS), "+", f_plus, ACCENT, depth=2 * SS)

    sub = "FORK  ·  BACKUP  ·  QR SHARE"
    d.text((int(pcx - d.textlength(sub, font=f_sub) / 2), int(H * 0.60)),
           sub, font=f_sub, fill=DIM)

    img = img.resize((256, 128), Image.LANCZOS)
    img.save(os.path.join(HERE, "banner.png"))


if __name__ == "__main__":
    make_icon()
    make_banner()
    print("wrote meta/icon.png and meta/banner.png")
