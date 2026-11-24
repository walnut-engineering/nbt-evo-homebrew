#!/usr/bin/env python3
"""Build the DOOM + Quake launcher tile icons from YOUR OWN game data.

The two game tiles use each game's own title artwork, read straight out of the
licensed data files you already run on the unit - format-converted into the
launcher's .rgba icon format, not redrawn:

    DOOM    <- the M_DOOM title graphic in doom1.wad   (DOOM picture format + PLAYPAL)
    Quake 3 <- textures/sfx/logo512.jpg in pak0.pk3    (a plain zip)

Because the output icons are the games' copyrighted logos, they are .gitignore'd
(like the WAD/pak themselves); this script regenerates them from your data.

Usage:
    extract_game_icons.py [DOOM1_WAD] [PAK0_PK3]
Defaults:
    DOOM1_WAD = ../../doom_evo/doom1.wad   (relative to this script)
    PAK0_PK3  = must be given, or pull it first:
                evo_ssh 'cat /fs/sda1/evo/q3/demoq3/pak0.pk3' > pak0.pk3

Output: projects/evo_launcher/icons/{doom,quake}.{rgba,png}

.rgba format (what evo_launcher reads):
    [u32 LE width][u32 LE height][width*height * u32 LE 0xAARRGGBB]
"""
import os, sys, struct, zipfile
from PIL import Image, ImageDraw

HERE  = os.path.dirname(os.path.abspath(__file__))
ICONS = os.path.join(HERE, "..", "icons")
SIZE, RAD = 140, 28


# ---------- launcher icon format + cohesive panel ----------
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
    return Image.alpha_composite(img, hl), mask


def fit(logo, maxw, maxh):
    w, h = logo.size
    s = min(maxw / w, maxh / h)
    return logo.resize((max(1, int(w * s)), max(1, int(h * s))), Image.LANCZOS)


def save(img, name):
    img.save(os.path.join(ICONS, name + ".png"))
    w, h = img.size
    px = img.load()
    with open(os.path.join(ICONS, name + ".rgba"), "wb") as f:
        f.write(struct.pack("<II", w, h))
        for y in range(h):
            row = bytearray()
            for x in range(w):
                r, g, b, a = px[x, y]
                row += struct.pack("<I", (a << 24) | (r << 16) | (g << 8) | b)
            f.write(row)
    print("wrote", name + ".png /", name + ".rgba", "(%dx%d)" % (w, h))


# ---------- DOOM: extract a named picture lump from the IWAD ----------
def wad_picture(wad_path, lump_name):
    data = open(wad_path, "rb").read()
    _, numlumps, infoofs = struct.unpack_from("<4sII", data, 0)
    ents = {}
    for i in range(numlumps):
        pos, size, name = struct.unpack_from("<II8s", data, infoofs + i * 16)
        ents[name.rstrip(b"\x00").decode("latin1")] = (pos, size)
    pal = data[ents["PLAYPAL"][0]:ents["PLAYPAL"][0] + 768]
    palette = [(pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]) for i in range(256)]
    pos, _ = ents[lump_name]
    w, h = struct.unpack_from("<HH", data, pos)
    colofs = struct.unpack_from("<%dI" % w, data, pos + 8)
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = img.load()
    for x in range(w):
        cp = pos + colofs[x]
        while data[cp] != 0xFF:              # 0xFF terminates the column
            topdelta, length = data[cp], data[cp + 1]
            cp += 3                          # topdelta, length, unused-pad
            for y in range(length):
                if 0 <= topdelta + y < h:
                    px[x, topdelta + y] = palette[data[cp + y]] + (255,)
            cp += length + 1                 # pixels + trailing unused-pad
    return img


def build_doom(wad_path):
    p, mask = panel((0x6b, 0x16, 0x16), (0x14, 0x04, 0x04))
    logo = fit(wad_picture(wad_path, "M_DOOM"), 122, 96)
    p.alpha_composite(logo, ((SIZE - logo.width) // 2, (SIZE - logo.height) // 2))
    p.putalpha(mask)
    save(p, "doom")


# ---------- Quake: key the logo's black backdrop to transparent ----------
def build_quake(pak_path):
    with zipfile.ZipFile(pak_path) as z:
        import io
        logo = Image.open(io.BytesIO(z.read("textures/sfx/logo512.jpg"))).convert("RGB")
    p, mask = panel((0x1e, 0x16, 0x18), (0x05, 0x04, 0x05))
    w, h = logo.size
    lp = logo.load()
    keyed = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    kp = keyed.load()
    for y in range(h):
        for x in range(w):
            r, g, b = lp[x, y]
            m = max(r, g, b)
            a = 0 if m <= 10 else min(255, (m - 10) * 6)
            kp[x, y] = (r, g, b, a)
    keyed = fit(keyed, 120, 120)
    p.alpha_composite(keyed, ((SIZE - keyed.width) // 2, (SIZE - keyed.height) // 2))
    p.putalpha(mask)
    save(p, "quake")


def main():
    wad = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "doom_evo", "doom1.wad")
    pak = sys.argv[2] if len(sys.argv) > 2 else None
    if os.path.exists(wad):
        build_doom(wad)
    else:
        print("! DOOM wad not found:", wad, "(skipping doom icon)")
    if pak and os.path.exists(pak):
        build_quake(pak)
    else:
        print("! Quake pak not given/found; pull it then pass it:")
        print("    evo_ssh 'cat /fs/sda1/evo/q3/demoq3/pak0.pk3' > pak0.pk3")
        print("    extract_game_icons.py %s pak0.pk3" % wad)
    print("icons ->", os.path.abspath(ICONS))


if __name__ == "__main__":
    main()
