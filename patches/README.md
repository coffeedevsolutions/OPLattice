# OPL patches

Two patches for Open PS2 Loader's theme engine, against
`ps2homebrew/Open-PS2-Loader` @ `3e3f34e` (v1.2.0-Beta). **Apply in order** —
the second builds on the first.

| Patch | Adds |
|---|---|
| `01-opl-tile-grid.patch` | Tile grids: `columns`, `cell_width`, `cell_height`, `gap`, `text`, `label_height`, `frame` on `ItemsList`; `offset` on `GameImage`; two-axis navigation |
| `02-opl-sort-and-recent.patch` | Sort modes cycled with **R3**; a persistent recently-played list; `RecentImage` and `RecentText` element types; widescreen correction for the grid |
| `03-opl-menu-tabs.patch` | `MenuTabs` — every visible device drawn at once with a capsule behind the active one; per-device label overrides; `prefix`/`suffix` on `GameCountText`; `x_scaled` for widescreen-aware alignment |

```bash
git clone https://github.com/ps2homebrew/Open-PS2-Loader
cd Open-PS2-Loader
git checkout 3e3f34e
git apply /path/to/01-opl-tile-grid.patch
git apply /path/to/02-opl-sort-and-recent.patch
git apply /path/to/03-opl-menu-tabs.patch
make
```

## Build status

**Compiles and links.** Built with the real toolchain (`ps2dev/ps2dev`,
`mips64r5900el-ps2-elf-gcc 15.2.0`), `make` exit 0, **zero warnings** on both
patched files. A ready binary is in [`build/OPNPS2LD.ELF`](build/OPNPS2LD.ELF).

Verified the patch is actually in the output by diffing against an unpatched
build of the same tree:

| | baseline | patched | Δ |
|---|---|---|---|
| `obj/themes.o` .text | 19184 | 20825 | +1641 |
| `obj/menusys.o` .text | 12689 | 13921 | +1232 |
| `OPNPS2LD.ELF` (grid only) | 1360324 | 1361588 | +1264 |
| `OPNPS2LD.ELF` (both patches) | 1360324 | 1362356 | +2032 |

Reproduce:

```bash
docker run --rm --network host -v "$PWD":/src -w /src ps2dev/ps2dev:latest sh -c '
  apk add --no-cache make python3 py3-yaml git curl bash
  bash ./download_lwNBD.sh; bash ./download_cfla.sh
  make -j4'
```

(`TRANSLATIONS=` skips the language pack download if it fails.)

**Still not run on hardware or in an emulator.** It builds; nobody has booted it.
The runtime behaviour below — navigation feel, scroll edges, art pop-in — is
reasoned from the code, not observed. Test it on something you can recover.

## Why a patch is needed at all

Stock OPL hands every theme element the same two pointers:

```c
elem->drawElem(selected_item, selected_item->item->current, itemConfig, elem);
```
`src/menusys.c:936`

So a `GameImage` can only ever draw the *highlighted* game. Twelve `ItemCover`
elements at twelve positions render twelve copies of one cover. The only element
that walks the list is `ItemsList`, and its layout is compiled in: `posY += 19`,
one column, decorator forced to 20×20.

The art cache was never the obstacle — it is already item-addressed. Every list
entry carries its own slot (`submenu->item.cache_id = malloc(gameCacheCount * sizeof(int))`,
`src/menusys.c:397`), and the existing decorator path already pulls art for 16
different games in one frame. The patch only changes *which entry* gets asked
for; caching, async loading, LRU eviction and the IO queue are untouched.

## What it adds

### `ItemsList` grid mode

| Key | Default | Meaning |
|---|---|---|
| `columns` | `1` | `1` keeps the classic single-column text list, unchanged. |
| `cell_width` | `width / columns` | Horizontal pitch. |
| `cell_height` | `cell_width` | Vertical pitch. |
| `gap` | `4` | Pitch minus drawn art, so art is `cell − gap` wide. |
| `text` | `1` | Reserve one 19px label line at the bottom of each cell. |
| `frame` | `2` when `columns > 1` | Selection frame thickness in virtual px, `0` to disable. |

`displayedItems` becomes `(height / cell_height) × columns`. The selection frame
is four filled `rmDrawRect` calls in `sel_text_color` — hard edges, no rounding,
no glow. Art comes from the element the `decorator` points at, exactly as before.

**Widescreen** (refined in patch 02). In anamorphic 16:9 `rmDrawPixmap` narrows a
`scaled=1` image to 3/4 so it survives the display stretch undistorted. The cell
*pitch* and the centring offset are narrowed by the same factor, so the tiles
keep their shape **and** the grid keeps its proportions instead of spreading out.
Centre a grid with `aligned=1` + `x=POS_MID` and it stays centred in both aspects.

Text needs no correction at all: `fntUpdateAspectRatio` already rasterises glyphs
at 3/4 width in anamorphic mode, so OPL text never stretches. Only the clip box
is converted.

What does *not* self-correct is an individually placed element — a row of
`RecentImage` thumbnails keeps each thumbnail's shape but the gaps between them
widen, because element x coordinates are never aspect-corrected. That is stock
OPL behaviour for every element, not something the patch introduces.

### `GameImage` slot binding

| Key | Default | Meaning |
|---|---|---|
| `offset` | *(absent)* | Render the entry `offset` places after the top of the page rather than the highlighted one. |

Absent key keeps the original behaviour exactly, so existing themes are
unaffected. Past the end of the list the element draws nothing, so a partly
filled last page leaves holes instead of repeating entries. This is the
free-form route — hero layouts, filmstrips, a grid of individually placed
elements — where grid mode is the structured one.

### Navigation

A grid needs two axes, so when `columns > 1`:

| Button | Classic list | Grid theme |
|---|---|---|
| Up / Down | ±1 entry | ±1 **row** (`columns` entries) |
| Left / Right | previous / next device | ∓1 entry along the row |
| **L1 / R1** | page up / down | **previous / next device** |
| L2 / R2 | first / last page | page up / down |

A grid needs both d-pad axes for the tiles, so device switching moves to the
shoulders — L1/R1, which is where most modern UIs put tab switching. Paging
shifts down to L2/R2, and first/last page is dropped, since paging a grid gets
you there quickly enough.

Single-column themes take the `columns > 1` branch nowhere and behave
identically to stock — including the page-up-on-scroll-back behaviour, which is
deliberately preserved rather than replaced with row scrolling.

## Sort modes (patch 02)

`R3` on the main screen cycles the order and saves it as `sort_mode` in
`conf_opl.cfg`. It re-sorts the module's own list head and re-points the menu at
it, so the two never disagree about where the list starts.

| Mode | Order |
|---|---|
| 0 | Title A→Z — stock OPL's behaviour |
| 1 | Title Z→A |
| 2 | Most recently launched first, then A→Z |
| 3 | As scanned from the device, no sorting |

`autosort` still gates whether sorting happens at all; `sort_mode` chooses which.

## Recently played (patch 02)

OPL already stored a single `last_played` id. This keeps an ordered list of the
last **8**, newest first, in the same `conf_last.cfg`, written on launch by all
three game backends. Titles are stored alongside the ids so a header can render
them without searching a device list for a game that may live elsewhere.

Two element types read it:

| Type | Attributes | Draws |
|---|---|---|
| `RecentImage` | `pattern` (default `COV`), `count`, `default`, `index` | Art for the `index`-th most recent launch |
| `RecentText` | `index` | Its stored title |

`index` 0 is the most recent. Elements past the end of the list draw nothing
(`RecentText`) or fall back to `default` (`RecentImage`).

**Cross-device caveat.** The list is global, but art is resolved through the
*current* device's `ART` folder, because that is the only image path OPL exposes.
A game last played from HDD while you are browsing USB shows its title and falls
back to `default` for the picture.

## `MenuTabs` (patch 03)

`MenuText` draws only the device you are on, with a pair of arrows implying the
rest. `MenuTabs` walks the whole device list — the `menu_list_t` chain that
`drawElem` already receives — and draws every entry whose `visible` is set,
framing the current one.

| Key | Default | Meaning |
|---|---|---|
| `pad` | `10` | Horizontal padding inside each tab |
| `sel_color` | theme `sel_text_color` | Capsule fill, or outline colour when no caps are given |
| `sel_text_color` | theme `bg_color` | Active label, i.e. inverted out of the capsule |
| `cap_left` / `cap_right` | *(none)* | Round end caps for the capsule |
| `frame` | `2` | Outline thickness used **only** when no caps are supplied |
| `prev_text` / `next_text` | *(none)* | Button hints drawn at each end, e.g. `L1` / `R1` |
| `hint_font`, `hint_color` | element's | So the hints can be smaller and dimmer than the tabs |
| `label_bdm`, `label_eth`, `label_hdd`, `label_app` | *(none)* | Replace OPL's device names, e.g. `USB` for "USB Games" |
| `width` | `16` | Gap between tabs, not a box width |
| `height` | `24` | Tab height, which is what the capsule encloses |

`aligned=1` with `x=POS_MID` centres the whole strip, measured the same way
`guiAlignMenuHints` measures the hint row.

**The capsule** is a round cap at each end with a plain `rmDrawRect` between
them, so it is exact at any label width rather than a stretched rounded
rectangle. The caps are drawn scaled, which narrows them by 3/4 in anamorphic
16:9 exactly as `fntUpdateAspectRatio` narrows the glyphs — so the capsule stays
wrapped around its label in both aspects. Cap art should be white: the tint is
halved before it reaches the GS, because a textured draw multiplies by the vertex
colour with `0x80` as unity and a full-value tint would come out doubled.

### `x_scaled` — keeping alignment in widescreen

A centred grid moves its edges *inward* in anamorphic 16:9, because the cell
pitch narrows by 3/4 along with the tiles. An element placed at a fixed `x` does
not move with it, so a heading aligned to the grid's left edge in 4:3 drifts
away from it in widescreen.

`x_scaled=1` on any element treats its `x` as an offset from screen centre and
narrows that offset by the same rule, so it tracks a centred grid in both
aspects.

Two details make it exact rather than approximate:

- It is applied **at draw time**, in `menuRenderElements`, and restored
  afterwards. It cannot be baked in at load: toggling widescreen calls
  `rmSetAspectRatio` without reloading the theme (`gui.c:633`), so a value
  computed once would go stale.
- `rmWideScale` is integer `(n * 3) >> 2`, so make the grid's `cell_width` a
  multiple of 4 and both the grid edge and the tracking element land on the same
  whole pixel. `thm_GridHard` uses 88 for exactly this reason.

### `GameCountText` prefix and suffix

| Key | Meaning |
|---|---|
| `prefix` / `suffix` | Set either and the count renders as `prefix` + number + `suffix`, replacing the localised "Files found: %i" |

Deliberately not a printf format from the cfg — that would hand a user-supplied
format string straight to `snprintf`.

## Files touched

| File | Change |
|---|---|
| `include/themes.h` | `offset`/`useOffset` on `mutable_image_t`; grid fields on `items_list_t` |
| `src/themes.c` | read the new keys; `drawCellFrame`; `drawItemsListGrid`; offset walk in `drawGameImage` |
| `src/menusys.c` | `menuRowStep`, step helpers, row-wise `menuNextV`/`menuPrevV`, `menuNextItem`/`menuPrevItem`, input remap, `submenuCompare`, `menuCycleSort` |
| `src/opl.c` | the recent list, `gSortMode` load/save |
| `src/bdmsupport.c`, `src/hddsupport.c`, `src/ethsupport.c` | record a launch into the recent list |
| `include/opl.h`, `include/config.h`, `include/menusys.h` | declarations |

## Designing against it

`opl-theme-previewer.html` implements all of the above, and flags every
patch-only key with a `PATCHED_ONLY` note so you can tell at a glance which parts
of a theme need this build. Four themes in [`../themes/`](../themes/) use it.

## Known gaps

- Built but never booted. See Build status.
- No horizontal scrolling; the grid pages vertically only.
- `frame` draws around the art, not the label. If `text=1` the frame does not
  enclose the caption.
- The frame corrects for widescreen by hand (`rmWideScale`) because `rmDrawRect`
  does not apply the aspect correction `rmDrawPixmap` does. Worth re-checking on
  a real 16:9 console.
- Grid mode and `offset` on the same page are untested together.
- The sort is still OPL's original bubble sort, now with a comparator switch. It
  is O(n²); a 500-game list is ~250k `strcasecmp` calls per re-sort, and R3 makes
  that interactive rather than once at boot.
- `RecentImage` holds one cache slot per element, so four of them means four
  entries in that pattern's cache. Size `count` accordingly.
