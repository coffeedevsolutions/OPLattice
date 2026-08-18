#!/usr/bin/env python3
"""Build COVXL, the details page's cover, from the full-resolution masters.

    tools/make-cover.py [downloads-dir] [out-dir]

The 600x900 originals in the downloads folder are the only full-resolution cover
art there is; everything on the device derives from them. COV (100x150) and
COVHD (90x180) were both made for small rects and the details page is not one --
it draws 108 virtual wide, which is 324 physical at 1080i.

216x432 texels, and the ratio is not a mistake. The virtual 640x480 space is
stretched to fill 16:9, so a rect W wide by H tall displays at
(W/640*16):(H/480*9); a 2:3 cover therefore occupies a virtual rect of H = 2W,
and its texels have to carry the same pre-squash. Resizing a 2:3 master straight
to 216x432 IS that squash -- which is why the resize is unconditional and no
aspect is preserved.
"""
import os
import subprocess
import sys

W, H = 216, 432
dl = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/Downloads")
out = sys.argv[2] if len(sys.argv) > 2 else "_cover-truecolor"
os.makedirs(out, exist_ok=True)

pairs = []
for line in open(os.path.expanduser("~/Documents/PS2/artmap.txt")):
    if ":" in line:
        k, s = line.strip().split(":", 1)
        pairs.append((k.strip(), s.strip()))

done, missing = [], []
for key, serial in sorted(pairs, key=lambda p: p[1]):
    src = None
    for ext in ("png", "jpg", "jpeg", "webp", "avif", "PNG", "JPG"):
        cand = os.path.join(dl, "%s-gameArt.%s" % (key, ext))
        if os.path.exists(cand):
            src = cand
            break
    if not src:
        missing.append(key)
        continue
    dst = os.path.join(out, "%s_COVXL.png" % serial)
    subprocess.run(["magick", src, "-filter", "Lanczos",
                    "-resize", "%dx%d!" % (W, H), "-strip", dst], check=True)
    done.append(serial)

print("%d built at %dx%d" % (len(done), W, H))
if missing:
    print("\nno master found for: %s" % ", ".join(sorted(missing)))
