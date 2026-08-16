#!/usr/bin/env python3
"""Palettize RGBA art without destroying soft alpha edges.

    tools/palettize-rgba.py <file.png> [more.png ...]

ImageMagick cannot do this. `PNG8:` emits a single-entry tRNS, which is binary
transparency and throws away every antialiased edge; `-type PaletteAlpha` emits
no tRNS at all and the alpha reads back as zero. pngquant would, and is not
installed. So the palette is built here.

A palette PNG carries alpha in tRNS, which is a per-entry alpha *array* -- so a
palette entry is really an (R,G,B,A) tuple and soft edges survive as long as the
quantiser treats alpha as a fourth dimension rather than an afterthought.

Two paths:

  <=256 distinct RGBA  ->  exact palette. Lossless, bit for bit. This covers
                           PANEL and BTN, which are flat fields and a ramp.
  otherwise            ->  median cut over RGBA, with alpha weighted heavily so
                           that levels of transparency get their own entries in
                           preference to shades of colour. An edge that loses
                           its alpha ramp is visible; one that loses a little
                           colour precision is not.

Every output is verified by decoding it again and comparing to the source:
exact conversions must be identical, quantised ones report their error. An
unverified art conversion is how a silent quality regression ships.
"""
import os
import struct
import subprocess
import sys
import zlib

MAX_COLORS = 256
ALPHA_WEIGHT = 3.0      # alpha counts triple when choosing the axis to split


def read_rgba(path):
    raw = subprocess.run(["magick", path, "-depth", "8", "RGBA:-"],
                         capture_output=True, check=True).stdout
    d = open(path, "rb").read(26)
    w, h = struct.unpack(">II", d[16:24])
    if len(raw) != w * h * 4:
        raise SystemExit(f"{path}: decoded {len(raw)} bytes, expected {w*h*4}")
    return w, h, raw


def median_cut(pixels, want):
    """pixels: list of (r,g,b,a). Returns up to `want` representative tuples."""
    boxes = [pixels]
    while len(boxes) < want:
        # Split the box with the largest weighted extent; stop when none can.
        target, axis, extent = None, 0, 0
        for b in boxes:
            if len(b) < 2:
                continue
            for ax in range(4):
                vals = [p[ax] for p in b]
                e = (max(vals) - min(vals)) * (ALPHA_WEIGHT if ax == 3 else 1.0)
                if e > extent:
                    target, axis, extent = b, ax, e
        if target is None:
            break
        target.sort(key=lambda p: p[axis])
        mid = len(target) // 2
        boxes.remove(target)
        boxes.append(target[:mid])
        boxes.append(target[mid:])
    out = []
    for b in boxes:
        if not b:
            continue
        n = len(b)
        out.append(tuple(sum(p[i] for p in b) // n for i in range(4)))
    return out


def write_indexed_png(path, w, h, indices, palette):
    plte = b"".join(bytes(c[:3]) for c in palette)
    trns = bytes(c[3] for c in palette)
    # tRNS may be truncated at the last fully-opaque run; keep it simple and
    # emit it in full unless every entry is opaque.
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += bytes(indices[y * w:(y + 1) * w])

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    out = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0))
           + chunk(b"PLTE", plte))
    if any(a != 255 for a in trns):
        out += chunk(b"tRNS", trns)
    out += chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(out)


def convert(path):
    w, h, raw = read_rgba(path)
    pixels = [tuple(raw[i:i + 4]) for i in range(0, len(raw), 4)]
    uniq = sorted(set(pixels))

    if len(uniq) <= MAX_COLORS:
        palette = uniq
        lut = {c: i for i, c in enumerate(palette)}
        indices = [lut[p] for p in pixels]
        mode = "exact"
    else:
        palette = median_cut([list(p) for p in pixels], MAX_COLORS)
        palette = [tuple(c) for c in palette]
        # Nearest entry, alpha weighted the same way the split was chosen.
        cache = {}
        indices = []
        for p in pixels:
            j = cache.get(p)
            if j is None:
                best, bd = 0, None
                for i, c in enumerate(palette):
                    d = ((p[0]-c[0])**2 + (p[1]-c[1])**2 + (p[2]-c[2])**2
                         + ALPHA_WEIGHT * (p[3]-c[3])**2)
                    if bd is None or d < bd:
                        best, bd = i, d
                cache[p] = j = best
            indices.append(j)
        mode = "median-cut"

    write_indexed_png(path, w, h, indices, palette)

    # Verify by decoding the result back.
    w2, h2, raw2 = read_rgba(path)
    if (w2, h2) != (w, h):
        raise SystemExit(f"{path}: size changed on write")
    ct = open(path, "rb").read(26)[25]
    if ct != 3:
        raise SystemExit(f"{path}: colour type {ct}, not 3")
    diffs = sum(1 for a, b in zip(raw, raw2) if a != b)
    err = sum(abs(a - b) for a, b in zip(raw, raw2)) / max(len(raw), 1)
    alpha_err = max((abs(raw[i+3] - raw2[i+3]) for i in range(0, len(raw), 4)), default=0)
    if mode == "exact" and diffs:
        raise SystemExit(f"{path}: exact palette was not lossless ({diffs} bytes differ)")
    return mode, len(uniq), len(palette), err, alpha_err


for path in sys.argv[1:]:
    mode, uniq, n, err, ae = convert(path)
    tag = "lossless" if mode == "exact" else f"mean err {err:.2f}, worst alpha {ae}"
    print(f"  {os.path.basename(path):<28} {uniq:>6} uniq -> {n:>3} entries  {mode:<11} {tag}")
