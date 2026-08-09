# `broken/` — expected diagnostics

Every fault here is deliberate. Loading this folder should light up the
validation panel; if a code below stops appearing, something regressed.

| # | In the cfg | Code | What OPL actually does |
|---|---|---|---|
| 1 | `text_color=FFFFFF` | `COLOR_NO_HASH` | No leading `#`, so `strToColor` returns without parsing and leaves the zeroed buffer — the text renders **black**, not white. Not an error on the console, just wrong. |
| 2 | `main0_default=logo.png` | `EXT_IN_FILENAME` | `texDiscoverLoad` appends `.png` itself, so it looks for `logo.png.png` and finds nothing. |
| 3 | `main1_decorator=ICO` with `main2_count=6` | `DECORATOR_DROPPED` | The list is 380px tall → 20 rows, but the `ItemIcon` caches 6. OPL requires `count ≥ rows` or it silently drops the row icons. |
| 4 | `main3_type=Background` | `BG_NOT_FIRST` | A `Background` past index 0 is discarded **and the index is still consumed**. Validation then injects a default background at the head. |
| 5 | `main4` `StaticText` with no `value` | `MISSING_REQUIRED` | The element is constructed but gets no draw function, so it silently never appears. |
| 6 | `main5_type=itemslist` | `UNKNOWN_TYPE` | Types are compared with `strcmp`. Note this does **not** stop the scan — `main6` still loads. |
| 7 | `main6` 512×512 cover with `count=40` | memory panel → `err` | ≈31 MB of decoded texture on a 32 MB console. Works in PCSX2, black-screens on hardware. |
| 8 | `main6_default=missing_cover` | `MISSING_ASSET` | `missing_cover.png` is not in the folder. |
| 9 | `main7` has no `type` | `NUMBER_GAP` | This ends the scan for the page. `main8` and `main9` are **never read** — the classic "why did my element vanish" bug. |
| 10 | `info1_pattern=COV` | `INVALID_ATTR` | `AttributeText` never reads `pattern`; the line is dead weight. |
| 11 | `sel_text_color=#0FF` | `COLOR_MALFORMED` | Three-digit shorthand is not supported. OPL accumulates two hex digits per channel, so this reads as `#0FF000` — green, not cyan. |

Loading this folder should produce **exactly these eleven** diagnostics and a
red art-cache total of ~31 MB. Duplicates would mean the apps-chain
de-duplication regressed; extra codes mean something else broke.

Also present: `DUP_KEY` is *not* expected here, and `COMMENT_PREFIX` must **not**
fire — no comment in the file contains a colon, which is itself the point. A `#`
line with a `:` and no `=` satisfies OPL's `parsePrefix` and becomes a section
header, silently re-homing every key below it.

Assets in this folder: `logo.png` and `backdrop.png` exist; `missing_cover.png`
deliberately does not.
