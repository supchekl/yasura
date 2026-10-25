#!/usr/bin/env python3
# Yasura "Liquid Glass" theme for the ARK-4 launcher.
# Draws every resource at 4x and downsamples for smooth edges.
# Usage: gen_theme.py <out_resources_dir> <sounds_from_dir>

import math
import os
import shutil
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

S = 4
ICONS = "/usr/share/fonts/truetype/material-design-icons-iconfont/MaterialIcons-Regular.ttf"
TEXT = "/usr/share/fonts/opentype/inter/Inter-SemiBold.otf"
TEXT_LIGHT = "/usr/share/fonts/opentype/inter/Inter-Regular.otf"

G = {
    "esports": 0xEA28, "settings": 0xE8B8, "folder": 0xE2C7, "extension": 0xE87B,
    "exit": 0xE879, "wifi": 0xE63E, "restore": 0xE8BA, "file": 0xE24D,
    "desc": 0xE873, "music": 0xE405, "image": 0xE3F4, "album": 0xE019,
    "archive": 0xE149, "check": 0xE5CA, "memory": 0xE322, "games": 0xE021,
}


def font(path, px):
    return ImageFont.truetype(path, int(px))


def squircle_mask(w, h, r, n=5.0):
    # Superellipse corners, closer to Apple's continuous curve than a rounded rect.
    m = Image.new("L", (w, h), 0)
    pts = []
    steps = 64
    corners = [(w - r, r, 0), (r, r, 90), (r, h - r, 180), (w - r, h - r, 270)]
    for cx, cy, start in corners:
        for i in range(steps + 1):
            a = math.radians(start + 90 * i / steps)
            c, s = math.cos(a), math.sin(a)
            x = cx + r * math.copysign(abs(c) ** (2 / n), c)
            y = cy - r * math.copysign(abs(s) ** (2 / n), s)
            pts.append((x, y))
    ImageDraw.Draw(m).polygon(pts, fill=255)
    return m


def vgrad(w, h, top, bottom):
    g = Image.new("RGBA", (w, h))
    px = g.load()
    for y in range(h):
        t = y / max(1, h - 1)
        c = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(4))
        for x in range(w):
            px[x, y] = c
    return g


def diag_alpha(w, h, a0, a1):
    # Alpha ramp from top-left (a0) to bottom-right (a1).
    g = Image.new("L", (w, h))
    px = g.load()
    for y in range(h):
        for x in range(w):
            t = (x / w + y / h) / 2
            px[x, y] = int(a0 + (a1 - a0) * t)
    return g


def rim_light(w, h):
    # Bright on the top-left edge, a softer catch light bottom-right, dim in between.
    g = Image.new("L", (w, h))
    px = g.load()
    for y in range(h):
        for x in range(w):
            t = (x / w + y / h) / 2
            px[x, y] = int(70 + 185 * max(0.0, 1 - t * 2.2) + 110 * max(0.0, (t - 0.62) / 0.38))
    return g


def with_alpha(img, mask):
    out = img.copy()
    out.putalpha(ImageChops.multiply(img.getchannel("A"), mask))
    return out


def glass(w, h, r, tint=(255, 255, 255), fill=0.16, dark=False):
    """A Liquid Glass slab: translucent body, top sheen, lit rim, soft shadow."""
    W, H, R = w * S, h * S, r * S
    pad = 6 * S
    canvas = Image.new("RGBA", (W + 2 * pad, H + 2 * pad), (0, 0, 0, 0))
    mask = squircle_mask(W, H, R)

    shadow = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    sm = Image.new("L", canvas.size, 0)
    sm.paste(mask, (pad, pad + 2 * S))
    shadow.putalpha(sm.point(lambda v: v * 0.35))
    shadow = shadow.filter(ImageFilter.GaussianBlur(3 * S))
    # Keep the shadow outside the slab, otherwise it muddies the glass.
    hole = Image.new("L", canvas.size, 0)
    hole.paste(mask, (pad, pad))
    shadow.putalpha(ImageChops.subtract(shadow.getchannel("A"), hole))
    canvas.alpha_composite(shadow)

    if dark:
        body = vgrad(W, H, (40, 44, 70, 170), (14, 16, 30, 190))
    else:
        a = int(255 * fill)
        body = vgrad(W, H, tint + (a + 40,), tint + (a,))
    body = with_alpha(body, mask)

    sheen_h = int(H * 0.55)
    sheen = vgrad(W, sheen_h, (255, 255, 255, 110 if not dark else 50), (255, 255, 255, 0))
    sheen_full = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    sheen_full.paste(sheen, (0, 0))
    body.alpha_composite(with_alpha(sheen_full, mask))

    inner = mask.filter(ImageFilter.GaussianBlur(4 * S))
    edge_glow = ImageChops.subtract(mask, inner)
    glow = Image.new("RGBA", (W, H), (255, 255, 255, 0))
    glow.putalpha(edge_glow.point(lambda v: v * 0.45))
    body.alpha_composite(glow)

    rim_w = max(S, int(1.2 * S))
    shrunk = Image.new("L", (W, H), 0)
    shrunk.paste(squircle_mask(W - 2 * rim_w, H - 2 * rim_w, max(1, R - rim_w)), (rim_w, rim_w))
    ring = ImageChops.subtract(mask, shrunk)
    ring = ImageChops.multiply(ring, rim_light(W, H))
    rim = Image.new("RGBA", (W, H), (255, 255, 255, 0))
    rim.putalpha(ring)
    body.alpha_composite(rim)

    canvas.alpha_composite(body, (pad, pad))
    return canvas, pad


def glyph(img, code, cx, cy, px, color=(255, 255, 255, 255), shadow=True):
    f = font(ICONS, px)
    ch = chr(code)
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    if shadow:
        sh = Image.new("RGBA", img.size, (0, 0, 0, 0))
        ImageDraw.Draw(sh).text((cx, cy + px * 0.04), ch, font=f, fill=(0, 0, 40, 90), anchor="mm")
        img.alpha_composite(sh.filter(ImageFilter.GaussianBlur(px * 0.05)))
    d.text((cx, cy), ch, font=f, fill=color, anchor="mm")
    img.alpha_composite(layer)


def down(img, w, h):
    return img.resize((w, h), Image.LANCZOS)


def app_icon(code, size=100, tile=82):
    slab, pad = glass(tile, tile, int(tile * 0.27))
    big = Image.new("RGBA", (size * S, size * S), (0, 0, 0, 0))
    off = (size * S - slab.width) // 2
    big.alpha_composite(slab, (off, off - 2 * S))
    c = size * S / 2
    glyph(big, code, c, c - 2 * S, tile * 0.52 * S)
    return down(big, size, size)


SMALL_TINTS = {
    "FOLDER.PNG": ("folder", (64, 156, 255)),
    "FILE.PNG": ("file", (142, 142, 160)),
    "TXT.PNG": ("desc", (120, 130, 150)),
    "PBP.PNG": ("games", (175, 82, 222)),
    "PRX.PNG": ("memory", (52, 199, 89)),
    "ISO.PNG": ("album", (255, 149, 0)),
    "ZIP.PNG": ("archive", (172, 142, 104)),
    "MUSIC.PNG": ("music", (255, 45, 85)),
    "PICTURE.PNG": ("image", (48, 176, 199)),
}


def small_icon(code, tint, size=12):
    B = 48 * S
    img = Image.new("RGBA", (B, B), (0, 0, 0, 0))
    m = squircle_mask(B, B, int(B * 0.3))
    body = vgrad(B, B, tuple(min(255, v + 50) for v in tint) + (255,), tint + (255,))
    body = with_alpha(body, m)
    sheen = vgrad(B, B // 2, (255, 255, 255, 120), (255, 255, 255, 0))
    sf = Image.new("RGBA", (B, B), (0, 0, 0, 0))
    sf.paste(sheen, (0, 0))
    body.alpha_composite(with_alpha(sf, m))
    img.alpha_composite(body)
    glyph(img, code, B / 2, B / 2, B * 0.68, shadow=False)
    return down(img, size, size)


def checkbox(on, size=12):
    B = 48 * S
    img = Image.new("RGBA", (B, B), (0, 0, 0, 0))
    m = squircle_mask(B, B, int(B * 0.3))
    if on:
        body = with_alpha(vgrad(B, B, (90, 180, 255, 255), (10, 122, 255, 255)), m)
        img.alpha_composite(body)
        glyph(img, G["check"], B / 2, B / 2, B * 0.8, shadow=False)
    else:
        body = with_alpha(vgrad(B, B, (255, 255, 255, 90), (255, 255, 255, 40)), m)
        img.alpha_composite(body)
        ring = ImageChops.subtract(m, m.filter(ImageFilter.MinFilter(4 * S + 1)))
        r = Image.new("RGBA", (B, B), (255, 255, 255, 0))
        r.putalpha(ring.point(lambda v: v * 0.9))
        img.alpha_composite(r)
    return down(img, size, size)


def wallpaper(w=480, h=272, blur=38):
    W, H = w * S // 2, h * S // 2
    img = vgrad(W, H, (12, 18, 52, 255), (34, 10, 60, 255))
    blobs = [
        (0.15, 0.20, 0.42, (0, 122, 255)),
        (0.80, 0.15, 0.38, (175, 82, 222)),
        (0.62, 0.85, 0.45, (255, 45, 140)),
        (0.05, 0.95, 0.35, (0, 199, 190)),
        (0.95, 0.65, 0.28, (255, 159, 10)),
    ]
    for bx, by, br, col in blobs:
        layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        r = br * W
        ImageDraw.Draw(layer).ellipse(
            (bx * W - r, by * H - r * 0.8, bx * W + r, by * H + r * 0.8), fill=col + (210,)
        )
        img.alpha_composite(layer)
    img = img.filter(ImageFilter.GaussianBlur(blur * S / 2))
    return down(img, w, h)


def text(img, xy, s, px, fill, path=TEXT, anchor="mm"):
    ImageDraw.Draw(img).text(xy, s, font=font(path, px), fill=fill, anchor=anchor)


def loading_screen(bg):
    w, h = 480, 272
    big = bg.resize((w * S, h * S), Image.LANCZOS).convert("RGBA")
    slab, pad = glass(220, 96, 30, fill=0.14)
    big.alpha_composite(slab, ((w * S - slab.width) // 2, (h * S - slab.height) // 2))
    text(big, (w * S / 2, h * S / 2 - 12 * S), "Yasura", 40 * S, (255, 255, 255, 255))
    text(big, (w * S / 2, h * S / 2 + 24 * S), "ARK-4  ·  PSP Street", 12 * S,
         (255, 255, 255, 200), TEXT_LIGHT)
    return down(big, w, h)


def card(code, label, w=144, h=80):
    slab, pad = glass(w - 8, h - 8, 18, fill=0.18)
    big = Image.new("RGBA", (w * S, h * S), (0, 0, 0, 0))
    big.alpha_composite(slab, ((w * S - slab.width) // 2, (h * S - slab.height) // 2 - S))
    glyph(big, code, w * S / 2, h * S / 2 - 9 * S, 30 * S)
    text(big, (w * S / 2, h * S / 2 + 18 * S), label, 11 * S, (255, 255, 255, 235))
    return down(big, w, h)


def spinner(size=80):
    B = size * S
    img = Image.new("RGBA", (B, B), (0, 0, 0, 0))
    slab, pad = glass(56, 56, 28, fill=0.14)
    img.alpha_composite(slab, ((B - slab.width) // 2, (B - slab.height) // 2))
    arc = Image.new("RGBA", (B, B), (0, 0, 0, 0))
    d = ImageDraw.Draw(arc)
    r = 18 * S
    c = B / 2
    for i in range(60):
        a0 = -90 + i * 4.5
        alpha = int(255 * (i / 60) ** 1.6)
        d.arc((c - r, c - r, c + r, c + r), a0, a0 + 5, fill=(255, 255, 255, alpha), width=4 * S)
    img.alpha_composite(arc)
    return down(img, size, size)


def bubble(size=22):
    B = size * S
    img = Image.new("RGBA", (B, B), (0, 0, 0, 0))
    m = Image.new("L", (B, B), 0)
    ImageDraw.Draw(m).ellipse((S, S, B - S, B - S), fill=255)
    img.alpha_composite(with_alpha(vgrad(B, B, (255, 255, 255, 90), (255, 255, 255, 25)), m))
    ring = ImageChops.subtract(m, m.filter(ImageFilter.MinFilter(2 * S + 1)))
    ring = ImageChops.multiply(ring, diag_alpha(B, B, 255, 80))
    r = Image.new("RGBA", (B, B), (255, 255, 255, 0))
    r.putalpha(ring)
    img.alpha_composite(r)
    hl = Image.new("RGBA", (B, B), (0, 0, 0, 0))
    ImageDraw.Draw(hl).ellipse((B * 0.28, B * 0.2, B * 0.48, B * 0.36), fill=(255, 255, 255, 200))
    img.alpha_composite(hl.filter(ImageFilter.GaussianBlur(S)))
    return down(img, size, size)


def dialog_box(size=256):
    slab, pad = glass(size - 12, size - 12, 34, dark=True)
    big = Image.new("RGBA", (size * S, size * S), (0, 0, 0, 0))
    big.alpha_composite(slab, ((size * S - slab.width) // 2, (size * S - slab.height) // 2))
    return down(big, size, size)


def main():
    out, sounds = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    bg = wallpaper()
    bg.save(os.path.join(out, "DEFBG.PNG"))
    loading_screen(bg).save(os.path.join(out, "LOADING.PNG"))
    for name, key in [("GAME.PNG", "esports"), ("BROWSER.PNG", "folder"), ("SETTINGS.PNG", "settings"),
                      ("PLUGINS.PNG", "extension"), ("EXIT.PNG", "exit"), ("FTP.PNG", "wifi")]:
        app_icon(G[key]).save(os.path.join(out, name))
    for name, (key, tint) in SMALL_TINTS.items():
        small_icon(G[key], tint).save(os.path.join(out, name))
    checkbox(True).save(os.path.join(out, "CHECK.PNG"))
    checkbox(False).save(os.path.join(out, "UNCHECK.PNG"))
    card(G["image"], "NO ICON").save(os.path.join(out, "NOICON.PNG"))
    card(G["restore"], "Recovery").save(os.path.join(out, "RECOVERY.PNG"))
    spinner().save(os.path.join(out, "WAIT.PNG"))
    bubble().save(os.path.join(out, "SPRITE.PNG"))
    dialog_box().save(os.path.join(out, "BOX.PNG"))
    for snd in ("BOOT.MP3", "SOUND.MP3"):
        shutil.copy(os.path.join(sounds, snd), os.path.join(out, snd))
    # Full-size XMB wallpaper, set on the PSP via Settings > Theme Settings > Wallpaper.
    wallpaper().convert("RGB").save(os.path.join(os.path.dirname(out.rstrip("/")), "XMB_WALLPAPER.png"))


if __name__ == "__main__":
    main()
