# Sync log

Both volumes current as of 2026-08-19. Nothing queued.

## Deployed this pass
- `APPS/OPNPS2LD.ELF` -- 1,389,876 bytes, md5 07f7fb7b2c0e56202054ca3bb02aa88a,
  on the HDD and both card locations. ELF only: the theme, the 46 CFGs and the
  372 art files were already current and were verified rather than re-copied.
  Previous binaries kept as `_hdd-backup/OPNPS2LD-*.ELF` and
  `_card-backup/OPNPS2LD-*.ELF` for an A/B.
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
4. Details: TRIANGLE favorites. The last build bound this to square while the
   footer drew a triangle, so pressing what the page said did nothing. The star
   should fill, and survive a reboot -- that last part is the CFG write proving
   out.
5. Library, Category ordering: names spelled out in a wheel with the current one
   centred, L2 above and R2 below it, and each category starting its own row.

## Console settings
BDM Start Mode -> Auto; BDM HDD -> On; Enable PS2RD Cheat Engine -> On;
Settings -> Cheat Settings (global) -> Enable Cheats, Auto-select, Save Changes.

## Copying to either volume
`export COPYFILE_DISABLE=1` first, or macOS writes `._` sidecars onto exFAT.
