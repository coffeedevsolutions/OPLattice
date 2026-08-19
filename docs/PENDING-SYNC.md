# Sync log

Both volumes current as of 2026-08-18. Nothing queued.

## Deployed this pass
- `APPS/OPNPS2LD.ELF` -- 1,389,492 bytes, md5 4dff337b62dc4dfa9b2a322a8259c7cf,
  on the HDD and both card locations.
- 46 CFGs merged: the sixteen-value Genre vocabulary and `Source=Disc`. Merged,
  so OPL's own LastPlayed / PlayCount / Rating survived.
- 46 `*_COVXL.png` at 216x432 (HDD only -- art is device-local).
- Theme slimmed on both volumes: 19 classic-chain images DELETED rather than
  merely not-copied, plus star-on / star-off. 319,118 bytes, 11 files, byte
  identical to the repo on both.

## What to watch on this boot
1. Home, left alone for several minutes. Every render path used to open six
   files a frame and leak them; nothing in any render path opens a file now.
2. The hero gradient. It is one gouraud quad. Any residual stepping is the
   16-bit framebuffer at vmode 11 (1920x1080 > 704x576 forces GS_PSM_CT16S,
   5 bits per channel), dithered -- not a seam. A hard seam would mean the quad
   is wrong; soft gradation is the depth ceiling.
3. Library: Square cycles ordering, L1/R1 the filter, L2/R2 jumps a section.
   Moving between rows should no longer reload art that was just on screen.
4. Details: triangle favourites, and the star should be filled after.

## Console settings
BDM Start Mode -> Auto; BDM HDD -> On; Enable PS2RD Cheat Engine -> On;
Settings -> Cheat Settings (global) -> Enable Cheats, Auto-select, Save Changes.

## Copying to either volume
`export COPYFILE_DISABLE=1` first, or macOS writes `._` sidecars onto exFAT.
