#!/usr/bin/env python3
"""The pc platform's settings menu art (docs/platforms.md, "The settings menu").

    ui_assets.py --fonts-cpp hud_font_tables.cpp --small small.png --large large.png
                 --pause pause.png -o ui_assets.c

The PC settings menu (platform/pc/src/ui.c) is drawn by the platform, not
the game, with the game's own pieces, so that it looks like the game's
menus: the HUD fonts (games/xs/graphics/fonts/, the glyph order and
widths of gSmallFontChars/gSmallFontGlyphs and gLargeFontChars/
gLargeFontGlyphs in hud_font_tables.cpp) and the pause screen's art
(games/xs/graphics/menus/pause.png). This writes them as C arrays:
each glyph's 16x16 cell of palette indices, its advance and its Y offset,
a char -> glyph table, the font palette (the PNG's, as 15-bit colours),
and the pause art's 240x160 indices and 256-colour palette.
"""

import argparse
import json
import os
import re

from PIL import Image


def c_string(src, name):
    m = re.search(name + r'\[\] = "((?:[^"\\]|\\.)*)"', src)
    if not m:
        raise SystemExit(f"ui_assets.py: no {name} in the font tables")
    s = m.group(1)
    out = []
    i = 0
    while i < len(s):
        if s[i] == "\\":
            j = i + 1
            while j < len(s) and j < i + 4 and s[j] in "01234567":
                j += 1
            out.append(int(s[i + 1:j], 8))
            i = j
        else:
            out.append(ord(s[i]))
            i += 1
    return out


def metrics(src, name, n):
    i = src.index(f"{name}[{n}]")
    found = re.findall(r"\{ (\d+), (\d+), (\d+),", src[i:i + 8000])[:n]
    if len(found) != n:
        raise SystemExit(f"ui_assets.py: {name} has {len(found)} glyphs, not {n}")
    return [(int(w), int(y) - 256 if int(y) > 127 else int(y)) for w, _, y in found]


def bgr15(rgb):
    r, g, b = (c >> 3 for c in rgb)
    return r | g << 5 | b << 10


def palette15(img, n):
    p = img.getpalette()
    p += [0] * (3 * n - len(p))
    return [bgr15(p[3 * i:3 * i + 3]) for i in range(n)]


def font(out, prefix, img, chars, mets):
    px = img.load()
    out.append(f"const unsigned char {prefix}Cells[{len(mets)}][256] = {{")
    for g in range(len(mets)):
        cx, cy = (g % 16) * 16, (g // 16) * 16
        cell = [px[cx + x, cy + y] for y in range(16) for x in range(16)]
        out.append("    {" + ",".join(str(v) for v in cell) + "},")
    out.append("};")
    out.append(f"const signed char {prefix}Advance[{len(mets)}] = {{" + ",".join(str(w) for w, _ in mets) + "};")
    out.append(f"const signed char {prefix}YOffset[{len(mets)}] = {{" + ",".join(str(y) for _, y in mets) + "};")
    lookup = [255] * 256
    for g, c in enumerate(chars):
        if lookup[c] == 255:
            lookup[c] = g
    out.append(f"const unsigned char {prefix}Lookup[256] = {{" + ",".join(str(v) for v in lookup) + "};")


# The themed borders' stats panel (borders.c): the pause menu's icons,
# each an animation's first frame (or a power's front-facing one) in that
# animation's palette (bank.json frames, the anims' paletteId in
# sprite_banks_22_38.cpp: the pause pages' gPause*IconFrames pick the
# animations), at half size as the pause pages draw the gems and relics
# (affine 0.5): (name, bank, frame, palette, half).
ICONS = [
    ("CRYSTAL", 37, 0, 117, 1),
    ("GEM_CLEAR", 32, 0, 50, 1),
    ("GEM_RED", 32, 0, 52, 1),
    ("GEM_GREEN", 32, 8, 51, 1),
    ("GEM_BLUE", 32, 0, 53, 1),
    ("GEM_YELLOW", 32, 0, 49, 1),
    ("RELIC_PLATINUM", 33, 8, 55, 1),
    ("RELIC_GOLD", 33, 8, 56, 1),
    ("RELIC_SAPPHIRE", 33, 0, 54, 1),
    ("POWER_SUPER_BODY_SLAM", 38, 45 + 7, 60, 1),
    ("POWER_DOUBLE_JUMP", 38, 30 + 7, 60, 1),
    ("POWER_TORNADO_SPIN", 38, 0 + 7, 60, 1),
    ("POWER_TURBO_RUN", 38, 15 + 7, 60, 1),
]


def jasc(path):
    lines = open(path).read().split("\n")
    if lines[0].strip() != "JASC-PAL":
        raise SystemExit(f"ui_assets.py: {path} isn't a JASC palette")
    return [tuple(int(v) for v in l.split()) for l in lines[3:3 + int(lines[2])]]


def icons(out, sprites):
    import glob

    names = []
    for name, bank, frame, pal, half in ICONS:
        png = glob.glob(os.path.join(sprites, f"bank{bank:02d}_*", f"f{frame:03d}.png"))
        palf = glob.glob(os.path.join(sprites, "palettes", f"{pal:03d}_*.pal"))
        if len(png) != 1 or len(palf) != 1:
            raise SystemExit(f"ui_assets.py: no bank {bank} frame {frame} or palette {pal}")
        im = Image.open(png[0])
        colors = jasc(palf[0])
        px = im.load()
        w, h = im.size
        step = 2 if half else 1
        rows = [[px[x, y] for x in range(0, w - step + 1, step)] for y in range(0, h - step + 1, step)]
        # cropped to what it draws
        ys = [y for y, r in enumerate(rows) if any(r)]
        xs = [x for x in range(len(rows[0])) if any(r[x] for r in rows)]
        rows = [r[xs[0]:xs[-1] + 1] for r in rows[ys[0]:ys[-1] + 1]]
        argb = []
        for r in rows:
            for i in r:
                if i == 0:
                    argb.append(0)
                else:
                    c = tuple((v >> 3) << 3 | (v >> 3) >> 2 for v in colors[i])
                    argb.append(0xFF000000 | c[0] << 16 | c[1] << 8 | c[2])
        out.append(f"static const unsigned int sIcon{len(names)}[] = {{" + ",".join(str(v) for v in argb) + "};")
        names.append((name, len(rows[0]), len(rows)))
    out.append("const struct UiIcon { int w; int h; const unsigned int *px; } gUiIcons[] = {")
    for k, (name, w, h) in enumerate(names):
        out.append(f"    {{ {w}, {h}, sIcon{k} }}, /* {name} */")
    out.append("};")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--fonts-cpp", required=True)
    ap.add_argument("--small", required=True)
    ap.add_argument("--large", required=True)
    ap.add_argument("--pause", required=True)
    ap.add_argument("--levels", help="levels.json: each level's name and theme, each theme's picture")
    ap.add_argument("--graphics", help="the graphics folder the theme pictures are in")
    ap.add_argument("--sprites", help="the sprite banks' folder: the stats panel's icons")
    ap.add_argument("-o", "--output", required=True)
    args = ap.parse_args()

    src = open(args.fonts_cpp, encoding="latin-1").read()
    small = Image.open(args.small)
    large = Image.open(args.large)
    pause = Image.open(args.pause)
    if pause.size != (240, 160) or pause.mode != "P":
        raise SystemExit("ui_assets.py: the pause art isn't a 240x160 indexed picture")

    out = ["/* Generated by platform/pc/tools/ui_assets.py from the game's fonts and pause art. */", ""]
    font(out, "gUiSmallFont", small, c_string(src, "gSmallFontChars"), metrics(src, "gSmallFontGlyphs", 79))
    font(out, "gUiLargeFont", large, c_string(src, "gLargeFontChars"), metrics(src, "gLargeFontGlyphs", 75))
    out.append("const unsigned short gUiFontPalette[16] = {" + ",".join(str(c) for c in palette15(small, 16)) + "};")
    px = pause.load()
    out.append("const unsigned char gUiPauseArt[160][240] = {")
    for y in range(160):
        out.append("    {" + ",".join(str(px[x, y]) for x in range(240)) + "},")
    out.append("};")
    out.append("const unsigned short gUiPausePalette[256] = {" + ",".join(str(c) for c in palette15(pause, 256)) + "};")
    if args.levels:
        # The themed borders (borders.c): each level's name and the porthole
        # picture of its theme (the level select's), -1 for none.
        data = json.load(open(args.levels))
        themes = data["themes"]
        pics = []
        theme_pic = []
        for t in themes:
            if "picture" in t:
                theme_pic.append(len(pics))
                pics.append(Image.open(os.path.join(args.graphics, t["picture"])))
            else:
                theme_pic.append(-1)
        levels = data["levels"]
        out.append(f"const int gUiLevelCount = {len(levels)};")
        out.append("const char *const gUiLevelNames[] = {" + ",".join(json.dumps(l["name"]) for l in levels) + "};")
        out.append("const signed char gUiLevelPicture[] = {" +
                   ",".join(str(theme_pic[l["theme"]]) for l in levels) + "};")
        out.append(f"const int gUiPictureCount = {len(pics)};")
        out.append(f"const unsigned char gUiPictures[{len(pics)}][64 * 64] = {{")
        for im in pics:
            if im.size != (64, 64) or im.mode != "P":
                raise SystemExit("ui_assets.py: a theme picture isn't a 64x64 indexed picture")
            px = im.load()
            out.append("    {" + ",".join(str(px[x, y]) for y in range(64) for x in range(64)) + "},")
        out.append("};")
        out.append(f"const unsigned short gUiPicturePalettes[{len(pics)}][256] = {{")
        for im in pics:
            out.append("    {" + ",".join(str(c) for c in palette15(im, 256)) + "},")
        out.append("};")
    if args.sprites:
        icons(out, args.sprites)
    with open(args.output, "w") as f:
        f.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
