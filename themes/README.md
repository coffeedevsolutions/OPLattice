# Themes

Four themes. One runs on stock OPL; three need
[`../patches/opl-tile-grid.patch`](../patches/README.md).

| Theme | Needs | Look |
|---|---|---|
| [`thm_UnifiedLibrary`](thm_UnifiedLibrary/) | stock OPL | List with row icons, big cover, per-game wallpaper |
| [`thm_GridHard`](thm_GridHard/) | patches 01+02 | Brutalist. Opaque slabs, 3px accent frame, recently-played strip |
| [`thm_GridGlass`](thm_GridGlass/) | patch 01 | Flat glass. Translucent bars over per-game wallpaper |
| [`thm_GridEditorial`](thm_GridEditorial/) | patch 01 | Swiss poster. Paper ground, rules, red accent |

The grid themes are 5 columns × 2 rows of 2:3 box art with a 14px gap and a 14px
caption, so you can compare them like-for-like. `thm_GridHard` also carries a
four-cover recently-played strip in its header, which is what pushes its tiles
down to 100×148.

## Typeface

All four use **Noto Sans Bold**, subset to Latin (631 KB → 21 KB) because OPL
reads the whole font file into EE RAM *once per slot*. Three slots of the full
font would cost 1.9 MB; the subset costs 63 KB. See
[`../assets/FONT-LICENSE.md`](../assets/FONT-LICENSE.md).

| Slot | Size | Used for |
|---|---|---|
| `default_font` | 17 | Hints, device name |
| `font1` | 26–30 | The `LIBRARY` masthead |
| `font2` | 12 | Tile captions and the game count |

The captions use slot 2 at 12px with `label_height=14` rather than the default
19px, which both fits more of each title before `fntRenderString` clips the line
and hands the 5 reclaimed pixels back to the artwork.

## Install

Copy the folder next to your other OPL themes and pick it in **Settings →
Theme**. The `thm_` prefix must stay — OPL only scans directories containing it.
`README.md` and `ART-ico/` are ignored by OPL; delete them from the copy if you
like.

## Art

Per-game art lives in the **device's `ART` folder**, named `<SERIAL>_<PATTERN>.png`.
The grid themes draw `_COV`; `thm_UnifiedLibrary` also uses `_ICO` and `_BG`.
PNG only — OPL dropped JPG and BMP support on 2024-10-22.

### Size your cover art for the grid

The cache holds the **decoded source image**, so drawing a 300×450 cover into a
100×150 tile wastes 3× the memory for no visible gain. Ten tiles:

| Source art | Cache |
|---|---|
| 300×450 | 4.05 MB |
| 150×225 | 1.01 MB |
| 100×150 | 0.45 MB |

On a 32 MB console that difference is worth having. To downscale a whole folder:

```bash
mkdir -p ART-grid && for f in ART/*_COV.png; do magick "$f" -resize 150x225 -strip "ART-grid/$(basename "$f")"; done
```

## "LIBRARY" is a masthead, not a claim

The grid themes show `LIBRARY` in large type with the device name (`USB Games`,
`HDD Games`, …) small and dim beside it. That split is deliberate.

The grid renders **one device at a time** — that is the OPL limitation the patch
does not remove. `MenuText` is the only element that knows which device you are
on, and its left/right arrows are the only hint that the others exist. Replacing
it outright with a static `LIBRARY` would look tidier and would be a lie: it
would read "Library" while showing you the USB list only, with nothing on screen
saying so.

So `LIBRARY` is the page title and `MenuText` stays as the source indicator. If
you would rather drop the device name entirely, delete the `MenuText` element —
just know that you also lose the only on-screen cue that L2/R2 change the source.

## Design notes

**No fades.** Every panel in these three is a flat fill with a hard border —
`scrim.png` in `thm_UnifiedLibrary` is the one gradient in the repo, and the grid
themes deliberately avoid that approach.

**Glassmorphism is approximate.** Real frosted glass needs a backdrop blur, which
means sampling the framebuffer as a texture. OPL's renderer has no call for that
(`renderman.c` draws sprites, quads, rects and lines, nothing else). `thm_GridGlass`
uses translucent panels with 1px bright borders — tinted acrylic rather than
frosted glass. Over a busy wallpaper it reads convincingly; over a flat one it
just looks like a tinted box.

**Captions clip mid-word.** `fntRenderString` drops the rest of the line once a
glyph would cross the box edge — no ellipsis, no scrolling. "Grand Theft Auto:
Vice City (JP)" becomes "Grand Theft". Set `text=0` for a pure art grid if that
bothers you more than losing the labels.

**Borders and the 1px problem.** A 1px border in a source image drawn at a
different size resamples to a soft line. Where a crisp edge matters the asset is
generated at exactly the size it is drawn (`thm_GridGlass/panel.png` is 570×366,
the grid box). The selection frame doesn't have this problem — it's drawn with
`rmDrawRect`, so it's always exact.

## Tuning

Open any of them in `../opl-theme-previewer.html` and drag. It implements the
patched behaviour and marks every patch-only key with a `PATCHED_ONLY` note.

| Want | Change |
|---|---|
| Bigger tiles, fewer of them | `cell_width` / `cell_height`, keep `cell − gap − 19` at 2:3 |
| More columns | `columns`, and `cell_width` to `width / columns` |
| No captions | `text=0`, then reclaim the 19px in `cell_height` |
| Thicker selection | `frame` |
| Different accent | `sel_text_color` — it is the only colour the frame and selected caption use |
| Fewer cached covers | lower `count` on the invisible `ItemCover`, but never below the visible tile count |
