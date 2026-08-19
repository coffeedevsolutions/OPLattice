# `_art-truecolor/` — the truecolor bank

Working directory. **Contents are gitignored; nothing here is source.**

`tools/palettize-art.py` copies every truecolor original into this folder on
first run and derives all conversions from *here*, never from the previous
output. That is what makes a re-run free: quantising an already-quantised image
compounds error, so the bank is the thing that keeps `--dither` re-decidable at
any time.

A file already banked is never re-banked, so a second run cannot overwrite an
original with a palettised one.

## Naming

    <SERIAL>_<PATTERN>.png        e.g. SLUS_213.55_COVXL.png

`SERIAL` is the disc serial as it appears on the card (`SLUS_213.55`,
`SCPS_150.16`, `SLPM_662.32`).

## Patterns

Sizes are the texel counts the element actually *draws*, which is not always its
declared width — `GameImage` defaults to `SCALING_RATIO` and `rmSetupQuad`
computes three quarters of the declared width in 16:9. The table in
`tools/palettize-art.py` is the single source of truth.

| Pattern | Texels | Mode | Notes |
|---|---|---|---|
| `BG` | 418×180 | quant | `scaled=0`, so declared width is drawn width |
| `HERO` | 612×244 | quant | SHELF Library hero; `640 - SHELF_RAIL_W` wide |
| `COV` | — | quant | classic grid; sized by `stage-device.sh` |
| `COVHD` | 90×180 | quant | declared 120, RATIO → 90 |
| `COVXL` | 216×432 | quant | details-page cover |
| `SCR`, `SCR2` | 101×101 | quant | declared 135, RATIO → 101 |
| `LGO` | 150×120 | keep | 236 alpha levels; left truecolor |
| `PANEL` | 96×4 | exact | lossless, has alpha |
| `BTN` | 90×26 | exact | declared 120, RATIO → 90 |

`quant` = quantise to 256 with the dither policy; `exact` = exact RGBA palette,
lossless; `keep` = leave truecolor.

## Populating it

Point the tool at a directory of truecolor art:

    tools/palettize-art.py [--dither riemersma|o8x8|none] [art-dir]

It defaults to `_deploy/ART`. Supply your own art — this repo ships none.
