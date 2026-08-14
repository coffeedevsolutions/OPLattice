# SHELF — Phase 5 design: the Apps page

Design only. The subtitle source is the decision you asked to see before any
code, and it is §2. Two other things in the brief turn out not to be buildable
as written; they are §5 and §6 rather than buried in a footnote.

The layout is not prose — it is in the previewer, under the **SHELF Apps** tab.
That page is hardcoded C, so there is no `conf_theme.cfg` behind it and the
element tree is empty by design. What the mock is for is the two things a design
doc cannot settle: geometry, and whether real glyph advances fit inside it.

---

## 1. What already exists

Apps are not new. `appsupport.c` builds an `app_info_t` list from two sources
and hands it to the generic `item_list_t` vtable:

| source | how | fields |
|---|---|---|
| **`title.cfg`** per-app sidecar | `oplScanApps` → `appScanCallback` | `title`, `boot`, `argv1` |
| **`conf_apps.cfg`** legacy list | `addAppsLegacyList` | `Name=path`, marked `legacy = 1` |

Launching is `appLaunchItem` through the vtable. **Phase 5 writes no launch
code** — X calls what the existing Apps screen already calls, with the same
`config_set_t`. That matters for the master toggle: with `SHELF UI` off there is
no second launch path to have diverged.

---

## 2. Subtitle source — the decision

### Option A — `subtitle=` in the existing `title.cfg` *(recommended)*

`appScanCallback` already parses `title.cfg` into a `config_set_t`. Reading one
more key is one line:

```c
if (configGetStr(appConfig, "subtitle", &sub) != 0) ...
```

Three things make this the right shape rather than merely the easy one:

**It is already append-don't-clobber.** `configWrite` (`config.c:541`) truncates
and rewrites the in-memory list in order, so keys it does not recognise survive
by construction. A `subtitle=` written by hand is preserved through any OPL
write, and an OPL that predates this change ignores it rather than choking.

**It sits with the data it describes.** The subtitle travels with the app
folder. Copy the folder to another card and the subtitle comes with it — no
central file to keep in sync, no orphan rows when an app is deleted.

**It costs no new scan.** The file is already opened, already parsed. A per-ELF
sidecar of our own invention would mean a second file open per app on a device
where opens are not free.

### Option B — a new `APPS.cfg` central list

One file, easier to hand-edit for many apps at once, and it could carry ordering
and grouping later. But it has to be kept in sync with a directory scan by hand,
it strands rows when an app is removed, and it does not travel with the folder.
It also duplicates `conf_apps.cfg`'s job, which is the file we would be adding
alongside.

### Legacy apps get no subtitle, and that is correct

`conf_apps.cfg` is `Name=path` pairs. There is no third field and no syntax for
one that an older OPL would not misread. Legacy entries are flagged `legacy = 1`
already, so the page can simply render no subtitle line for them.

**Not a placeholder — an omission.** A legacy app shows its name with nothing
beneath it. Inventing "Legacy app" or echoing the path would be filling a slot
with text that carries no information, which is the failure the `—` rule in your
Phase 7 brief exists to prevent. The one em-dash on the mock's HDLoader card
shows what a *missing* subtitle looks like on an app that could have had one;
legacy apps get no line at all.

---

## 3. Layout

640×480 virtual, 32px margins, 3 columns × 2 rows.

| | |
|---|---|
| status bar | y 0..40, rule at 41 |
| card | 176 × 170 |
| gutter | 16 |
| columns at x | 32, 224, 416 |
| rows at y | 64, 250 (bottom edge 420) |
| footer rule | y 438, hints at 458 |

Six visible. That is not a compromise for want of room — it is what an app list
usually holds. Nine would fit at 112px tall, but the icon would shrink to about
56px and the subtitle would sit under the name with no air. If your list grows
past six, paging is the answer, not smaller cards.

Card interior: icon 88×88 at `+20`, name baseline `+134`, subtitle baseline
`+154`, leaving 16px below. Selection is a brighter fill and a 2px light border,
using shelf.c's palette verbatim so the page reads as part of the panel it is
reached from.

### The font finding

`FNT_DEFAULT` is one size — `FNTSYS_DEFAULT_SIZE`, about 17px. A subtitle at the
same size as the name is not a subtitle.

`fntLoadFile(NULL, 12)` solves it: a NULL path makes `fntLoadSlot` fall back to
the embedded `poeveticanew_raw` (`fntsys.c:249`), so a second slot at 12px costs
no file and no art. There are 16 slots and themes use a handful.

**It survives theme switches.** `fntRelease` is only ever called with a theme's
own slot ids (`themes.c:1724`), so a SHELF-owned slot is not reclaimed when a
theme unloads. Load it once at init, keep the id.

Names ellipsize at `CW - 16`. The mock measures with the previewer's own text
path — the one that caught the `fontSpec` bug — so if a subtitle does not fit
there, it will not fit on the console.

---

## 4. Input

| button | action |
|---|---|
| D-pad | move within the grid |
| X | `appLaunchItem` via the existing vtable |
| Circle | back to the classic list |
| LEFT at column 0 | opens the sidebar — the Phase 4 gesture, unchanged |

That last row is the reason Phase 4's trigger took an `atLeftEdge` argument
rather than testing the main screen's grid directly. The Apps page is the first
caller that is not the main screen, and it needs no new plumbing.

---

## 5. Free space cannot be shown honestly

The brief expected page context to rescue the free-space line I left out of the
Phase 4 footer. It does not. **The query does not exist.**

Grepping `bdmsupport.c`, `mmcesupport.c` and `ethsupport.c` for a capacity or
free-space devctl returns nothing. The only capacity call in the tree is
`HDIOC_TOTALSECTOR` in `hdd.c:52`, which is the internal HDD and reports *total*
sectors, not free. There is no free-space query for MMCE — the device this
console actually runs from.

So the slot is drawn and the value is an em-dash, exactly as the mock shows.
Same rule as a missing subtitle: the layout reserves the space, and the absence
is visible rather than papered over with a number that would be invented.

Making it real means adding a devctl to the MMCE driver side, which is a
different codebase and a different phase. **I would not fold that into Phase 5**
— it is the only item here that reaches outside OPL, and hiding it inside a UI
phase is how a two-day phase becomes a two-week one.

## 6. The clock is a placeholder that can be made real

`sceCdReadClock` is already used in the tree (`OSDHistory.c:122`) for history
timestamps, reading BCD year/month/day off the CD/DVD RTC. It returns hours and
minutes too. So the clock is not blocked on anything.

Two caveats worth stating before Phase 8 inherits them:

- **The PS2 RTC runs on JST.** Every consumer of it has to subtract 9 hours, or
  apply a configured offset. `OSDHistory.c` does not, because a date is close
  enough for its purpose; a visible clock is not.
- **BCD, not binary.** `btoi()` conversion, as `OSDHistory.c` does.

Phase 5 draws `12:04` in the right place with the right metrics and leaves the
binding to Phase 8. When the clock feature lands it fills this slot; if it never
lands, this is the one placeholder here that is a genuine lie, so it should be
the first thing Phase 8 replaces or the last thing Phase 5 removes.

---

## 7. Icons

The mock draws a flat quad with a letter. Real icons would come through the
theme's apps chain or `appGetIconId`.

**I would not load per-app icon textures in Phase 5.** Six icons at any useful
size is a new simultaneous working set on a page that currently costs nothing in
VRAM, and Phase 6's grid is where the prefetch wrapper and the `VRAM COVERS`
debug line get built. Sizing an icon budget before that instrumentation exists
is guessing. The letter tile is legible and costs a quad.

---

## 8. Build order

1. Small font slot at init, id held by shelf.c.
2. `subtitle` read in `appScanCallback` into `app_info_t`.
3. Grid render from the real apps list, ellipsizing to the mock's metrics.
4. Selection and D-pad movement.
5. X → `appLaunchItem`; Circle → back.
6. Status bar: NET from `gNetworkStartup`, free space as `—`, clock placeholder.

Steps 1–2 are the only ones that touch anything outside `shelf.c`, and both are
additive: a new font slot and a new optional key.

## 9. Risks

**An empty apps list.** A device with no apps must render an empty state, not a
grid of nothing with a selection index pointing at an item that does not exist.
The selection clamp is the same class of bug as the previewer's `gameIndex`.

**Subtitle length.** Ellipsizing is measured against the mock, but the mock's
strings are mine. A real `title.cfg` with a long subtitle is the first thing to
try once step 2 lands.

**Launch parity.** X must produce byte-identical behaviour to the existing Apps
screen, `argv1` included. If it does not, the master toggle stops being a clean
revert — which is the one property the whole campaign is built on.
