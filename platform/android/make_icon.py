#!/usr/bin/env python3
"""The app's icon: a sprite (index 0 clear) cropped, squared, scaled up
with its pixels sharp to 192x192 (make_icon.py SPRITE.png OUT.png)."""
import sys
from PIL import Image

im = Image.open(sys.argv[1])
rgba = im.convert("RGBA")
px, out = im.load(), rgba.load()
for y in range(im.height):
    for x in range(im.width):
        if px[x, y] == 0:
            out[x, y] = (0, 0, 0, 0)
rgba = rgba.crop(rgba.getbbox())
s = max(rgba.size) + 4
sq = Image.new("RGBA", (s, s), (0, 0, 0, 0))
sq.paste(rgba, ((s - rgba.width) // 2, (s - rgba.height) // 2))
sq.resize((192, 192), Image.NEAREST).save(sys.argv[2])
