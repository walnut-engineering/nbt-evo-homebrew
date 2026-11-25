#!/usr/bin/env python3
"""Generate the NES tile icon (nes.png preview + nes.rgba for the launcher).

Original art, same panel style as the other utility icons: a rounded gradient
panel with a generic game-controller glyph (D-pad cross + two round buttons +
two select/start pills). No trademarks or console trade dress.

.rgba format (what evo_launcher reads): [u32 LE w][u32 LE h][w*h * u32 LE 0xAARRGGBB]
"""
import os, struct
from PIL import Image, ImageDraw

SIZE, RAD = 140, 28
OUT = os.path.dirname(os.path.abspath(__file__))

def panel(top, bot):
    img = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    grad = Image.new("RGB", (1, SIZE))
    for y in range(SIZE):
        t = y / (SIZE - 1)
        grad.putpixel((0, y), tuple(int(top[i] * (1 - t) + bot[i] * t) for i in range(3)))
    grad = grad.resize((SIZE, SIZE))
    mask = Image.new("L", (SIZE, SIZE), 0)
    ImageDraw.Draw(mask).rounded_rectangle([1, 1, SIZE - 2, SIZE - 2], RAD, fill=255)
    img.paste(grad, (0, 0), mask)
    hl = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    ImageDraw.Draw(hl).rounded_rectangle([1, 1, SIZE - 2, SIZE - 2], RAD, outline=(255, 255, 255, 40), width=2)
    return Image.alpha_composite(img, hl)

def nes():
    img = panel((0x5a, 0x1e, 0x2a), (0x1c, 0x08, 0x0c))          # deep red panel
    d = ImageDraw.Draw(img)
    body = (0xd8, 0xd8, 0xdc, 255); dark = (0x26, 0x26, 0x2c, 255)
    # controller body
    d.rounded_rectangle([18, 46, 122, 96], 10, fill=body)
    d.rounded_rectangle([24, 52, 116, 90], 7, fill=(0x3a, 0x3a, 0x42, 255))
    # D-pad cross (left)
    cx, cy = 46, 71
    d.rectangle([cx - 6, cy - 16, cx + 6, cy + 16], fill=dark)
    d.rectangle([cx - 16, cy - 6, cx + 16, cy + 6], fill=dark)
    # select / start pills (middle)
    d.rounded_rectangle([64, 67, 74, 73], 3, fill=dark)
    d.rounded_rectangle([78, 67, 88, 73], 3, fill=dark)
    # A / B buttons (right)
    d.ellipse([92, 64, 104, 76], fill=(0xd0, 0x30, 0x30, 255))
    d.ellipse([106, 64, 118, 76], fill=(0xd0, 0x30, 0x30, 255))
    return img

def save(img, name):
    img.save(os.path.join(OUT, name + ".png"))
    w, h = img.size; px = img.load()
    with open(os.path.join(OUT, name + ".rgba"), "wb") as f:
        f.write(struct.pack("<II", w, h))
        for y in range(h):
            row = bytearray()
            for x in range(w):
                r, g, b, a = px[x, y]
                row += struct.pack("<I", (a << 24) | (r << 16) | (g << 8) | b)
            f.write(row)
    print("wrote", name + ".png /", name + ".rgba", f"({w}x{h})")

save(nes(), "nes")
