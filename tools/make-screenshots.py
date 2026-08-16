#!/usr/bin/env python3
"""Placeholder _SCR / _SCR2 art, cut from each game's own background.

    tools/make-screenshots.py [art-dir] [out-dir]     # defaults _deploy/ART both ways

The details page has two screenshot slots (info15 SCR, info22 SCR2) and nothing
has ever filled them, so the two boxes render empty and the lower third of the
layout cannot be judged. This fills them.

These are placeholders and they are honest about it: real screenshots come from
IGS via tools/import-igs.py, which needs captures this library does not have
yet. Rather than invent flat colour swatches -- which would tell you nothing
about how a photographic image sits in that box -- each slot is a 4:3 crop of
the game's own 557x180 background. Two different crops per game, left-of-centre
and right-of-centre, so the pair reads as two distinct frames instead of the
same image twice.

What that does and does not prove: it settles the geometry, the spacing against
the description above, and how a busy image reads at 135x101 on a sharp panel.
It does not tell you anything about real screenshot content, because it is not
one. Swap in IGS captures when there are any and nothing about the layout moves.

Output is 250x188 -- the same target import-igs.py uses, comfortably above the
135x101 draw size so the downscale happens on the GS rather than being baked in.

Written as indexed PNG8, per SHELF-PHASE1.md's screenshot recipe: quantise with
+dither, because dithering buys little on an image this small and costs visible
noise. Only backgrounds have an open dithering question; screenshots do not, so
these are in-spec today rather than waiting on that verdict. At CT24 the pair
would cost 393,216 bytes of a 1,900,544 pool; as T8 they cost about a quarter of
that, which is the difference between comfortable headroom on the details page
and none.
"""
import os
import subprocess
import sys

SRC_W, SRC_H = 557, 180          # the BG art this library ships
OUT_W, OUT_H = 250, 188          # matches tools/import-igs.py

# A 4:3 window is taller than the 557x180 source allows, so the crop is width-
# limited: take a full-height slice and let the resize letterbox nothing away.
# 180 * 4/3 = 240 wide.
CROP_W, CROP_H = 240, 180
OFFSETS = {"SCR": 60, "SCR2": SRC_W - CROP_W - 60}   # left-of-centre, right-of-centre

art = sys.argv[1] if len(sys.argv) > 1 else "_deploy/ART"
out = sys.argv[2] if len(sys.argv) > 2 else "_deploy/ART"
os.makedirs(out, exist_ok=True)

bgs = sorted(f for f in os.listdir(art) if f.endswith("_BG.png"))
if not bgs:
    raise SystemExit(f"no *_BG.png in {art}")

made = 0
for bg in bgs:
    serial = bg[:-len("_BG.png")]
    for slot, x in OFFSETS.items():
        dst = os.path.join(out, f"{serial}_{slot}.png")
        subprocess.run([
            "magick", os.path.join(art, bg),
            "-crop", f"{CROP_W}x{CROP_H}+{x}+0", "+repage",
            "-resize", f"{OUT_W}x{OUT_H}!",
            # Slot 2 gets a slight tonal shift so the two are distinguishable at
            # a glance even on a game whose art is uniform across its width.
            *(["-modulate", "104,112,100"] if slot == "SCR2" else []),
            "+dither", "-colors", "256",
            "-define", "png:color-type=3", "-define", "png:bit-depth=8",
            "-strip", f"PNG8:{dst}",
        ], check=True)
        made += 1

print(f"{made} written to {out}  ({len(bgs)} games x 2 slots)")
