#!/usr/bin/env python3
"""Generate the SNES tile icon (png preview + .rgba for the launcher).

Original art in the same panel style as the other tiles: a rounded gradient panel
with a generic game-controller glyph (a four-button pad). No logos, trademarks or console
trade dress.

.rgba format (what evo_launcher reads): [u32 LE w][u32 LE h][w*h * u32 LE 0xAARRGGBB]
"""
import os, struct
from PIL import Image, ImageDraw

SIZE, RAD, SS = 140, 28, 4          # drawn at 4x and scaled down for smooth edges
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

def glyph(draw_fn):
    """draw_fn(d, s) draws on a transparent SIZE*SS canvas; s scales 140-unit coordinates"""
    big = Image.new("RGBA", (SIZE * SS, SIZE * SS), (0, 0, 0, 0))
    draw_fn(ImageDraw.Draw(big), lambda *v: [x * SS for x in v])
    return big.resize((SIZE, SIZE), Image.LANCZOS)

def snes():
    img = panel((0x3c, 0x34, 0x6e), (0x12, 0x10, 0x26))            # indigo panel
    body, dark, btn = (0xd6, 0xd6, 0xde, 255), (0x2a, 0x2a, 0x34, 255), (0x8c, 0x7c, 0xd8, 255)
    def draw(d, s):
        # a long pad with round ends
        d.ellipse(s(12, 46, 62, 96), fill=body)
        d.ellipse(s(78, 46, 128, 96), fill=body)
        d.rectangle(s(37, 50, 103, 92), fill=body)
        # D-pad cross (left)
        cx, cy = 37, 71
        d.rectangle(s(cx - 5, cy - 14, cx + 5, cy + 14), fill=dark)
        d.rectangle(s(cx - 14, cy - 5, cx + 14, cy + 5), fill=dark)
        # two small pills (middle)
        d.rounded_rectangle(s(58, 70, 67, 75), 2 * SS, fill=dark)
        d.rounded_rectangle(s(72, 70, 81, 75), 2 * SS, fill=dark)
        # four buttons in a diamond (right)
        bx, by, r = 103, 71, 6
        for dx, dy in ((0, -11), (11, 0), (0, 11), (-11, 0)):
            d.ellipse(s(bx + dx - r, by + dy - r, bx + dx + r, by + dy + r), fill=btn)
    return Image.alpha_composite(img, glyph(draw))

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

save(snes(), "snes")
