# `opl-tile-grid.patch`

Adds tile-grid support to Open PS2 Loader's theme engine. Against
`ps2homebrew/Open-PS2-Loader` @ `3e3f34e` (v1.2.0-Beta).

```bash
git clone https://github.com/ps2homebrew/Open-PS2-Loader
cd Open-PS2-Loader
git checkout 3e3f34e
git apply /path/to/opl-tile-grid.patch
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
| `OPNPS2LD.ELF` | 1360324 | 1361588 | +1264 |

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
| L2 / R2 | first / last page | previous / next **device** |
| L1 / R1 | page up / down | unchanged |

Device switching has to move off Left/Right because the grid needs them. Single
column themes take the `columns > 1` branch nowhere and behave identically to
stock — including the page-up-on-scroll-back behaviour, which is deliberately
preserved rather than replaced with row scrolling.

## Files touched

| File | Change |
|---|---|
| `include/themes.h` | `offset`/`useOffset` on `mutable_image_t`; grid fields on `items_list_t` |
| `src/themes.c` | read the new keys; `drawCellFrame`; `drawItemsListGrid`; offset walk in `drawGameImage` |
| `src/menusys.c` | `menuRowStep`, step helpers, row-wise `menuNextV`/`menuPrevV`, `menuNextItem`/`menuPrevItem`, input remap |

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
