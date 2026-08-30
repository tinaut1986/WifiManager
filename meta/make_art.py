#!/usr/bin/env python3
"""Regenerate meta/banner.png (256x128) and meta/icon.png (48x48).

Clean, centered modern dark theme with electric cyan/blue accents.
- Icon: perfectly centered Wi-Fi arcs + plus badge within squircle frame.
- Banner: clean centered 'WiFiManager+' wordmark + 'BACKUP - RESTORE - QR SHARE'.

Run: python3 meta/make_art.py
"""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))

FONT_CANDIDATES_BOLD = [
    "/usr/share/fonts/truetype/ubuntu/Ubuntu-B.ttf",
    "/usr/share/fonts/truetype/space-grotesk/SpaceGrotesk-Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"
]
FONT_CANDIDATES_REG = [
    "/usr/share/fonts/truetype/ubuntu/Ubuntu-M.ttf",
    "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
    "/usr/share/fonts/truetype/space-grotesk/SpaceGrotesk-Medium.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"
]

def get_font(size, bold=True):
    candidates = FONT_CANDIDATES_BOLD if bold else FONT_CANDIDATES_REG
    for path in candidates:
        if os.path.exists(path):
            try:
                return ImageFont.truetype(path, size)
            except Exception:
                pass
    return ImageFont.load_default()

# Colors
C_BG_TOP    = (11, 15, 21)
C_BG_BOT    = (19, 26, 36)
C_PANEL_BG  = (25, 34, 48)
C_PANEL_BORDER = (45, 60, 82)
C_ACCENT    = (56, 189, 248)    # Sky blue
C_ACCENT_DIM= (2, 132, 199)
C_WHITE     = (248, 250, 252)
C_DIM       = (160, 175, 195)

SS = 4  # 4x Supersampling for clean anti-aliasing

def draw_gradient_background(img, c1, c2):
    W, H = img.size
    base = Image.new("RGBA", (W, H), (0, 0, 0, 255))
    d = ImageDraw.Draw(base)
    for y in range(H):
        t = y / max(1, H - 1)
        r = int(c1[0] + (c2[0] - c1[0]) * t)
        g = int(c1[1] + (c2[1] - c1[1]) * t)
        b = int(c1[2] + (c2[2] - c1[2]) * t)
        d.line([(0, y), (W, y)], fill=(r, g, b, 255))
    img.paste(base, (0, 0))

def draw_wifi_symbol(draw, cx, cy, radius, stroke, dot_r, arc_colors):
    """Draw crisp 3-arc Wi-Fi symbol with center dot."""
    start_deg = 220
    end_deg = 320
    radii = [radius, radius * 0.70, radius * 0.42]
    for r, col in zip(radii, arc_colors):
        bbox = [cx - r, cy - r, cx + r, cy + r]
        draw.arc(bbox, start=start_deg, end=end_deg, fill=col, width=stroke)
    draw.ellipse([cx - dot_r, cy - dot_r, cx + dot_r, cy + dot_r], fill=arc_colors[-1])

def make_icon():
    W = H = 48 * SS
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # Centered squircle card
    pad = 2 * SS
    r = 10 * SS
    d.rounded_rectangle([pad, pad, W - pad, H - pad], r, fill=C_BG_TOP, outline=C_ACCENT, width=int(1.5 * SS))
    
    # Inner surface
    inner_pad = 4 * SS
    d.rounded_rectangle([inner_pad, inner_pad, W - inner_pad, H - inner_pad], r - 2 * SS, fill=C_PANEL_BG)

    # Wi-Fi symbol - centered in the card
    cx = W / 2.0
    cy = H * 0.66
    wifi_r = W * 0.36
    stroke = int(2.5 * SS)
    dot_r = int(2.8 * SS)
    draw_wifi_symbol(d, cx, cy, wifi_r, stroke, dot_r, [C_ACCENT, (125, 211, 252), C_WHITE])

    # Distinct '+' badge in top right (inside inner surface)
    p_cx = W - 11.5 * SS
    p_cy = 11.5 * SS
    p_r = 5.5 * SS
    d.ellipse([p_cx - p_r, p_cy - p_r, p_cx + p_r, p_cy + p_r], fill=C_ACCENT_DIM, outline=C_WHITE, width=int(1 * SS))
    d.rectangle([p_cx - 3.2 * SS, p_cy - 0.9 * SS, p_cx + 3.2 * SS, p_cy + 0.9 * SS], fill=C_WHITE)
    d.rectangle([p_cx - 0.9 * SS, p_cy - 3.2 * SS, p_cx + 0.9 * SS, p_cy + 3.2 * SS], fill=C_WHITE)

    out = img.resize((48, 48), Image.LANCZOS)
    out.convert("RGB").save(os.path.join(HERE, "icon.png"))

def make_banner():
    W, H = 256 * SS, 128 * SS
    img = Image.new("RGBA", (W, H), (0, 0, 0, 255))
    draw_gradient_background(img, C_BG_TOP, C_BG_BOT)
    d = ImageDraw.Draw(img)

    # Top and bottom neon accent lines
    d.rectangle([0, 0, W, 2.5 * SS], fill=C_ACCENT)
    d.rectangle([0, H - 4 * SS, W, H], fill=C_ACCENT_DIM)

    # Subtle horizontal decorative lines
    d.line([(20 * SS, 18 * SS), (W - 20 * SS, 18 * SS)], fill=(30, 42, 58, 255), width=int(1 * SS))
    d.line([(20 * SS, H - 18 * SS), (W - 20 * SS, H - 18 * SS)], fill=(30, 42, 58, 255), width=int(1 * SS))

    # Center Panel Box
    card_w = 224 * SS
    card_h = 76 * SS
    card_x0 = (W - card_w) / 2
    card_y0 = (H - card_h) / 2 - 1 * SS
    card_x1 = card_x0 + card_w
    card_y1 = card_y0 + card_h
    card_r = 10 * SS

    # Outer border and inner panel fill
    d.rounded_rectangle([card_x0 - 1.5 * SS, card_y0 - 1.5 * SS, card_x1 + 1.5 * SS, card_y1 + 1.5 * SS],
                        card_r + 1.5 * SS, fill=(15, 21, 30, 255), outline=C_PANEL_BORDER, width=int(1 * SS))
    d.rounded_rectangle([card_x0, card_y0, card_x1, card_y1], card_r, fill=C_PANEL_BG)

    # Fonts
    f_title = get_font(24 * SS, bold=True)
    f_sub   = get_font(8.5 * SS, bold=True)

    # Title: WiFiManager +
    title_text = "WiFiManager"
    plus_text  = "+"
    w_title = d.textlength(title_text, font=f_title)
    w_plus  = d.textlength(plus_text, font=f_title)
    gap = 4 * SS
    total_title_w = w_title + gap + w_plus

    title_x = (W - total_title_w) / 2
    title_y = card_y0 + 13 * SS

    # Draw "WiFiManager" in crisp white and "+" in vibrant Cyan
    d.text((title_x, title_y), title_text, font=f_title, fill=C_WHITE)
    d.text((title_x + w_title + gap, title_y), plus_text, font=f_title, fill=C_ACCENT)

    # Thin separator line inside panel
    sep_y = title_y + 32 * SS
    sep_w = 160 * SS
    d.line([((W - sep_w) / 2, sep_y), ((W + sep_w) / 2, sep_y)], fill=(45, 60, 85, 255), width=int(1 * SS))

    # Subtitle: "BACKUP - RESTORE - QR SHARE"
    sub_text = "BACKUP  -  RESTORE  -  QR SHARE"
    w_sub = d.textlength(sub_text, font=f_sub)
    sub_x = (W - w_sub) / 2
    sub_y = sep_y + 8 * SS

    d.text((sub_x, sub_y), sub_text, font=f_sub, fill=C_DIM)

    # Output 256x128 banner
    out = img.resize((256, 128), Image.LANCZOS)
    out.convert("RGB").save(os.path.join(HERE, "banner.png"))

if __name__ == "__main__":
    make_icon()
    make_banner()
    print("Regenerated clean centered icon.png and banner.png")


if __name__ == "__main__":
    make_icon()
    make_banner()
    print("Successfully generated modern meta/icon.png and meta/banner.png")

