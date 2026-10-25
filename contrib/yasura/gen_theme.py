#!/usr/bin/env python3
# Yasura "Liquid Glass" theme for the ARK-4 launcher.
# Draws every resource at 4x and downsamples for smooth edges.
# Usage: gen_theme.py <out_resources_dir> <sounds_from_dir>

import math
import os
import shutil
import sys

import io

import cairosvg
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

S = 4
ICONS = "/usr/share/fonts/truetype/material-design-icons-iconfont/MaterialIcons-Regular.ttf"
TEXT = "/usr/share/fonts/opentype/inter/Inter-SemiBold.otf"
TEXT_LIGHT = "/usr/share/fonts/opentype/inter/Inter-Regular.otf"
# Ionicons (MIT, drawn to match iOS), the SVGs used are copied next to this script
IONICONS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ionicons")

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


def wallpaper(w=480, h=272):
    """Bright iOS 26 style wallpaper: deep blue to violet to warm coral, soft light blobs
    and two glass ribbons. Mid tones keep white labels readable on the PSP LCD."""
    W, H = w * S // 2, h * S // 2
    img = vgrad(W, H, (22, 44, 150, 255), (70, 28, 120, 255))
    glows = [
        (0.10, 0.10, 0.55, (40, 120, 255), 200),
        (0.55, 0.45, 0.50, (140, 70, 230), 170),
        (0.95, 0.95, 0.55, (255, 110, 90), 190),
        (0.85, 0.05, 0.35, (255, 90, 170), 120),
        (0.15, 1.00, 0.45, (20, 170, 220), 130),
    ]
    for gx, gy, gr, col, a in glows:
        layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        r = gr * W
        ImageDraw.Draw(layer).ellipse((gx * W - r, gy * H - r * 0.8, gx * W + r, gy * H + r * 0.8), fill=col + (a,))
        img.alpha_composite(layer.filter(ImageFilter.GaussianBlur(W * 0.11)))

    for amp, off, phase, thick, a in ((0.14, 0.50, 0.3, 0.22, 40), (0.10, 0.70, 2.0, 0.14, 30)):
        band = Image.new("L", (W, H), 0)
        edge = Image.new("L", (W, H), 0)
        top = []
        bot = []
        for i in range(W + 1):
            t = i / W
            y = (off + amp * math.sin(t * math.pi * 1.5 + phase)) * H
            top.append((i, y))
            bot.append((i, y + thick * H * (0.7 + 0.3 * math.cos(t * 3 + phase))))
        ImageDraw.Draw(band).polygon(top + bot[::-1], fill=a)
        ImageDraw.Draw(edge).line(top, fill=170, width=max(1, S // 2))
        band = band.filter(ImageFilter.GaussianBlur(S * 1.5))
        edge = edge.filter(ImageFilter.GaussianBlur(S * 0.8))
        white = Image.new("RGBA", (W, H), (255, 255, 255, 0))
        white.putalpha(ImageChops.add(band, edge))
        img.alpha_composite(white)
    # a touch darker at the top for the status bar and at the bottom for the dock
    shade = Image.new("L", (W, H), 0)
    sp = shade.load()
    for y in range(H):
        t = y / H
        v = int(70 * max(0.0, 1 - t / 0.12) + 60 * max(0.0, (t - 0.7) / 0.3))
        for x in range(W):
            sp[x, y] = v
    dark = Image.new("RGBA", (W, H), (0, 0, 20, 0))
    dark.putalpha(shade)
    img.alpha_composite(dark)
    return down(img, w, h)


def ion(name, px, color=(255, 255, 255)):
    """Render an Ionicons SVG as a white glyph px wide."""
    with open(os.path.join(IONICONS, name + ".svg")) as f:
        svg = f.read()
    svg = svg.replace("<svg ", '<svg fill="#%02x%02x%02x" ' % color, 1)
    png = cairosvg.svg2png(bytestring=svg.encode(), output_width=int(px), output_height=int(px))
    return Image.open(io.BytesIO(png)).convert("RGBA")


def ios_icon(name, top, bottom, size=100, tile=84, glyph_frac=0.56):
    """iOS style app icon: colored squircle, white glyph, Liquid Glass sheen and rim."""
    B, T = size * S, tile * S
    big = Image.new("RGBA", (B, B), (0, 0, 0, 0))
    off = (B - T) // 2
    mask = squircle_mask(T, T, T // 2)

    sh = Image.new("L", (B, B), 0)
    sh.paste(mask, (off, off + 3 * S))
    shadow = Image.new("RGBA", (B, B), (0, 0, 30, 0))
    shadow.putalpha(sh.point(lambda v: v * 0.45).filter(ImageFilter.GaussianBlur(4 * S)))
    big.alpha_composite(shadow)

    body = with_alpha(vgrad(T, T, top + (255,), bottom + (255,)), mask)
    g = ion(name, T * glyph_frac)
    gs = Image.new("RGBA", (T, T), (0, 0, 0, 0))
    gl = Image.new("RGBA", (T, T), (0, 0, 0, 0))
    gp = ((T - g.width) // 2, (T - g.height) // 2)
    gl.alpha_composite(g, gp)
    gs.alpha_composite(g, (gp[0], gp[1] + int(T * 0.02)))
    sh2 = Image.new("RGBA", (T, T), (0, 0, 40, 0))
    sh2.putalpha(gs.getchannel("A").point(lambda v: v * 0.35).filter(ImageFilter.GaussianBlur(T * 0.02)))
    body.alpha_composite(sh2)
    body.alpha_composite(gl)
    big.alpha_composite(body, (off, off))
    big.alpha_composite(glass_overlay(tile, full=True), (off, off))
    return down(big, size, size)


def glass_overlay(size=64, full=False):
    """Liquid Glass finish for any squircle icon: top sheen plus a lit rim (GLOSS.PNG)."""
    B = size * S
    m = squircle_mask(B, B, B // 2)
    img = Image.new("RGBA", (B, B), (0, 0, 0, 0))
    sheen = vgrad(B, int(B * 0.5), (255, 255, 255, 70), (255, 255, 255, 0))
    sf = Image.new("RGBA", (B, B), (0, 0, 0, 0))
    sf.paste(sheen, (0, 0))
    img.alpha_composite(with_alpha(sf, m))
    rw = max(S, int(B * 0.025))
    inner = Image.new("L", (B, B), 0)
    inner.paste(squircle_mask(B - 2 * rw, B - 2 * rw, (B - 2 * rw) // 2), (rw, rw))
    ring = ImageChops.multiply(ImageChops.subtract(m, inner), rim_light(B, B))
    rl = Image.new("RGBA", (B, B), (255, 255, 255, 0))
    rl.putalpha(ring.point(lambda v: v * 0.8))
    img.alpha_composite(rl)
    return img if full else down(img, size, size)


def dock_panel():
    # Frosted light glass behind the system apps (nine-sliced, border 24).
    return panel(64, 22, (255, 255, 255, 64), (255, 255, 255, 40), 230, 46)


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


def panel(size, r, top, bottom, rim_a, sheen_a):
    """Square nine-slice source: rounded body, top sheen, thin lit rim."""
    B = size * S
    m = squircle_mask(B, B, r * S)
    img = with_alpha(vgrad(B, B, top, bottom), m)
    sheen = vgrad(B, int(B * 0.45), (255, 255, 255, sheen_a), (255, 255, 255, 0))
    sf = Image.new("RGBA", (B, B), (0, 0, 0, 0))
    sf.paste(sheen, (0, 0))
    img.alpha_composite(with_alpha(sf, m))
    rw = max(S, int(0.9 * S))
    inner = Image.new("L", (B, B), 0)
    inner.paste(squircle_mask(B - 2 * rw, B - 2 * rw, max(1, (r * S) - rw)), (rw, rw))
    ring = ImageChops.subtract(m, inner)
    ring = ImageChops.multiply(ring, vgrad(B, B, (0, 0, 0, rim_a), (0, 0, 0, rim_a // 3)).getchannel("A"))
    rl = Image.new("RGBA", (B, B), (255, 255, 255, 0))
    rl.putalpha(ring)
    img.alpha_composite(rl)
    return down(img, size, size)


def dialog_box():
    # Used for the top bar, the triangle menu and every popup (drawn nine-sliced, border 24).
    return panel(64, 20, (34, 38, 58, 175), (16, 18, 30, 200), 170, 36)


def plate():
    # Glass backdrop behind the selected game (nine-sliced, border 24).
    return panel(64, 18, (255, 255, 255, 52), (255, 255, 255, 26), 235, 60)


def rim(size=48):
    # Overlay on the selected game icon: a soft top reflection (nine-sliced, border 16).
    B = size * S
    img = Image.new("RGBA", (B, B), (0, 0, 0, 0))
    m = squircle_mask(B, B, 5 * S)
    sheen = vgrad(B, 16 * S, (255, 255, 255, 46), (255, 255, 255, 0))
    sf = Image.new("RGBA", (B, B), (0, 0, 0, 0))
    sf.paste(sheen, (0, 0))
    img.alpha_composite(with_alpha(sf, m))
    return down(img, size, size)



TEXT_BOLD = "/usr/share/fonts/opentype/inter/Inter-Bold.otf"


def _btn_base(w, h):
    B_w, B_h = w * S, h * S
    img = Image.new("RGBA", (B_w, B_h), (0, 0, 0, 0))
    m = squircle_mask(B_w, B_h, min(B_w, B_h) // 2, n=2.0)
    img.alpha_composite(with_alpha(vgrad(B_w, B_h, (255, 255, 255, 70), (255, 255, 255, 30)), m))
    inner = Image.new("L", (B_w, B_h), 0)
    inner.paste(squircle_mask(B_w - 2 * S, B_h - 2 * S, min(B_w, B_h) // 2 - S, n=2.0), (S, S))
    ring = ImageChops.subtract(m, inner)
    rl = Image.new("RGBA", (B_w, B_h), (255, 255, 255, 0))
    rl.putalpha(ring.point(lambda v: v * 0.6))
    img.alpha_composite(rl)
    return img


def face_button(kind, size=14):
    """PSP face buttons in their classic colours on a small glass disc."""
    img = _btn_base(size, size)
    d = ImageDraw.Draw(img)
    B = size * S
    c = B / 2
    r = B * 0.24
    lw = int(1.6 * S)
    if kind == "X":
        col = (130, 180, 255, 255)
        d.line((c - r, c - r, c + r, c + r), fill=col, width=lw)
        d.line((c - r, c + r, c + r, c - r), fill=col, width=lw)
    elif kind == "O":
        col = (255, 110, 130, 255)
        d.ellipse((c - r, c - r, c + r, c + r), outline=col, width=lw)
    elif kind == "T":
        col = (100, 225, 165, 255)
        d.polygon([(c, c - r * 1.1), (c + r * 1.05, c + r * 0.75), (c - r * 1.05, c + r * 0.75)], outline=col, width=lw)
    elif kind == "S":
        col = (255, 140, 215, 255)
        d.rectangle((c - r * 0.9, c - r * 0.9, c + r * 0.9, c + r * 0.9), outline=col, width=lw)
    return down(img, size, size)


def pill_button(label, w, h=14, px=8):
    img = _btn_base(w, h)
    text(img, (w * S / 2, h * S / 2 + 0.3 * S), label, px * S, (255, 255, 255, 255), TEXT_BOLD)
    return down(img, w, h)


def dpad(arms, size=14):
    """D-pad glyph; the arms named in `arms` (u, d, l, r) are lit."""
    B = size * S
    img = Image.new("RGBA", (B, B), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    t = B * 0.34
    c = B / 2
    boxes = {
        "u": (c - t / 2, S, c + t / 2, c - t / 2 + S),
        "d": (c - t / 2, c + t / 2 - S, c + t / 2, B - S),
        "l": (S, c - t / 2, c - t / 2 + S, c + t / 2),
        "r": (c + t / 2 - S, c - t / 2, B - S, c + t / 2),
    }
    d.rectangle((c - t / 2, c - t / 2, c + t / 2, c + t / 2), fill=(255, 255, 255, 110))
    for k, box in boxes.items():
        d.rounded_rectangle(box, radius=S, fill=(255, 255, 255, 255 if k in arms else 90))
    return down(img, size, size)


def hint_buttons(out):
    for k in "XOTS":
        face_button(k).save(os.path.join(out, "BTN_%s.PNG" % k))
    pill_button("L", 18).save(os.path.join(out, "BTN_L.PNG"))
    pill_button("R", 18).save(os.path.join(out, "BTN_R.PNG"))
    pill_button("START", 34, px=7).save(os.path.join(out, "BTN_ST.PNG"))
    pill_button("SELECT", 38, px=7).save(os.path.join(out, "BTN_SE.PNG"))
    dpad("ud").save(os.path.join(out, "BTN_UD.PNG"))
    dpad("lr").save(os.path.join(out, "BTN_LR.PNG"))
    dpad("udlr").save(os.path.join(out, "BTN_DP.PNG"))

def main():
    out, sounds = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    bg = wallpaper()
    bg.save(os.path.join(out, "DEFBG.PNG"))
    loading_screen(bg).save(os.path.join(out, "LOADING.PNG"))
    for name, svg, top, bottom in [
        ("GAME.PNG", "game-controller", (191, 90, 242), (255, 55, 120)),
        ("BROWSER.PNG", "folder", (90, 200, 250), (0, 110, 240)),
        ("FTP.PNG", "wifi", (80, 225, 110), (30, 165, 80)),
        ("SETTINGS.PNG", "settings", (190, 190, 196), (100, 100, 108)),
        ("CFW.PNG", "hardware-chip", (100, 110, 255), (60, 40, 170)),
        ("PLUGINS.PNG", "extension-puzzle", (255, 205, 40), (255, 140, 0)),
        ("EXIT.PNG", "power", (255, 110, 100), (225, 40, 40)),
    ]:
        ios_icon(svg, top, bottom).save(os.path.join(out, name))
    glass_overlay(64).save(os.path.join(out, "GLOSS.PNG"))
    dock_panel().save(os.path.join(out, "DOCK.PNG"))
    for name, (key, tint) in SMALL_TINTS.items():
        small_icon(G[key], tint).save(os.path.join(out, name))
    checkbox(True).save(os.path.join(out, "CHECK.PNG"))
    checkbox(False).save(os.path.join(out, "UNCHECK.PNG"))
    card(G["image"], "NO ICON").save(os.path.join(out, "NOICON.PNG"))
    card(G["restore"], "Recovery").save(os.path.join(out, "RECOVERY.PNG"))
    spinner().save(os.path.join(out, "WAIT.PNG"))
    bubble().save(os.path.join(out, "SPRITE.PNG"))
    dialog_box().save(os.path.join(out, "BOX.PNG"))
    plate().save(os.path.join(out, "PLATE.PNG"))
    rim().save(os.path.join(out, "RIM.PNG"))
    hint_buttons(out)
    for snd in ("BOOT.MP3", "SOUND.MP3"):
        shutil.copy(os.path.join(sounds, snd), os.path.join(out, snd))
    # Full-size XMB wallpaper, set on the PSP via Settings > Theme Settings > Wallpaper.
    wallpaper().convert("RGB").save(os.path.join(os.path.dirname(out.rstrip("/")), "XMB_WALLPAPER.png"))


if __name__ == "__main__":
    main()
