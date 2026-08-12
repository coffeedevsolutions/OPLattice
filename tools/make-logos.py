#!/usr/bin/env python3
"""Normalise _LGO art onto one canvas, and build the fade that sits under it.

    tools/make-logos.py [art-dir] [out-dir]

Source logos are all 400 wide but anywhere from 49 to 440 tall, so a theme
element with a fixed width and height would squash a tall logo and stretch a
wide one -- OPL always fills the rect and never preserves aspect. Rescaling each
one to fit a common canvas and padding the rest with transparency makes a single
fixed element size correct for every game.

Canvas is 200x120. The info page draws it into a 150x120 rect, which on a 16:9
screen displays as 200x120 -- so the art is carried at 1:1 with no upscaling,
for 96 KB of VRAM.

Also writes fade.png, a horizontal transparent-to-background ramp that covers
the right edge of the BG so it dissolves into the page instead of ending on a
hard line.
"""
import os
import struct
import sys
import zlib

CANVAS_W, CANVAS_H = 200, 120
FADE_W, FADE_H = 45, 215
BG_RGB = (0x0A, 0x0C, 0x0F)


def read_png(path):
    """Return (w, h, rows) with rows as RGBA bytearrays."""
    d = open(path, "rb").read()
    i, idat, pal, trns = 8, b"", None, None
    while i < len(d):
        ln, typ = struct.unpack(">I4s", d[i:i + 8])
        body = d[i + 8:i + 8 + ln]
        if typ == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or interlace:
                raise SystemExit(f"{path}: only 8-bit non-interlaced PNG supported")
        elif typ == b"PLTE":
            pal = body
        elif typ == b"tRNS":
            trns = body
        elif typ == b"IDAT":
            idat += body
        i += 12 + ln
    nch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = w * nch
    rows, prev, p = [], bytearray(stride), 0
    for _ in range(h):
        f = raw[p]; p += 1
        ln_ = bytearray(raw[p:p + stride]); p += stride
        for x in range(stride):
            a = ln_[x - nch] if x >= nch else 0
            b = prev[x]
            c = prev[x - nch] if x >= nch else 0
            if f == 1: ln_[x] = (ln_[x] + a) & 255
            elif f == 2: ln_[x] = (ln_[x] + b) & 255
            elif f == 3: ln_[x] = (ln_[x] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                ln_[x] = (ln_[x] + pr) & 255
        # widen to RGBA
        out = bytearray(w * 4)
        for x in range(w):
            s = ln_[x * nch:x * nch + nch]
            if ctype == 6:   r, g, bl, al = s
            elif ctype == 2: r, g, bl, al = s[0], s[1], s[2], 255
            elif ctype == 0: r = g = bl = s[0]; al = 255
            elif ctype == 4: r = g = bl = s[0]; al = s[1]
            else:
                idx = s[0]
                r, g, bl = pal[idx * 3:idx * 3 + 3]
                al = trns[idx] if trns and idx < len(trns) else 255
            out[x * 4:x * 4 + 4] = bytes((r, g, bl, al))
        rows.append(out); prev = ln_
    return w, h, rows


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    open(path, "wb").write(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b""))


def box_scale(sw, sh, rows, dw, dh):
    """Average-downsample to dw x dh. Premultiplies so transparent padding in
    the source cannot bleed its colour into the edges of the result."""
    out = []
    for dy in range(dh):
        y0, y1 = dy * sh // dh, max(dy * sh // dh + 1, (dy + 1) * sh // dh)
        row = bytearray(dw * 4)
        for dx in range(dw):
            x0, x1 = dx * sw // dw, max(dx * sw // dw + 1, (dx + 1) * sw // dw)
            ar = ag = ab = aa = n = 0
            for y in range(y0, y1):
                src = rows[y]
                for x in range(x0, x1):
                    r, g, b, a = src[x * 4:x * 4 + 4]
                    ar += r * a; ag += g * a; ab += b * a; aa += a; n += 1
            if aa:
                row[dx * 4:dx * 4 + 4] = bytes((ar // aa, ag // aa, ab // aa, aa // n))
        out.append(row)
    return out


art = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/Documents/PS2/art-out")
out = sys.argv[2] if len(sys.argv) > 2 else "_deploy/ART"
os.makedirs(out, exist_ok=True)

n = 0
for fn in sorted(os.listdir(art)):
    if not fn.endswith("_LGO.png"):
        continue
    sw, sh, rows = read_png(os.path.join(art, fn))
    # Fit inside the canvas, preserving aspect.
    scale = min(CANVAS_W / sw, CANVAS_H / sh)
    tw, th = max(1, int(sw * scale)), max(1, int(sh * scale))
    small = box_scale(sw, sh, rows, tw, th)
    canvas = [bytearray(CANVAS_W * 4) for _ in range(CANVAS_H)]
    ox, oy = (CANVAS_W - tw) // 2, (CANVAS_H - th) // 2
    for y in range(th):
        canvas[oy + y][ox * 4:(ox + tw) * 4] = small[y]
    write_png(os.path.join(out, fn), CANVAS_W, CANVAS_H, canvas)
    n += 1
    print(f"  {fn:<26} {sw}x{sh} -> {tw}x{th} on {CANVAS_W}x{CANVAS_H}")

# The fade. Transparent at the left so the art shows through, fully background
# at the right so the edge of the BG dissolves rather than stopping.
fade = []
for _ in range(FADE_H):
    row = bytearray(FADE_W * 4)
    for x in range(FADE_W):
        t = x / (FADE_W - 1)
        # Ease-in, not smoothstep. Smoothstep is already half-opaque at the
        # midpoint, which ate the right third of the art; this stays near
        # transparent across most of the ramp and only closes up at the edge.
        a = int(255 * (t ** 2.2))
        row[x * 4:x * 4 + 4] = bytes((*BG_RGB, a))
    fade.append(row)
write_png("themes/thm_GridHard/fade.png", FADE_W, FADE_H, fade)
print(f"\n{n} logos on a {CANVAS_W}x{CANVAS_H} canvas -> {out}")
print(f"fade.png {FADE_W}x{FADE_H} -> themes/thm_GridHard/")
