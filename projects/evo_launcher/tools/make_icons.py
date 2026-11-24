#!/usr/bin/env python3
"""Generate launcher app icons as both PNG (preview) and .rgba (on-unit format).

.rgba format (what evo_launcher reads):
    [u32 LE width][u32 LE height][width*height * u32 LE pixel]
    pixel = 0xAARRGGBB  (matches the launcher's framebuffer write convention)

Style: a rounded-rect panel with a soft vertical gradient + a bold centered glyph,
cohesive across the set (modern flat app-icon look).

This makes the UTILITY tile icons (Terminal / System / GPS). The two game
tiles (DOOM, Quake 3) use each game's own title artwork - see
extract_game_icons.py, which reads it from your doom1.wad / pak0.pk3.
"""
import os, struct, math
from PIL import Image, ImageDraw, ImageFont, ImageFilter

SIZE = 140
RAD  = 28
OUT  = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "icons")
os.makedirs(OUT, exist_ok=True)

def font(sz, bold=True):
    for p in ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold else
              "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
              "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"):
        if os.path.exists(p):
            try: return ImageFont.truetype(p, sz)
            except Exception: pass
    return ImageFont.load_default()

def panel(top, bot):
    """Rounded-rect vertical-gradient panel on transparent bg; returns RGBA image."""
    img = Image.new("RGBA", (SIZE, SIZE), (0,0,0,0))
    grad = Image.new("RGB", (1, SIZE))
    for y in range(SIZE):
        t = y / (SIZE - 1)
        grad.putpixel((0,y), tuple(int(top[i]*(1-t)+bot[i]*t) for i in range(3)))
    grad = grad.resize((SIZE, SIZE))
    mask = Image.new("L", (SIZE, SIZE), 0)
    ImageDraw.Draw(mask).rounded_rectangle([1,1,SIZE-2,SIZE-2], RAD, fill=255)
    img.paste(grad, (0,0), mask)
    # subtle top inner highlight
    hl = Image.new("RGBA", (SIZE, SIZE), (0,0,0,0))
    ImageDraw.Draw(hl).rounded_rectangle([1,1,SIZE-2,SIZE-2], RAD, outline=(255,255,255,40), width=2)
    img = Image.alpha_composite(img, hl)
    return img

def center(draw, xy_box, text, fnt, fill):
    l,t,r,b = draw.textbbox((0,0), text, font=fnt)
    w,h = r-l, b-t
    bx0,by0,bx1,by1 = xy_box
    draw.text((bx0+(bx1-bx0-w)/2 - l, by0+(by1-by0-h)/2 - t), text, font=fnt, fill=fill)

# DOOM + Quake tile icons are built from the games' own title art by
# extract_game_icons.py (not here).

# ---------- Terminal: slate panel + mini window + green prompt ----------
def terminal():
    img = panel((0x2b,0x30,0x36), (0x0d,0x0f,0x12))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([26,30,114,110], 12, fill=(0x0a,0x0d,0x10,255), outline=(0x3a,0x42,0x4a,255), width=2)
    d.rounded_rectangle([26,30,114,46], 12, fill=(0x20,0x26,0x2c,255))
    d.rectangle([26,40,114,46], fill=(0x20,0x26,0x2c,255))
    for i,(c) in enumerate([(0xff,0x5f,0x56),(0xff,0xbd,0x2e),(0x27,0xc9,0x3f)]):
        d.ellipse([34+i*12,35,40+i*12,41], fill=c+(255,))
    d.text((38,58), ">", font=font(30), fill=(0x3a,0xe0,0x6a,255))
    d.rectangle([60,78,84,86], fill=(0x3a,0xe0,0x6a,255))  # cursor block
    return img

# ---------- CPU Monitor: blue panel + chip + CPU ----------
def cpu():
    img = panel((0x16,0x37,0x5e), (0x07,0x14,0x26))
    d = ImageDraw.Draw(img)
    x0,y0,x1,y1 = 40,40,100,100
    for x in range(x0+3, x1-2, 10):         # pins
        d.rectangle([x,y0-10,x+5,y0], fill=(0x8a,0x97,0xa6,255))
        d.rectangle([x,y1,x+5,y1+10], fill=(0x8a,0x97,0xa6,255))
    for y in range(y0+3, y1-2, 10):
        d.rectangle([x0-10,y,x0,y+5], fill=(0x8a,0x97,0xa6,255))
        d.rectangle([x1,y,x1+10,y+5], fill=(0x8a,0x97,0xa6,255))
    d.rounded_rectangle([x0,y0,x1,y1], 8, fill=(0x1e,0x5a,0xb0,255), outline=(0x5a,0x9b,0xe8,255), width=2)
    center(d, (x0,y0,x1,y1), "CPU", font(22), (0xff,0xff,0xff,255))
    return img

# ---------- GPS: teal panel + white location pin on a crosshair ----------
def gps():
    img = panel((0x10,0x6b,0x5c), (0x05,0x24,0x1f))
    d = ImageDraw.Draw(img)
    cx = SIZE//2
    # faint crosshair ring behind the pin
    d.ellipse([cx-40,56,cx+40,136], outline=(0x7f,0xe6,0xd2,90), width=3)
    d.line([cx,22,cx,46], fill=(0x7f,0xe6,0xd2,120), width=3)
    # location pin: round head + pointed base
    head=(0xff,0xff,0xff,255)
    d.ellipse([cx-26,34,cx+26,86], fill=head)
    d.polygon([(cx-20,74),(cx+20,74),(cx,112)], fill=head)
    d.ellipse([cx-11,47,cx+11,69], fill=(0x10,0x6b,0x5c,255))   # inner hole = panel colour
    return img

def save(img, name):
    img.save(os.path.join(OUT, name+".png"))
    w,h = img.size
    px = img.load()
    with open(os.path.join(OUT, name+".rgba"), "wb") as f:
        f.write(struct.pack("<II", w, h))
        for y in range(h):
            row = bytearray()
            for x in range(w):
                r,g,b,a = px[x,y]
                row += struct.pack("<I", (a<<24)|(r<<16)|(g<<8)|b)
            f.write(row)
    print("wrote", name+".png /", name+".rgba", f"({w}x{h})")

save(terminal(), "terminal")
save(cpu(), "cpu")
save(gps(), "gps")
print("icons ->", os.path.abspath(OUT))
