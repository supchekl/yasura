#!/usr/bin/env python3
# Rough mock of the ARK launcher game screen, following menu.cpp / system_mgr.cpp layout,
# to review the theme without a PSP. Usage: mock.py <resources_dir> <out.png>
import sys

from PIL import Image, ImageDraw, ImageFont

R = sys.argv[1].rstrip("/") + "/"
F = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 10)
FB = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 15)


def img(n):
    return Image.open(R + n).convert("RGBA")


def nearest(im, w, h):
    return im.resize((max(1, w), max(1, h)), Image.NEAREST)


def slice9(canvas, im, x, y, w, h, sb):
    tw, th = im.size
    sb = min(sb, tw // 2, th // 2)
    db = min(sb, w // 2, h // 2)
    su = [0, sb, tw - sb, tw]
    sv = [0, sb, th - sb, th]
    dx = [x, x + db, x + w - db, x + w]
    dy = [y, y + db, y + h - db, y + h]
    for j in range(3):
        for i in range(3):
            part = im.crop((su[i], sv[j], su[i + 1], sv[j + 1]))
            canvas.alpha_composite(nearest(part, dx[i + 1] - dx[i], dy[j + 1] - dy[j]), (dx[i], dy[j]))


def fake_icon(title, c1, c2):
    g = Image.new("RGBA", (144, 80))
    d = ImageDraw.Draw(g)
    for yy in range(80):
        t = yy / 79
        d.line([(0, yy), (143, yy)], fill=tuple(int(c1[k] + (c2[k] - c1[k]) * t) for k in range(3)) + (255,))
    d.text((72, 40), title, font=FB, fill="white", anchor="mm", stroke_width=2, stroke_fill="black")
    return g


games = [fake_icon(*a) for a in [
    ("MINECRAFT", (60, 140, 60), (40, 70, 140)), ("GTA VCS", (230, 120, 40), (120, 30, 80)),
    ("Need for Speed", (30, 40, 60), (90, 110, 140)), ("Tekken 6", (140, 20, 20), (30, 10, 10)),
    ("Crisis Core", (40, 50, 90), (10, 10, 20)), ("Daxter", (200, 160, 40), (90, 60, 20)),
]]
hb = [fake_icon(*a) for a in [("Fast Recovery", (235, 235, 235), (200, 200, 200)),
                              ("PRO Update", (230, 245, 250), (180, 220, 230)),
                              ("ARK Loader", (20, 20, 30), (60, 60, 80))]]

c = img("DEFBG.PNG")
box = img("BOX.PNG")
plate = img("PLATE.PNG")
rim = img("RIM.PNG")

def column(entries, ctype, index, selected):
    x = ctype * 144 + (ctype + 1) * 10 + 5
    y = 20
    cur = None
    i = index - 2
    while y < 272:
        if i < 0:
            y += 40; i += 1; continue
        if i >= len(entries):
            break
        if selected and i == index:
            cur = y; y += 90; i += 1; continue
        c.alpha_composite(nearest(entries[i], 72, 40), (x, y))
        y += 40; i += 1
    if selected:
        ex, ey = x + 5, cur + 5
        slice9(c, plate, ex - 4, ey - 4, 144 + 8, 80 + 8, 24)
        c.alpha_composite(entries[index], (ex, ey))
        slice9(c, rim, ex, ey, 144, 80, 16)

column(hb, 1, 0, False)
column(games, 0, 2, True)
slice9(c, box, 0, 0, 480, 20, 24)
d = ImageDraw.Draw(c)
d.text((5, 4), "Game", font=F, fill=(239, 239, 239))
d.text((330, 4), "2026/10/25 03:58", font=F, fill=(239, 239, 239))
d.rectangle((455, 6, 475, 14), outline=(239, 239, 239))
d.rectangle((457, 8, 470, 12), fill=(239, 239, 239))

# triangle menu, opened
menu = c.copy()
slice9(menu, box, 0, 0, 480, 140, 24)
for k, n in enumerate(["GAME.PNG", "BROWSER.PNG", "SETTINGS.PNG", "PLUGINS.PNG", "EXIT.PNG"]):
    ic = img(n)
    menu.alpha_composite(nearest(ic, 72, 72), (20 + k * 92, 26))

out = Image.new("RGBA", (960, 272 * 2 + 10), (0, 0, 0, 255))
out.alpha_composite(c.resize((480, 272)), (0, 0))
out.alpha_composite(menu, (480, 0))
big = Image.new("RGBA", (960, 1088 // 2 + 272 + 10), (0, 0, 0, 255))
big.alpha_composite(c.resize((960, 544), Image.NEAREST), (0, 0))
big.alpha_composite(menu.resize((480, 272)), (0, 554))
lo = img("LOADING.PNG").resize((480, 272))
big.alpha_composite(lo, (480, 554))
big.convert("RGB").save(sys.argv[2])
