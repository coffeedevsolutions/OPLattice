#!/usr/bin/env python3
"""Rebuild _BG art from the full-size originals, without cropping.

    tools/make-bg.py [downloads-dir] [out-dir]

The art in art-out is 460x215, a 2.140:1 frame. The originals in Downloads are
mostly 1920x620, which is 3.097:1. Those are different shapes, so getting from
one to the other meant discarding about 31% of the width -- and on artwork that
puts its subject at one edge, that is a whole character gone. This regenerates
from the originals at the source aspect so nothing is thrown away.

Mapping comes from ~/Documents/PS2/artmap.txt (`key:SERIAL` per line); the file
in Downloads is `<key>-gameArt-BG.<ext>`.

Target is 557x180. On a 16:9 screen the theme's 418x180 rect displays as
557x180, so the art is carried 1:1 -- 401 KB of VRAM.

A few originals are not 3.097:1. Those are fitted inside the frame and padded
with the theme background rather than cropped, so the composition survives even
though it does not fill the width. Which ones got padded is reported.
"""
import os
import subprocess
import sys

TARGET_W, TARGET_H = 557, 180
BG_HEX = "0A0C0F"
TOLERANCE = 0.03          # within 3% of the frame aspect, just resize


def dims(path):
    out = subprocess.run(["sips", "-g", "pixelWidth", "-g", "pixelHeight", path],
                         capture_output=True, text=True).stdout
    w = h = 0
    for line in out.splitlines():
        if "pixelWidth:" in line: w = int(line.split(":")[1])
        elif "pixelHeight:" in line: h = int(line.split(":")[1])
    return w, h


dl = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/Downloads")
out = sys.argv[2] if len(sys.argv) > 2 else "_deploy/ART"
os.makedirs(out, exist_ok=True)

amap = os.path.expanduser("~/Documents/PS2/artmap.txt")
pairs = []
for line in open(amap):
    line = line.strip()
    if ":" in line:
        k, s = line.split(":", 1)
        pairs.append((k.strip(), s.strip()))

target_aspect = TARGET_W / TARGET_H
resized, padded, missing = [], [], []

for key, serial in sorted(pairs, key=lambda p: p[1]):
    src = None
    for ext in ("png", "jpg", "jpeg", "PNG", "JPG"):
        p = os.path.join(dl, f"{key}-gameArt-BG.{ext}")
        if os.path.exists(p):
            src = p
            break
    if not src:
        missing.append(key)
        continue

    sw, sh = dims(src)
    dst = os.path.join(out, f"{serial}_BG.png")
    a = sw / sh

    if abs(a - target_aspect) / target_aspect <= TOLERANCE:
        subprocess.run(["sips", "-s", "format", "png", "-z", str(TARGET_H), str(TARGET_W),
                        src, "--out", dst], capture_output=True)
        resized.append((serial, f"{sw}x{sh}"))
    else:
        # Fit inside the frame, then pad. Never crop -- that is the bug this
        # script exists to undo.
        scale = min(TARGET_W / sw, TARGET_H / sh)
        fw, fh = max(1, round(sw * scale)), max(1, round(sh * scale))
        subprocess.run(["sips", "-s", "format", "png", "-z", str(fh), str(fw),
                        src, "--out", dst], capture_output=True)
        subprocess.run(["sips", "--padToHeightWidth", str(TARGET_H), str(TARGET_W),
                        "--padColor", BG_HEX, dst], capture_output=True)
        padded.append((serial, f"{sw}x{sh}", f"{a:.2f}:1", f"{fw}x{fh}"))

print(f"{len(resized)} resized to {TARGET_W}x{TARGET_H}")
if padded:
    print(f"\n{len(padded)} were not {target_aspect:.2f}:1 and were fitted + padded rather than cropped:")
    for s, d, a, f in padded:
        print(f"  {s:<14} {d:<11} {a:<8} -> {f} on a {TARGET_W}x{TARGET_H} field")
if missing:
    print(f"\nno original found for: {', '.join(missing)}")
