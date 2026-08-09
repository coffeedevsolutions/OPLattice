# Unified Library

A theme that makes USB/SD, HDD, Ethernet and Apps look like one library, with
row art, a large cover for the selected game, and a per-game backdrop.

## Read this first — what OPL will not do

Two things people expect from a "unified library" theme are impossible in a
`conf_theme.cfg`, because they're limits of OPL's renderer, not of the config
format. No theme can work around them.

**1. Devices cannot be merged into one list.** OPL builds a separate menu entry
per device (`moduleUpdateMenuInternal`, `src/opl.c:216`) — `BDM_MODE`…`BDM_MODE4`,
`ETH_MODE`, `HDD_MODE`, `APP_MODE` — and `menuRenderElements` draws the theme for
whichever one is selected, one at a time. There is no config key that combines
them. What this theme does instead is make every device page *identical*, and put
the device name and its left/right arrows front and centre, so switching source
with L1/R1 feels like changing a filter rather than changing screens.

**2. There is no tile or grid view.** `drawItemsList` (`src/themes.c:840`) is a
single column: one text string per row, `posY += 19` each time, plus an optional
decorator icon whose size is hard-coded to 20×20 (`DECORATOR_SIZE`). You cannot
add columns, and you cannot fake a grid with several `ItemCover` elements —
every `GameImage` renders only the *currently selected* game. The closest OPL
gets to tiles is what this theme does: a 20×20 icon on each row plus one large
cover for the selection.

## Install

1. Copy the whole `thm_UnifiedLibrary` folder to your OPL themes directory
   (next to `conf_opl.cfg`, usually `.../OPL/THM/` or the root of the device).
   The folder name must keep its `thm_` prefix — OPL only scans directories
   whose name contains it.
2. Select it in OPL under **Settings → Theme**.
3. Delete `ART-ico/` and `README.md` from the copy if you like; they aren't read
   by OPL.

## Art

Per-game art lives in the **device's `ART` folder**, not in the theme. Filenames
are `<SERIAL>_<PATTERN>.png` — the serial exactly as OPL shows it.

| Pattern | Used by | Suggested size |
|---|---|---|
| `_COV` | the large cover | 300×450 (2:3) |
| `_ICO` | the row icons | **64×64** |
| `_BG` | the per-game backdrop | 460×215 or larger |

**PNG only.** OPL dropped `.jpg` and `.bmp` support on 2024-10-22; a `.jpg` will
simply not appear.

Anything missing falls back gracefully: no `_COV` shows `cover.png`, no `_BG`
shows `backdrop.png`, and a game with no `_ICO` just gets a blank row icon.

`ART-ico/` in this folder contains 64×64 icons generated from a set of covers,
as a starting point — copy them into your `ART` folder alongside the rest.

## The one number to be careful with

`main7_count` (16) must stay **at or above** the row count of `main9`, which is
`main9_height / 19` = `304 / 19` = 16.

If you make the list taller without raising that count, OPL silently drops the
row icons entirely — no error, they just stop appearing. This is the
`cache->count >= displayedItems` guard in `validateItemsList`. If you shorten the
list, lower the count to match and save the memory.

## Memory

~3.1 MB of art cache. The breakdown, at `count × source width × height × 3`:

| Cache | Sum | |
|---|---|---|
| `COV` | 6 × 300 × 450 × 3 | 2.32 MB |
| `BG` | 2 × 460 × 215 × 3 | 0.57 MB |
| `ICO` | 16 × 64 × 64 × 3 | 0.19 MB |

That's comfortable on a 32 MB console. The reason it's small is `main7`: an
invisible `ItemIcon` at 0×0 that exists purely to create a *small* `ICO` cache
for the decorator to reuse. Pointing the decorator straight at `COV` instead
would work, but it would force the cover cache up to 16 entries and take the
total past 6.9 MB — the sizes that get themes labelled "works in PCSX2,
black-screens on hardware".

**The drawn size does not matter here.** The cache holds the decoded *source*
image, so shrinking `main6_width` saves nothing. To cut memory, shrink the art
files themselves or lower a `count`.

## Tuning

Open the folder in the previewer (`opl-theme-previewer.html` in the repo root)
and drag things around — it re-checks all of the above live.

| Want | Change |
|---|---|
| More rows | Raise `main9_height` in steps of 19, and raise `main7_count` to match. |
| Bigger cover | `main6_width` / `main6_height`, keeping 2:3. |
| Plain background | Delete `main0_pattern`, leaving `default=backdrop`. |
| Less dimming | Replace `scrim.png`, or delete `main1` entirely. |
| Different accent | `sel_text_color` — it colours the selected row, and nothing else can override it per element. |
