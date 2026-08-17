#!/usr/bin/env python3
"""Normalise _LGO art onto one canvas, and build the fade that sits under it.

    tools/make-logos.py [art-dir] [out-dir]

Source logos are all 400 wide but anywhere from 49 to 440 tall, so a theme
element with a fixed width and height would squash a tall logo and stretch a
wide one -- OPL always fills the rect and never preserves aspect. Rescaling each
one to fit a common canvas and padding the rest with transparency makes a single
fixed element size correct for every game.

Canvas is 150x120 texels, anamorphic. The info page draws it into a 150x120
rect, which displays as 200x120 on a 16:9 screen -- but the stretch happens to
the whole framebuffer at scanout and does not give the rect more texels. It
samples 150 regardless. The previous 200-wide canvas therefore had a quarter of
its width discarded by a hardware bilinear downscale, on top of the box average
that produced it: two resamples where one would do.

Now a single average straight from the 400-wide source to 150x120, squeezed
horizontally the way 16:9 content is. Scanout undoes the squeeze, so the shape
on screen is unchanged and the result is sharper. 74 KB of VRAM rather than 96.

Also writes fade.png, a horizontal transparent-to-background ramp that covers
the right edge of the BG so it dissolves into the page instead of ending on a
hard line.

It also writes <SERIAL>_PANEL.png, the field the logo sits on, which flips
between white and the page colour depending on how dark that game's logo is.
Half this library's logo art is drawn for a light background -- MGS2 and MGS3
measure luminance 8 and 9, invisible against a #0A0C0F page. The panel carries
the ramp out of the BG as well, so the fade and the field behind the logo are
one image and always agree with each other. Only black or white by design: a
sampled colour could land anywhere and would sometimes be worse than the page it
replaced.
"""
import os
import struct
import sys
import zlib

CANVAS_W, CANVAS_H = 150, 120     # texels
DISPLAY_W = 200                   # what those texels look like at 16:9
SQUEEZE = CANVAS_W / DISPLAY_W    # 0.75, the anamorphic factor
PLATE_LUMA = 128      # below mid-grey, the logo needs a light plate to read on
FADE_W, FADE_H = 45, 180
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


def mean_luma(canvas, w, h):
    """Mean ink luminance, weighted by coverage so transparent padding does not
    drag a bright figure down.

    Returns None when there is no ink at all. A placeholder logo is entirely
    transparent, and averaging that gives 0.0 -- indistinguishable from a black
    logo, which would plate the page white and leave a bright empty slab where
    the art should be. "No ink" is a third case, not the darkest one."""
    lum = alpha = 0.0
    for y in range(h):
        row = canvas[y]
        for x in range(w):
            r, g, b, a = row[x*4:x*4+4]
            if a < 24:
                continue
            f = a / 255
            lum += (0.2126*r + 0.7152*g + 0.0722*b) * f
            alpha += f
    return (lum / alpha) if alpha else None


def write_panel(path, dark_logo):
    """The field the logo sits on: a ramp out of the BG, then flat colour.

    Written small and stretched by the theme. A horizontal ramp survives being
    stretched -- it is still the same ramp -- so 96x4 covers a 267x180 region
    for 1.5 KB instead of 192 KB. The first sixth is the ramp, which lands at
    45px once stretched, matching the BG's right edge.
    """
    fill = (0xFF, 0xFF, 0xFF) if dark_logo else BG_RGB
    W, H, RAMP = 96, 4, 16
    rows = []
    for _ in range(H):
        row = bytearray(W * 4)
        for x in range(W):
            if x < RAMP:
                t = x / (RAMP - 1)
                a = int(255 * (t ** 2.2))     # same ease-in as the old fade
            else:
                a = 255
            row[x*4:x*4+4] = bytes((*fill, a))
        rows.append(row)
    write_png(path, W, H, rows)


art = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/Documents/PS2/art-out")
out = sys.argv[2] if len(sys.argv) > 2 else "_deploy/ART"
os.makedirs(out, exist_ok=True)

n = plated = 0
for fn in sorted(os.listdir(art)):
    if not fn.endswith("_LGO.png"):
        continue
    sw, sh, rows = read_png(os.path.join(art, fn))
    # Fit inside the canvas, preserving aspect *as displayed*, then squeeze.
    # Fitting in texel space instead would leave every logo looking narrow.
    scale = min(DISPLAY_W / sw, CANVAS_H / sh)
    tw_disp, th = max(1, int(sw * scale)), max(1, int(sh * scale))
    tw = max(1, round(tw_disp * SQUEEZE))
    small = box_scale(sw, sh, rows, tw, th)
    canvas = [bytearray(CANVAS_W * 4) for _ in range(CANVAS_H)]
    ox, oy = (CANVAS_W - tw) // 2, (CANVAS_H - th) // 2
    for y in range(th):
        canvas[oy + y][ox * 4:(ox + tw) * 4] = small[y]
    write_png(os.path.join(out, fn), CANVAS_W, CANVAS_H, canvas)
    luma = mean_luma(canvas, CANVAS_W, CANVAS_H)
    # No ink means there is nothing for a plate to help read, so leave the page
    # colour: the hero shows through and the slot reads as empty on purpose.
    dark = luma is not None and luma < PLATE_LUMA
    write_panel(os.path.join(out, fn.replace("_LGO.png", "_PANEL.png")), dark)
    n += 1
    if dark:
        plated += 1
    shown = "blank" if luma is None else f"{luma:5.1f}"
    print(f"  {fn[:-8]:<14} luma {shown:>5}  panel {'WHITE' if dark else 'black'}")

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
print(f"\n{n} logos on {CANVAS_W}x{CANVAS_H}; {plated} of them dark enough to need a white panel")
print(f"fade.png {FADE_W}x{FADE_H} -> themes/thm_GridHard/")
