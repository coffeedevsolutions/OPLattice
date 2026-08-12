#!/usr/bin/env python3
"""Draw a play button per game, tinted from that game's own BG art.

    tools/make-playbtn.py [art-dir]        # default _deploy/ART

Icon and label sit left-aligned inside the capsule, clear of the cap curve.

OPL cannot derive a colour at runtime -- element colours are static hex in the
theme and nothing samples a texture. So a button that picks up each game's
palette has to be per-game art, generated here and read through its own pattern:
<SERIAL>_BTN.png, drawn by a GameImage the same way COV and BG are. 120x26 RGBA
is 12.5 KB of VRAM, which is affordable next to a 300x450 cover.

Picking the colour: bucket the BG's hues weighted by saturation and value,
ignoring anything washed out or nearly black, and take the strongest bucket.
That finds the artwork's accent rather than its background -- Vice City's pink,
not the dark sky behind it. The result is then forced bright and saturated
enough that the inverted triangle stays readable, because a faithful sample is
worth nothing if the button turns into a dark smear.

Also writes themes/thm_GridHard/playbtn.png as the fallback for any game with no
BG art, using the theme's accent.
"""
import colorsys
import math
import os
import struct
import sys
import zlib

W, H = 120, 26
INK = (0x0A, 0x0C, 0x0F)      # triangle, in the page background colour
SS = 4                        # supersample the caps and the diagonal
ACCENT = (0xFF, 0x4D, 0x2E)   # theme accent, used when there is no BG to sample

MIN_V, MIN_S = 0.92, 0.72     # what the sampled hue is rendered at


def read_png(path):
    d = open(path, "rb").read()
    i, idat, pal, trns = 8, b"", None, None
    while i < len(d):
        ln, typ = struct.unpack(">I4s", d[i:i + 8])
        body = d[i + 8:i + 8 + ln]
        if typ == b"IHDR":
            w, h, depth, ctype, _, _, il = struct.unpack(">IIBBBBB", body)
            if depth != 8 or il:
                raise SystemExit(f"{path}: only 8-bit non-interlaced PNG supported")
        elif typ == b"PLTE": pal = body
        elif typ == b"tRNS": trns = body
        elif typ == b"IDAT": idat += body
        i += 12 + ln
    nch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw, stride = zlib.decompress(idat), w * nch
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
        out = bytearray(w * 4)
        for x in range(w):
            s = ln_[x * nch:x * nch + nch]
            if ctype == 6:   r, g, bl, al = s
            elif ctype == 2: r, g, bl, al = s[0], s[1], s[2], 255
            elif ctype == 0: r = g = bl = s[0]; al = 255
            elif ctype == 4: r = g = bl = s[0]; al = s[1]
            else:
                idx = s[0]; r, g, bl = pal[idx*3:idx*3+3]
                al = trns[idx] if trns and idx < len(trns) else 255
            out[x*4:x*4+4] = bytes((r, g, bl, al))
        rows.append(out); prev = ln_
    return w, h, rows


def accent_of(path, step=1):
    """Dominant hue of an image, vivified to something a button can be.

    Two things this gets right that the obvious version does not.

    The saturation floor is 0.15, not 0.35. Gundam's logo green is #A0E0A0 at
    saturation 0.29 -- a pale brand colour, and the dominant one in the mark --
    which a 0.35 floor excluded entirely, leaving a 300-pixel red detail to win.

    And the winning bucket contributes its circular-mean *hue*, not a mean of
    its RGB. Averaging colour across a bucket pulls toward grey and produced
    muddy browns; taking the hue and then imposing a fixed saturation and value
    keeps the colour recognisable as the game's.
    """
    w, h, rows = read_png(path)
    B = 24
    weight = [0.0] * B
    cx = [0.0] * B
    cy = [0.0] * B
    for y in range(0, h, step):
        row = rows[y]
        for x in range(0, w, step):
            r, g, b, a = row[x*4:x*4+4]
            if a < 128:
                continue
            hh, ss, vv = colorsys.rgb_to_hsv(r/255, g/255, b/255)
            if ss < 0.15 or vv < 0.22:      # grey, black or white carries no hue
                continue
            k = min(B - 1, int(hh * B))
            wgt = ss * vv
            weight[k] += wgt
            cx[k] += math.cos(hh * 2 * math.pi) * wgt
            cy[k] += math.sin(hh * 2 * math.pi) * wgt
    if sum(weight) <= 0:
        return None
    k = max(range(B), key=lambda i: weight[i])
    hue = (math.atan2(cy[k], cx[k]) / (2 * math.pi)) % 1.0
    r, g, b = colorsys.hsv_to_rgb(hue, MIN_S, MIN_V)
    return (round(r*255), round(g*255), round(b*255))


def draw(path, fill):
    rows = []
    r_cap = H / 2
    for py in range(H):
        row = bytearray(W * 4)
        for px in range(W):
            inp = intri = 0
            for sy in range(SS):
                for sx in range(SS):
                    x, y = px + (sx + .5) / SS, py + (sy + .5) / SS
                    cx = min(max(x, r_cap), W - r_cap)
                    if (x - cx) ** 2 + (y - H/2) ** 2 <= r_cap * r_cap:
                        inp += 1
                    tx, th = 16.0, 11.0
                    if tx <= x <= tx + th * .9 and abs(y - H/2) <= (th/2) * (1 - (x - tx) / (th * .9)):
                        intri += 1
            n = SS * SS
            p, tri = inp / n, intri / n
            if p <= 0:
                continue
            row[px*4:px*4+4] = bytes((
                int(fill[0]*(1-tri) + INK[0]*tri),
                int(fill[1]*(1-tri) + INK[1]*tri),
                int(fill[2]*(1-tri) + INK[2]*tri),
                int(255 * p)))
        rows.append(row)
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t+d) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


art = sys.argv[1] if len(sys.argv) > 1 else "_deploy/ART"
made = 0
for fn in sorted(os.listdir(art)):
    if not fn.endswith("_BG.png"):
        continue
    serial = fn[:-7]
    src = "LGO"
    fill = None
    lgo = os.path.join(art, f"{serial}_LGO.png")
    if os.path.exists(lgo):
        fill = accent_of(lgo)
    if fill is None:
        fill, src = accent_of(os.path.join(art, fn)), "BG"
    if fill is None:
        fill, src = ACCENT, "theme accent"
    draw(os.path.join(art, f"{serial}_BTN.png"), fill)
    made += 1
    print(f"  {serial:<14} #{fill[0]:02X}{fill[1]:02X}{fill[2]:02X}  from {src}")

draw("themes/thm_GridHard/playbtn.png", ACCENT)
print(f"\n{made} per-game buttons, plus the theme fallback at #{ACCENT[0]:02X}{ACCENT[1]:02X}{ACCENT[2]:02X}")
