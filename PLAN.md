# PLAN.md — OPL Theme Previewer

Architecture and scope for a browser-based WYSIWYG previewer/editor for Open PS2 Loader themes.
Grounded in [`docs/THEME-FORMAT.md`](docs/THEME-FORMAT.md); every rendering rule cited below has a
section reference there.

**Do not implement past the Non-goals without asking.**

---

## 1. Decision: single self-contained HTML file, vanilla JS, no build step

`opl-theme-previewer.html` — double-click to run, or drop on any static host. No framework.

Justification, since the brief asked for one either way:

* The app is one canvas plus three panels. State is a parsed cfg plus a selection index. React's
  value (diff-driven reconciliation of large trees) buys nothing here; the canvas is redrawn
  wholesale each frame regardless.
* The rendering core is a faithful reimplementation of `rmSetupQuad` / `fntRenderString` — imperative
  drawing code that would sit outside any framework's model anyway.
* No build step means no toolchain rot for a tool that tracks a slow-moving C file, and the user can
  read the whole thing in one editor tab.
* The one real cost is a large single file. Mitigated by strict internal sectioning (§3) and by
  keeping fixtures/tests as separate files.

**Canvas vs DOM:** `<canvas>` for the 640×480 render surface. OPL stretches images to arbitrary
`width`×`height` with no aspect preservation and clips text mid-glyph — trivial on canvas, awkward
with CSS. Hit-testing for selection is done against the same rectangles the renderer computes, so
picking can never drift from what's drawn. Selection chrome (bounds, handles) draws on a second
transparent canvas above, so it never contaminates a "clean" screenshot.

**Font:** `assets/PoeVeticaNew.ttf`, vendored from the OPL tree, loaded via `FontFace`. To keep the
file truly standalone when opened over `file://` (where `fetch` of a sibling file is blocked in
Chrome), the font is **also** embedded as a base64 `data:` URI inside the HTML, with the external
file used as the source of truth for regeneration. `tools/embed-font.js` regenerates the blob.

---

## 2. Fidelity model — what "correct" means

The renderer targets **`rmGetScreenExtents` = 640×480 square pixels, VGA-like** (`ws = hs = 1`), the
case where the console's pixel-aspect correction is identity. Everything else follows §9 of the
format doc:

| Rule | Source | Implementation |
|---|---|---|
| `aligned=0` ⇒ (x,y) top-left; `aligned=1` ⇒ center | §9.1 | `setupQuad()` |
| Images stretch to w×h, never letterboxed | §9.1 | `drawImage` with explicit dw/dh |
| `DIM_UNDEF` (-1) ⇒ intrinsic texture size | §6.1 | resolved at draw time, after art loads |
| `scaled=1` ⇒ `w × iAspectWidth/4` (×0.75 in 16:9) | §9.2 | applied **before** canvas stretch |
| Text baseline `y + (size-2)`, or `y + (size-4)/2` centered | §9.3 | `renderString()` |
| HCENTER subtracts `min(textWidth, boxWidth)/2` | §9.3 | uses `measureText` |
| `\n` advances 19px, not the font size | §9.3 | only when `width != 0` |
| Overflow ⇒ rest of line dropped, no ellipsis | §9.3 | pen locked past `xmax` |
| Word wrap mutates the string once, at load | §9.3 | `fitString()` at parse time |
| Rows step 19px; selected row uses `sel_text_color` | §5.2 | `drawItemsList()` |
| Decorator forced to 20×20 | §5.2 | hard-coded |
| Decorator/text x-mismatch when `aligned=1` | §5.2 quirk | **reproduced, not fixed**; flagged as info |
| Negative x/y ⇒ from right/bottom edge | §6.1 | in `initBasic()` |
| Colors get alpha 0x80; images tint-multiply by `color` | §1.5, §7 | `#808080` = neutral 2× multiply |

**Aspect toggles.** Two independent controls, because they're independent in OPL:

* *Theme aspect* — 4:3 vs 16:9. Sets `iAspectWidth` to 4 or 3, changing the layout itself (§9.2).
* *Display scaling* — integer (1×/2×) vs smooth fit-to-window, plus a 4:3-vs-stretched presentation
  of the finished 640×480 buffer.

**Explicitly approximated** (each gets a README entry and, where visible, an in-app note):

* Plasma background: static converged approximation tinted by `bg_color`, not the animated
  frame-incremental Perlin field (§11.6). Toggle for flat fill.
* Glyph rasterization: browser vs FreeType, ±1px expected (§11.7).
* Internal OPL icons (`MenuIcon`, `BdmIndex`, hint buttons, `LoadingIcon` frames) are drawn as
  labelled placeholder boxes at the correct size unless the theme overrides them, since they come
  from OPL's compiled-in `gfx/` set. *(Stretch: vendor the handful of `gfx/*.png` that themes can
  override.)*

---

## 3. Internal structure of the single file

Nine `// ===== SECTION =====` blocks, in dependency order. Each is independently testable and the
first four are pure (no DOM), which is what the tests exercise.

| # | Section | Responsibility |
|---|---|---|
| 1 | `cfg-parse` | Text → `ConfigDoc`: ordered line records + key index. Round-trip fidelity lives here. |
| 2 | `cfg-write` | `ConfigDoc` + edits → text. Surgical line rewrites only. |
| 3 | `theme-build` | `ConfigDoc` → element chains. Reimplements `addGUIElem`/`initBasic`/`validate*`, emits diagnostics. |
| 4 | `validate` | Additional lint that OPL doesn't do (missing assets, OOM risk, invalid attrs). |
| 5 | `assets` | Folder ingest, `name → ImageBitmap`, placeholder art generation. |
| 6 | `fixture-data` | 15 fake PS2 titles + serials + generated per-game art. |
| 7 | `render` | `setupQuad`, `renderString`, `fitString`, per-type draw functions, plasma. |
| 8 | `editor` | Selection, hit-test, drag, nudge, snap, inspector, cfg text pane, sync loop. |
| 9 | `app` | Wiring, toolbar, file input / drag-drop / File System Access, export. |

### 3.1 `ConfigDoc` — the round-trip-safe representation

The core data structure. A cfg is stored as an **array of line records**, not a key→value map:

```js
{ raw: "\ttype=Background",   // exact original text
  kind: "assign"|"prefix"|"blank"|"other",
  indent: "\t",               // preserved verbatim
  prefix: "main0",            // resolved section at this line, or null
  key: "main0_type",          // composed key ("<prefix>_<key>"), or null
  rawKey: "type",             // as written
  value: "Background",
  sep: "=",
  lineNo: 3 }
```

Plus `index: Map<composedKey, lineIndex[]>` for lookup.

This gives round-trip safety by construction:

* Comments, blank lines, malformed lines, and unknown keys are `kind:"other"`/`"blank"` and are
  **never touched** — they pass through byte-for-byte.
* Editing an existing key rewrites **only** that line, reusing its `indent` and `sep`.
* A new key is inserted at the end of its section, matching the section's dominant indentation
  (prefix form) or as a flat `main3_x=...` if the file uses flat form. Section style is detected
  per-section, so mixed-style files stay mixed.
* Deleting a key removes exactly its line.
* Duplicate keys: the **last** occurrence is edited (matching OPL's read semantics, §11.4) and a
  warning is raised.
* `\r\n` vs `\n` is detected once and reproduced. Trailing-newline presence is preserved.

**Invariant, enforced by test:** `write(parse(text)) === text` for every fixture, and for the
shipped `docs/reference-conf_theme_OPL.cfg`.

### 3.2 `theme-build` — mirroring `addGUIElem`

Follows the C control flow literally so failure modes come out for free:

```
for chain in [main, info, appsMain, appsInfo]:
  i = 0
  loop:
    if `<chain><i>_enabled` == 0      -> diag(info "disabled"); i++; continue
    if `<chain><i>_type` missing      -> diag(...); break            # THE GAP RULE
    if type not in ELEMENT_TYPES      -> diag(warn "unknown type"); i++; continue
    if type == Background and chain non-empty -> diag(warn); i++; continue
    build element from per-type defaults (§7 table) + overrides
    append
  validateBackgroundElems(); validateItemsList()
```

The §7 defaults table is transcribed into a single `ELEMENT_DEFAULTS` object — one place to update
when OPL changes, cross-checked by a test against the numbers in the doc.

Every diagnostic carries `{severity, code, message, line?, element?}` so the validation panel can
link straight to a cfg line and the inspector can badge the element.

---

## 4. Loading a theme

Three ingest paths, all landing in the same `ThemeFolder` abstraction (`Map<relPath, File>`):

1. **`<input type="file" webkitdirectory>`** — baseline, works everywhere including `file://`.
2. **Drag-and-drop of a folder** — `DataTransferItem.webkitGetAsEntry()`, recursive walk.
3. **File System Access API** (`showDirectoryPicker`) — progressive enhancement. When available,
   enables **live reload**: poll `getFile().lastModified` for `conf_theme.cfg` and every referenced
   image every 700 ms, reload only what changed. Feature-detected; the other two paths stay
   available as the fallback.

Asset resolution mirrors §6.4: `default`/`overlay` values are extension-less and only `.png` is
tried. A theme shipping `logo.jpg` with `default=logo` gets a "PNG only since 2024-10-22" warning
naming the exact file — that's one of the highest-value checks in the tool.

Sample game art for `GameImage`/`ItemIcon`/`ItemCover` comes from either:

* an optional `ART/` subfolder in the loaded folder, or a separately-picked art folder, matched as
  `<STARTUP>_<PATTERN>.png` (§6.5); or
* **generated placeholders** — canvas-drawn at the conventional aspect for the pattern
  (`COV` 140×200, `ICO` 64×64, `BG` 640×480, `SCR` 4:3, else square), stamped with the pattern name,
  serial and title so per-game mapping is visually obvious.

A small mapping panel lets the user point individual fake games at real files.

---

## 5. Fake game list

15 plausible PS2 titles with plausible-format serials (`SLUS_xxx.xx`, `SLES_xxx.xx`, `SCUS_xxx.xx`),
deliberately fictional so nothing looks like a claim about a real release. Long and short titles are
both included so `ItemsList` clipping and `AttributeText` wrapping are exercised. Each carries fake
info-page attributes (`Genre`, `Release`, `Developer`, `Size`, `Description`) so `AttributeText`
elements render something real. Selection index is user-controllable (click a row, or ↑/↓ when the
canvas has focus but no element is selected) so `sel_text_color` is directly observable.

---

## 6. Editing

**Selection.** Click hit-tests the element rectangles the renderer just computed, topmost-first
(reverse draw order). Shows a dashed bounds box + numeric badge. Tab/Shift-Tab cycle. Elements with
no drawable output (e.g. `StaticText` missing `value`) are still listed and selectable from the
element tree, since being invisible is exactly the bug you're hunting.

**Move.** Drag updates `x`/`y`; the delta is applied in virtual 640×480 units so it's correct at any
zoom. Alt-drag constrains to one axis. Arrow keys nudge 1px, Shift+Arrow 8px. Snap-to-grid toggle
quantizes to 8px with an optional visible grid. **Negative coordinates are preserved as negative**
(§6.1): if `x` was written `-86`, dragging keeps writing a right-edge-relative value rather than
silently converting to absolute — otherwise the tool would break widescreen-aware themes. A toggle
lets the user force absolute.

**Resize.** Corner/edge handles when `width`/`height` are concrete. Elements at `DIM_UNDEF` show
their intrinsic size in a lighter outline; dragging a handle promotes them to explicit values (with
an undo entry, so it's reversible).

**Inspector.** Every attribute valid for the selected type (§6.6 table), with type-appropriate
widgets: number steppers, `#RRGGBB` color pickers, enum dropdowns for `aligned`/`scaled`/`display`,
a font-slot selector, and free text for `pattern`/`value`/`attribute`. Each row shows the effective
value and marks whether it's **explicit** or **defaulted**, with a one-click "revert to default"
that deletes the line. Attributes the type ignores are shown greyed in a collapsed "not used by this
type" group rather than hidden, since that's a common source of confusion.

**Two-way cfg sync.** A raw text pane beside the canvas.

* *Text → canvas:* debounced 250 ms, reparse, rebuild, redraw. Parse diagnostics appear inline.
* *Canvas → text:* surgical rewrite via §3.1, pane updated in place with cursor/scroll preserved.
* Guarded by a `source` flag so the two directions can't feed each other. If the user is actively
  typing, canvas-side edits are queued rather than clobbering the buffer.

**Undo/redo.** A stack of `ConfigDoc` snapshots (cheap — a cfg is a few KB), coalescing consecutive
drag steps into one entry. Ctrl/Cmd+Z / Shift+Z.

**Export.** `Blob` + `URL.createObjectURL` download of `conf_theme.cfg`. When the File System Access
handle is present, an additional "Save in place" writes back to the original file.

---

## 7. Validation panel

Two groups, because they answer different questions.

**A. "OPL will not render this the way you think"** — replicating engine behavior:

| Code | Severity | Check |
|---|---|---|
| `NUMBER_GAP` | error | `main<N>` missing while `main<N+1>`+ exist ⇒ everything after N is dead (§2.2). Names the exact orphaned keys. |
| `NO_TYPE` | error | Element keys present with no `_type`. |
| `UNKNOWN_TYPE` | error | `type` not in the table — includes a case-sensitivity hint (`itemslist` → `ItemsList`). |
| `BG_NOT_FIRST` | error | `Background` at index > 0 ⇒ discarded, default injected (§3.1). |
| `BG_INJECTED` / `IL_INJECTED` | info | A default was injected; shows what it looks like. |
| `MISSING_REQUIRED` | error | `StaticText` w/o `value`, `AttributeText`/`AttributeImage` w/o `attribute`, `GameImage` w/o `pattern`, `StaticImage` w/o `default`. |
| `THIRD_ITEMSLIST` | warn | Third+ `ItemsList` ignored (§5.1). |
| `DECORATOR_DROPPED` | warn | `decorator` target's `count < displayedItems` (§5.2) — shows both numbers and the `count` needed. |
| `DECORATOR_NO_TARGET` | warn | No `GameImage`/`ItemIcon`/`ItemCover` in the same chain with that pattern. |
| `COLOR_NO_HASH` | error | Color value missing `#` ⇒ parses as **black** (§1.5). |
| `COLOR_MALFORMED` | warn | Not 6 hex digits. |
| `EXT_IN_FILENAME` | warn | `default`/`overlay` value contains `.png`/`.jpg` ⇒ OPL appends another `.png` (§6.4). |
| `JPG_ASSET` | warn | Referenced name exists only as `.jpg`/`.bmp` in the folder — dead since 2024-10-22. |
| `MISSING_ASSET` | error | Referenced `<name>.png` not in the folder. |
| `KEY_TOO_LONG` / `VALUE_TOO_LONG` | warn | >32 / >256 bytes ⇒ silent truncation (§1.3). |
| `DUP_KEY` | warn | Duplicate composed key; states which value wins. |
| `BAD_INDENT` | warn | An assignment after a `prefix:` line that starts at column 0 ⇒ prefix silently cleared (§1.2). Catches space/tab mishaps. |
| `INVALID_ATTR` | info | Attribute not used by this element type (§6.6). |
| `ALIGNED_DECORATOR` | info | The §5.2 x-mismatch quirk is in play. |

**B. "This may not run on real hardware"** — the memory check the brief asked for:

`estimatedBytes = Σ over caches of count × w × h × 3` (decoded `GS_PSM_CT24`, §6.5), where `w`/`h`
are the declared element size or, if `DIM_UNDEF`, the measured size of the supplied/placeholder art.
Reported as a per-cache table plus a total, with thresholds calling out that a fat cache on a 32 MB
console is the classic "theme works in PCSX2, black-screens on PS2" failure. Shared caches
(`findDuplicate`, §6.4) are counted **once**, matching OPL.

Every diagnostic is clickable: jumps the text pane to the line and selects the element.

---

## 8. Deliverables and order of work

Committed in reviewable steps:

1. ~~`docs/THEME-FORMAT.md`~~ *(done)*
2. `PLAN.md` *(this file)*
3. **Parser + writer** + `tests/` — round-trip proven before anything renders.
4. **Theme builder + validator** — element chains and diagnostics, still headless.
5. **Renderer** — canvas, fonts, images, aspect toggles.
6. **Editor** — selection, drag, inspector, two-way sync, export.
7. **Fixtures** — the three themes below.
8. **README.md** — usage + the known-differences list.

### Fixtures (`fixtures/`)

| Folder | Exercises |
|---|---|
| `minimal/` | `Background` + `ItemsList` only. Flat `key=value` form (round-trip coverage for style B). |
| `midrange/` | Prefix form. `POS_MID`, `aligned=0/1`, negative x/y, `ItemCover` + `ItemIcon`, `decorator=ICO`, `AttributeText` with `wrap`, two font slots, custom `sel_text_color`, a full info page. |
| `broken/` | Deliberate: numbering gap at `main4`, `type=itemslist` (wrong case), `Background` at `main3`, `default=logo.png` (extension in name), `text_color=FFFFFF` (no `#`), missing asset, `count=40` on a 512×512 cover, `decorator` whose target caches too few. Every warning class fires. |

Each ships a `README.md` naming what it's testing and, for `broken/`, the expected diagnostic codes —
so the fixtures double as the validator's expectation file.

### Tests (`tests/`)

Two entry points over one shared spec file, so the same assertions run either way:

* `tests/run.mjs` — `node tests/run.mjs`, zero dependencies, exits non-zero on failure.
* `tests/tests.html` — same specs in the browser for `file://` users.

Coverage: parse of both cfg forms and mixed files; CRLF and BOM; `\r\n` preservation; comment,
blank-line and unknown-key passthrough; `write(parse(x)) === x` over all fixtures **and** the OPL
reference cfg; targeted edit → single-line diff; key insertion into both styles; deletion; duplicate
handling; `strToColor` including the missing-`#` and short-value cases; `POS_MID`/`DIM_INF` prefix
matching; negative-coordinate resolution; the numbering-gap / unknown-type / `enabled=0` scan
outcomes; per-type default table; `displayedItems` arithmetic; the decorator count guard; and
`fitString` word wrapping. Target: every §10 discrepancy and every §11 judgment call has at least
one assertion pinning the behavior.

No test framework, no bundler, no package.json dependencies.

---

## 9. Non-goals (unchanged from the brief)

* No emulation of OPL beyond layout rendering — no settings screens, no menu navigation, no game
  launching, no per-game config editing.
* No image editing. Assets are edited externally and hot-reloaded.
* No theme packaging/installer, no device transfer.
* **In scope, as specified:** main page. **Info page: implemented** — it's the same element chain
  with a different prefix and the built-in theme's info page is where `AttributeText`/`AttributeImage`
  actually get used, so a second tab is a small increment rather than a stretch goal. `appsMain`/
  `appsInfo` get tabs too, since §2.3 inheritance is easy to get wrong by hand and worth showing.
* Not planned, ask first: multi-theme comparison, animation preview, PCSX2 screenshot diffing,
  theme templates/wizards, cloud sharing.

---

## 10. Risks

| Risk | Mitigation |
|---|---|
| Text metrics drift from FreeType | Use the actual OPL TTF at the actual size; document ±1px; a pixel-grid overlay + coordinate readout so authors verify against a PS2 screenshot. |
| `file://` blocks loading the sibling TTF | Font embedded as a base64 `data:` URI; external file kept for regeneration. |
| Single file grows unwieldy | Hard section boundaries (§3), pure logic in sections 1–4, ~2500 lines is the working ceiling; if it's exceeded, split renderer into a second file and say so before doing it. |
| Round-trip writer corrupts an exotic cfg | The `write(parse(x)) === x` invariant is asserted over every fixture *and* OPL's own cfg; export is additionally diffed against the input in-app before download, with a warning if unexpected lines changed. |
| OPL changes the format upstream | The §7 defaults table and element list are transcribed into one object with doc-section references in comments, so a re-read of `themes.c` is a localized edit. |
