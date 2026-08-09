# `midrange/` — the interesting features

Written in the **prefix** form (tab-indented), like OPL's own
`conf_theme_OPL.cfg`. Nothing here is broken; it exists to exercise the parts of
the format that are easy to get wrong.

| Feature | Where | Why it matters |
|---|---|---|
| `POS_MID` | `main1_x`, `main7_x`, `main8_x` | Resolves to 320 (or 240 for `y`). Matched as a 7-char prefix, so `POS_MIDDLE` works too. |
| Negative coordinates | `main4`, `main5`, `main6` | `x=-118` means `640-118`, i.e. measured from the **right** edge. Undocumented in the official guide but used constantly. |
| `aligned=0` vs `1` | `main0` vs `main1` | 0 anchors the top-left, 1 centres on `(x,y)`. It is not a bitfield. |
| Decorator | `main3_decorator=ICO` → `main2` | Matches by **pattern string**, not element name. `main2_count=24` ≥ the 16 rows, so it attaches; drop it below 16 and OPL silently stops drawing row icons. |
| Overlay | `main4_overlay=case` + 8 corner offsets | Frame drawn on top of the cover, corners relative to the quad's top-left. |
| Two font slots | `default_font`, `font1` + `font1_size` | `main1_font=1` renders at 22px. Both slots point at the real OPL font, shipped here, so the slot actually loads — a slot whose TTF is missing reverts to slot 0 *including its size*. |
| Word wrap | `info4_wrap=1` with width+height | `fntFitString` rewrites the string once at load, inserting newlines; lines then advance 19px, not the font size. |
| Apps inheritance | `appsMain1` | Overrides only the header; every other index falls back to its `main<N>` twin. Compare the Main and Apps main tabs. |
| Full info page | `info0`–`info6` | Where `AttributeText` and `AttributeImage` are actually used. |

Assets: `backdrop.png`, `cover.png`, `case.png` (RGBA overlay frame),
`PoeVeticaNew.ttf`.

Per-game art is **not** bundled. Load a folder of `<SERIAL>_<PATTERN>.png` files
with the "ART folder…" button to see real covers, or leave it and the previewer
draws labelled placeholders at the right aspect.
