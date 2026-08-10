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
caption, so you can compare them like-for-like. `thm_GridHard` also carries a **CONTINUE band** above the grid — the single most
recent launch as a wide `_BG` banner (170×80, matching that art's 460×215 shape)
with the title beside it — and a **details page**. Its band and grid share a left
rule at x=122 rather than the page margin.

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

## Widescreen

The grid themes are built so nothing distorts when you switch to 16:9.

| Part | `scaled` | 16:9 behaviour |
|---|---|---|
| Header / footer bars | `0` + `DIM_INF` | Stretch edge to edge; their contents spread out with the screen |
| Grid tiles | `1` (default) | Tiles keep 2:3 **and** the cell pitch narrows to match, so the grid stays a block rather than spreading |
| All text | n/a | Never stretches — OPL rasterises glyphs at 3/4 width in anamorphic mode |
| Recent thumbnails | `1` | Keep their shape, but the gaps between them widen |

The grid is centred with `aligned=1` + `x=POS_MID` so that when it narrows it
stays centred. That last row is the one compromise: individually placed elements
have their x coordinates stretched by the display, and no theme key changes
that — it is how every OPL element has always behaved.

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

### The details page (SQUARE)

`thm_GridHard` defines an `info0`–`info17` chain, which is stock OPL — no patch.
`itemExecSquare` switches to `GUI_SCREEN_INFO` when the theme has info elements,
and OPL only offers the **Info** hint on the main page under that same condition.
A theme with no `info<N>` block simply has no details screen, which is why the
other three don't show one.

It gathers everything OPL can say about a title:

| Element | Shows |
|---|---|
| `ItemCover`, `GameImage` with `pattern=LGO` / `SCR` / `SCR2` | the rest of the art collateral |
| `AttributeText` `Title` `Genre` `Release` `Developer` `#Size` `Description` | metadata from the game's own config |
| `AttributeImage` `#Media` `#Format` `Rating` | badges picked by the value of that key |
| `ItemText` | the startup id |

Metadata reflects what your setup actually records. `Size`, `Media` and `Format`
are filled in by OPL; `Genre`, `Release`, `Developer` and `Description` come from
whatever art/config pack you use and stay blank otherwise — `display=0` keeps the
label visible either way.

## Button hints without OPL's button graphics

Any icon in the `BDM_ICON`..`START_ICON` range can be replaced by dropping a PNG
named after it into the theme folder — `circle.png`, `cross.png`, `triangle.png`,
`square.png`, `start.png`, `select.png`, `left.png`, `right.png`. No patch.

All four themes ship plain outlines and bars instead of rendered PS2 buttons:
a ring for ○, two strokes for ✕, outlines for △ and □, a filled bar for START and
a hollow one for SELECT. Delete one and OPL falls back to its own, because
`use_default=1`.

`guiDrawIconAndText` always draws the icon 20px tall and derives the width from
the source aspect, so the way to make a hint smaller is to draw a smaller shape
inside a 20px canvas — which is what these do.

## Cache order matters

`findDuplicate` gives every element that names a pattern the **first** cache
created for it, and the first element in the file wins. In `thm_GridHard` the
invisible `ItemCover` is declared before the recent band for exactly this
reason: it sizes the shared `COV` cache for the 10 grid tiles *plus* the 3
thumbnails. Move it after them and the cache is created at the thumbnails' size,
and OPL silently drops the grid's row art. The previewer reports that as
`DECORATOR_DROPPED`.

| Want | Change |
|---|---|
| Bigger tiles, fewer of them | `cell_width` / `cell_height`, keep `cell − gap − 19` at 2:3 |
| More columns | `columns`, and `cell_width` to `width / columns` |
| No captions | `text=0`, then reclaim the 19px in `cell_height` |
| Thicker selection | `frame` |
| Different accent | `sel_text_color` — it is the only colour the frame and selected caption use |
| Fewer cached covers | lower `count` on the invisible `ItemCover`, but never below the visible tile count |
