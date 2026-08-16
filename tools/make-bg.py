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

Target is 418x180, anamorphic -- and the reasoning that produced the old 557
deserves recording, because it was wrong in an instructive way.

The theme rect is 418x180 *virtual*. On a 16:9 screen it displays at 3.09:1, so
the earlier target of 557x180 was chosen to make the texture itself 3.09:1 "so
the art is carried 1:1". But the 16:9 stretch is applied to the whole
framebuffer at scanout -- it does not give a rect more texels to sample. A 418
wide rect samples 418 texels no matter how wide it looks. Carrying 557 meant the
GS threw a quarter of them away with a bilinear downscale, after sips had already
resampled 1920 down to 557. Two resamples, the second of them poor, and 33 KB of
VRAM spent to make the result softer.

Now: one Lanczos pass straight from the 1920x620 original to 418x180, squeezed
horizontally exactly as 16:9 DVD content is. The screen stretch undoes the
squeeze at scanout, so the picture looks identical in shape and is sharper,
because nothing resamples it twice.

Vertical still runs 180 -> 168 in hardware (Y_SCALE is iDisplayHeight/480). That
one is left alone deliberately: 168 is a property of the current video mode, and
baking it in would silently break the art on any other.

A few originals are not 3.097:1. Those are fitted inside the frame and padded
with the theme background rather than cropped, so the composition survives even
though it does not fill the width. Which ones got padded is reported.
"""
import os
import subprocess
import sys

TARGET_W, TARGET_H = 418, 180        # texels
DISPLAY_W = 557                      # what those texels look like at 16:9
SQUEEZE = TARGET_W / DISPLAY_W       # 0.75, the anamorphic factor
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

# Aspect decisions are made in display space, because that is the shape the
# viewer actually sees; the squeeze is applied once at the end.
target_aspect = DISPLAY_W / TARGET_H
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
        # Straight to texel space in one pass. "!" because the squeeze is
        # deliberate -- preserving aspect here would undo it.
        subprocess.run(["magick", src, "-filter", "Lanczos",
                        "-resize", f"{TARGET_W}x{TARGET_H}!", "-strip", dst], check=True)
        resized.append((serial, f"{sw}x{sh}"))
    else:
        # Fit inside the frame, then pad. Never crop -- that is the bug this
        # script exists to undo. Fitting is computed against the *display*
        # frame, then squeezed, so a padded image keeps its proportions.
        scale = min(DISPLAY_W / sw, TARGET_H / sh)
        fw_disp, fh = max(1, round(sw * scale)), max(1, round(sh * scale))
        fw = max(1, round(fw_disp * SQUEEZE))
        subprocess.run(["magick", src, "-filter", "Lanczos",
                        "-resize", f"{fw}x{fh}!",
                        "-background", f"#{BG_HEX}", "-gravity", "center",
                        "-extent", f"{TARGET_W}x{TARGET_H}", "-strip", dst], check=True)
        padded.append((serial, f"{sw}x{sh}", f"{a:.2f}:1", f"{fw_disp}x{fh} displayed"))

print(f"{len(resized)} resized to {TARGET_W}x{TARGET_H}")
if padded:
    print(f"\n{len(padded)} were not {target_aspect:.2f}:1 and were fitted + padded rather than cropped:")
    for s, d, a, f in padded:
        print(f"  {s:<14} {d:<11} {a:<8} -> {f} on a {TARGET_W}x{TARGET_H} field")
if missing:
    print(f"\nno original found for: {', '.join(missing)}")
