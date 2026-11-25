#!/usr/bin/env python3
"""Generate the Genesis tile icon (png preview + .rgba for the launcher).

Original art in the same panel style as the other tiles: a rounded gradient panel
with a generic game-controller glyph (a three-button pad). No logos, trademarks or console
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

def genesis():
    img = panel((0x2c, 0x2e, 0x36), (0x08, 0x08, 0x0c))            # near-black panel
    body, light, btn = (0x4a, 0x4c, 0x56, 255), (0xb8, 0xba, 0xc4, 255), (0xd0, 0x38, 0x38, 255)
    def draw(d, s):
        # a wide pad, thicker at the grips
        d.ellipse(s(10, 44, 66, 100), fill=body)
        d.ellipse(s(74, 44, 130, 100), fill=body)
        d.rectangle(s(38, 48, 102, 90), fill=body)
        # round D-pad with a cross on it (left)
        cx, cy = 38, 72
        d.ellipse(s(cx - 17, cy - 17, cx + 17, cy + 17), fill=(0x22, 0x22, 0x2a, 255))
        d.rectangle(s(cx - 4, cy - 13, cx + 4, cy + 13), fill=light)
        d.rectangle(s(cx - 13, cy - 4, cx + 13, cy + 4), fill=light)
        # one pill (middle)
        d.rounded_rectangle(s(63, 60, 77, 65), 2 * SS, fill=light)
        # three buttons on a rising arc (right)
        r = 6
        for bx, by in ((88, 82), (102, 74), (116, 68)):
            d.ellipse(s(bx - r, by - r, bx + r, by + r), fill=btn)
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

save(genesis(), "genesis")
