# `_cover-truecolor/` — COVXL output

Working directory. **Contents are gitignored; nothing here is source.**

Default output of `tools/make-cover.py`, which builds `COVXL` — the details
page's cover — from full-resolution masters:

    tools/make-cover.py [downloads-dir] [out-dir]

Reads 600×900 masters and writes `<SERIAL>_COVXL.png` at **216×432**.

## Why 216×432, and why the ratio is not a mistake

The virtual 640×480 space is stretched to fill 16:9, so a rect W wide by H tall
displays at `(W/640*16):(H/480*9)`. A 2:3 cover therefore occupies a virtual
rect of `H = 2W`, and its texels have to carry the same pre-squash. Resizing a
2:3 master straight to 216×432 *is* that squash — which is why the resize is
unconditional and no aspect is preserved.

It is a separate pattern from `COVHD` because the details-page rect is 108
virtual wide (324 physical at 1080i), where 90 texels was a 3.6× stretch while
the grid beside it upscales 1.65×. 216 brings it to 1.50×. Home caches seven
`COVHD` at a 42-wide rect, where 216 would be a megabyte of VRAM for nothing.

## Supplying masters

The tool expects a downloads directory of 600×900 covers and a serial map at
`~/Documents/PS2/artmap.txt` (`key:serial` per line). Both are yours to
provide — this repo ships no cover art.
