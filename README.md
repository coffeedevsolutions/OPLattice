# OPL Theme Previewer

A WYSIWYG previewer and editor for [Open PS2 Loader](https://github.com/ps2homebrew/Open-PS2-Loader)
themes, in one self-contained HTML file. Load a theme folder, see what OPL will
actually draw, drag things around, and export a `conf_theme.cfg` that keeps your
comments and formatting intact.

Rendering rules were derived by reading OPL's renderer, not the documentation —
see [`docs/THEME-FORMAT.md`](docs/THEME-FORMAT.md), which also records the
[14 places](docs/THEME-FORMAT.md#10-documentation-discrepancies) where the
official theme guide and the code disagree.

---

## Open it

```bash
open opl-theme-previewer.html
```

Double-click works; there is no build step, no dependencies and no server. The
font is embedded, so text metrics are correct over `file://` too.

If you'd rather serve it (needed only for the browser test page):

```bash
python3 -m http.server 8731
```

## Load a theme

Three ways, in increasing order of convenience:

1. **Open theme folder…** — a directory picker. Works in every browser.
2. **Drag the folder onto the window.**
3. **Live reload** — Chrome/Edge only. Picks the folder through the File System
   Access API, then watches it and re-renders whenever you save `conf_theme.cfg`
   or any image in an external editor. This also enables **Save in place**.

You can also paste a `conf_theme.cfg` straight into the text pane at the bottom
with no folder at all; you just won't get asset checks or images.

### Per-game art

`GameImage`, `ItemIcon` and `ItemCover` elements pull art from the console's
`ART` folder, not from the theme. Point **ART folder…** at a folder of
`<SERIAL>_<PATTERN>.png` files — the same naming OPL uses
(`SLPS_250.50_COV.png`, `SCPS_150.21_BG.png`, …). An `ART/` subfolder inside the
theme folder is picked up automatically.

With no art loaded, the previewer draws labelled placeholders at the
conventional aspect for each pattern, so layout still reads correctly.

The sample game list uses real serials and disc sizes so the row text and the
art filenames are representative. The info-page attribute values (genre,
release, developer, description) are **marked sample strings, not a game
database** — they exist to exercise wrapping and clipping at realistic lengths.

## Edit

| Action | How |
|---|---|
| Select | Click an element, or pick it in the left-hand list. <kbd>Tab</kbd> cycles. |
| Move | Drag. <kbd>Alt</kbd> constrains to one axis. |
| Nudge | Arrow keys (1px), <kbd>Shift</kbd>+arrows (8px). |
| Resize | Drag the handles on a selected element. |
| Exact values | Type them in the inspector. |
| Snap | **Snap** button quantises drags to 8px; **Grid** shows the grid. |
| Undo / redo | <kbd>Ctrl/Cmd</kbd>+<kbd>Z</kbd> / <kbd>Shift</kbd>+<kbd>Ctrl/Cmd</kbd>+<kbd>Z</kbd>. |
| Revert one attribute | Hover the inspector row and click **reset** — deletes that line. |
| Change the selected game | Click a row on the canvas or in the game list. |

The inspector dims attributes that are **defaulted** and shows in full those
that are **explicit** in the file, so you can see at a glance what the theme
actually sets. Attributes the element's type never reads are collected in a
separate collapsed group rather than hidden.

**The cfg pane is live in both directions.** Type in it and the canvas
re-renders; drag on the canvas and only the affected lines are rewritten —
comments, blank lines, key order, indentation style and unknown keys are
preserved byte-for-byte. Negative coordinates stay negative, so a theme written
against the right or bottom edge keeps working in widescreen after you drag it.

**Export conf_theme.cfg** downloads the result. With Live reload on, **Save in
place** writes back to the folder you opened.

## What the validation panel catches

Two groups. The first replicates OPL's own behaviour, i.e. things that will look
wrong or vanish on the console:

* **Numbering gaps** — the big one. A missing `main5_type` ends the scan, so
  `main6` and everything after it is never read. The panel names the orphans.
* Unknown or miscased `type` (types are compared with `strcmp`; `itemslist` is
  not `ItemsList`) — note this does *not* stop the scan, unlike a gap.
* `Background` anywhere but first, and the default OPL injects in its place.
* Missing required attributes: `StaticText` without `value`, `GameImage`
  without `pattern`, and so on — these build fine and silently never draw.
* Colours without a leading `#`, which parse as **black** rather than erroring.
* A comment containing `:` — OPL reads it as a section header and silently
  re-homes every key below it.
* A `decorator` whose target caches fewer images than the list has rows, which
  makes OPL drop the row icons entirely.
* File extensions inside `default`/`overlay` values (OPL appends `.png` itself),
  missing assets, and assets that exist only as `.jpg` — JPG and BMP support was
  removed from OPL on 2024-10-22.
* Keys or values past OPL's 32/256-byte limits, and duplicate keys.

The second is the hardware-safety check: **PS2 art cache estimate**. Each art
cache costs `count × source width × height × 3` bytes of decoded texture, held
resident — the element's drawn size does not reduce it. Shared patterns are counted once, matching OPL's `findDuplicate`. A
512×512 cover with `count=40` is ~31 MB against a 32 MB console — a theme that
looks perfect in PCSX2 and black-screens on real hardware. Every diagnostic is
clickable and jumps to the offending line.

## Pages

Tabs for **Main**, **Info**, **Apps main** and **Apps info**. The apps pages
mirror the main pages by index unless you define an `appsMain<N>` block with the
same number, which is easy to get wrong by hand — the inspector tells you when
an element is inherited and what editing it will affect.

## Themes and the tile-grid patch

[`themes/`](themes/) holds four ready themes, and [`patches/`](patches/README.md)
holds two OPL patches adding what the stock renderer cannot do.

Stock OPL hands every element the same "currently selected game" pointer, and its
only list widget is a hard-coded single 19px column. So a grid of covers is not
reachable from a cfg. Patch 01 adds grid keys to `ItemsList`, an `offset` key to
`GameImage`, and two-axis navigation. Patch 02 adds R3-cycled sort modes, a
persistent recently-played list, and `RecentImage`/`RecentText` elements to show
it. **Both build clean with the real PS2 toolchain (`make` exit 0, zero warnings)
and a ready `OPNPS2LD.ELF` is included, but neither has been booted** — see the
patch README.

The previewer implements the patched behaviour and flags every patch-only key
with a `PATCHED_ONLY` note, so a theme can't quietly depend on a build you don't
have.

## Fixtures

| Folder | What it's for |
|---|---|
| [`fixtures/minimal/`](fixtures/minimal/) | `Background` + `ItemsList`, in the flat `key=value` form. |
| [`fixtures/midrange/`](fixtures/midrange/) | `POS_MID`, negative coordinates, decorators, overlays, two font slots, word wrap, apps inheritance, a full info page. |
| [`fixtures/broken/`](fixtures/broken/) | Ten deliberate faults, one per warning class. Its README lists the expected diagnostic codes. |

## Tests

```bash
node tests/run.mjs
```

Zero dependencies. Both this and `tests/tests.html` (open it over the local
server) extract the core logic straight out of `opl-theme-previewer.html`
between marker comments, so there is no second copy to drift.

93 assertions covering both cfg forms, CRLF/BOM handling, `write(parse(x)) === x`
over every fixture *and* OPL's own `conf_theme_OPL.cfg`, surgical single-line
edits, the numbering-gap/unknown-type/`enabled=0` scan outcomes, the per-type
defaults table, `displayedItems` arithmetic, the decorator count guard, colour
parsing including the missing-`#` case, widescreen geometry and word wrapping.
Every documented discrepancy has at least one assertion pinning the behaviour.

---

## Known differences from real OPL

Honest list. Nothing here affects layout decisions, but you should know.

* **Text is ±1px.** The previewer uses OPL's own `PoeVeticaNew.ttf` at the same
  17px/72dpi and sums integer per-glyph advances — which is exactly what
  FreeType does here, since the font has no `kern` table. What differs is glyph
  rasterisation and hinting between the browser and FreeType. Line positions,
  centring and clipping points are faithful; individual glyph edges may sit a
  pixel out.
* **The previewer assumes square pixels.** OPL re-derives the font size from the
  active video mode's pixel aspect ratio (`fntUpdateAspectRatio`). This tool
  targets the VGA-like 1:1 case. On an interlaced NTSC/PAL console, text is
  scaled vertically by the same factor as everything else — relative layout is
  unaffected.
* **The plasma background is static.** OPL's is an animated Perlin field that
  updates 6 rows a frame and eases toward `bg_color` over time. This draws a
  converged, stable approximation of it — a layout tool wants a still image.
  Toggle **Plasma** off for a flat `bg_color` fill.
* **OPL's built-in icons are placeholders.** `MenuIcon`, `BdmIndex`, the hint
  buttons and the busy-icon frames are compiled into OPL rather than read from
  the theme, so they render as labelled boxes at the correct size and position.
  Anything your theme supplies is drawn for real.
* **Hint text is representative, not exact.** The main page shows OPL's actual
  hint set (Menu / Run / Info / Options / Refresh) at the right spacing, but
  labels come from the English string table and real widths depend on the
  active language.
* **No animation, no navigation, no settings screens.** This renders layout. It
  does not emulate OPL.
* **Overlay corner offsets default to 0.** OPL doesn't zero-initialise them, so
  an overlay with only some `overlay_*` keys set is genuinely undefined on
  hardware. The previewer picks 0 and warns.
* `ui_text_color` is parsed and shown but never drawn — it only affects OPL's
  own settings dialogs, which are out of scope.
* `use_real_height` is parsed and applied literally, but it cannot change
  anything: OPL's virtual screen is always 640×480.

## Layout of this repo

```
opl-theme-previewer.html   the whole tool
docs/THEME-FORMAT.md       format reference derived from OPL's source
docs/reference-conf_theme_OPL.cfg   OPL's own built-in theme, for comparison
PLAN.md                    architecture and scope
assets/PoeVeticaNew.ttf    OPL's built-in font (source of truth for the embed)
tools/embed-font.mjs       regenerates the embedded base64 font
fixtures/                  three test themes
tests/                     spec + node runner + browser runner
```

`assets/PoeVeticaNew.ttf` is vendored from the Open-PS2-Loader tree
(`thirdparty/`) and carries its own licence.
