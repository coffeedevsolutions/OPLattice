# OPLattice

A custom [Open PS2 Loader](https://github.com/ps2homebrew/Open-PS2-Loader) build
and the toolkit that produced it: a sidebar-driven UI called **SHELF**, nine
patches to OPL's renderer and menu system, four themes, an art pipeline, and a
WYSIWYG theme previewer that runs in a browser.

It exists because OPL's theme engine cannot express a modern console dashboard.
Stock OPL hands every theme element the same "currently selected game" pointer,
and its only list widget is a hard-coded single 19px column — so a grid of cover
art is not reachable from a `conf_theme.cfg` at all, no matter how it is
written. Everything here follows from fixing that.

> **Scope.** This is a PS2 homebrew loader front-end. It ships **no game art and
> no games** — see [Supplying art](#supplying-art).

---

## What's in here

| | |
|---|---|
| [`patches/`](patches/README.md) | Nine patches against OPL. Grid themes, sort modes, recently-played, menu tabs, MMCE support, texture safety, play stats, and the SHELF sidebar |
| [`patches/shelf/tree/`](patches/shelf/tree/) | Whole copies of every file patch 09 modifies — the source of truth the patch is regenerated from |
| [`themes/`](themes/README.md) | Four themes. One runs on stock OPL; three need the grid patches |
| [`opl-theme-previewer.html`](opl-theme-previewer.html) | The theme previewer, in one self-contained file — see [`docs/THEME-PREVIEWER.md`](docs/THEME-PREVIEWER.md) |
| [`tools/`](tools/) | Art conversion, ISO checking, device staging, card and HDD sync |
| [`docs/`](docs/) | Format reference, the SHELF campaign record, booting notes |
| [`fixtures/`](fixtures/) | Three test themes, including one with ten deliberate faults |
| [`tests/`](tests/) | 93 assertions, zero dependencies |

## Status

Honest ledger. Detail is in [`docs/SHELF-CAMPAIGN.md`](docs/SHELF-CAMPAIGN.md).

| Phase | Scope | Status |
|---|---|---|
| 0, 0a | Reconnaissance, gsKit addendum | done |
| 1 | Art dimensions, pipeline, safety fixes | done |
| 2 | Play-stats minimal core (patch 08) | done |
| ~~3~~ | ~~Streaming texture manager~~ | **struck** — gsKit already does it better |
| 4 | Sidebar shell + page routing | **verified on hardware at 480p** |
| 5, 6, 7 | Apps page, Library grid, Home page | complete |
| 8 | Game grouping | not started |

The theme patches `01`–`03` compile clean with the real toolchain — `make` exit
0, zero warnings — and their effect is confirmed by diffing section sizes
against an unpatched build of the same tree. The SHELF build has been run on
real hardware; the standalone theme patches have not been booted in isolation.
Read [`patches/README.md`](patches/README.md) before flashing anything, and test
on a setup you can recover.

## How it works

Four layers, each usable without the ones above it.

**1 — The patches.** OPL is C, built with the `ps2dev` toolchain. Patches `01`
and `02` teach `ItemsList` to lay out a grid and `GameImage` to address a slot
other than the highlighted one, which is what makes cover grids possible from a
cfg. The art cache was never the obstacle — it is already item-addressed, and
every list entry already carries its own slot. The patch only changes *which
entry gets asked for*; caching, async loading, LRU eviction and the IO queue are
untouched.

**2 — SHELF** (patch 09) is a different proposition: a sidebar shell with its
own pages — Home, Library, Apps, Settings — drawn over OPL rather than
configured through it. It is inert unless `gEnableShelfUI` is set, and the gate
lives at the call sites in `gui.c`, so with the toggle off no state is advanced
and no draw is issued.

**3 — Themes** are ordinary OPL themes that use the new keys. Four ship here;
`thm_UnifiedLibrary` needs no patch at all.

**4 — The art pipeline** converts truecolor sources to the indexed PNG8 the
console actually wants. This matters more than it sounds: each art cache costs
`count × source width × height × 3` bytes of decoded texture held resident, and
the element's drawn size does not reduce it. A 512×512 cover with `count=40` is
~31 MB against a 32 MB console — a theme that looks perfect in PCSX2 and
black-screens on hardware.

## Install

Full walkthrough in [`docs/INSTALL.md`](docs/INSTALL.md). The short version:

**Use a prebuilt ELF.** Grab `OPNPS2LD.ELF` from
[Releases](../../releases) and put it where your chip boots OPL from — usually
`mc0:/OPL/OPNPS2LD.ELF`, or `mmce0:/APPS/` on an SD2PSX. Binaries are Release
assets rather than repository files, because rebuilding two 1.3 MB ELFs on every
commit had put 110 MB of dead weight in this history.

**Or build it.** Nothing here needs a local toolchain — the build runs in Docker:

```bash
git clone --depth 1 --branch OPL-MMCE-beta-2 \
  https://github.com/ps2-mmce/Open-PS2-Loader opl-mmce
cd opl-mmce
python3 ../patches/04-mmce-fork-toolchain.py .
for p in 01-opl-tile-grid 02-opl-sort-and-recent 03-opl-menu-tabs \
         05-mmce-device-integration; do git apply --3way ../patches/$p.patch; done
docker run --rm -v "$PWD":/src -w /src ps2dev/ps2dev:latest sh -c \
  'apk add --no-cache make git bash python3 py3-yaml >/dev/null && \
   git config --global --add safe.directory /src && make RELEASE=1'
```

`04` must run first — the MMCE fork branched in January 2025 and never rebased,
so it does not compile on a current toolchain until those fixes land. Build
serially; under `-j` the export-table steps race and fail spuriously.

Building against **mainline** instead is the same shape minus `04` and `05`, but
note that a mainline ELF has no MMCE support at all — flashing one on an MMCE
setup makes the memory-card SD disappear.

**Then install a theme.** Copy a folder from [`themes/`](themes/) next to your
other OPL themes and pick it in **Settings → Theme**. The `thm_` prefix must
stay; OPL only scans directories containing it.

## Using it

Open the sidebar with **L3**, from anywhere — or hold **Left** at the left edge
of the main list. **L3** or Back closes it.

| Where | Button | Does |
|---|---|---|
| Sidebar | Up / Down, then confirm | Home, Library, Apps, Settings |
| Library | D-pad | Move through the grid |
| Library | **Square** | Cycle ordering |
| Library | **L1 / R1** | Change the filter |
| Library | **L2 / R2** | Jump a section |
| Details | **Triangle** | Favourite, written through to the game CFG |
| In game | **D-pad Up** | Screenshot to `mc1:` (needs GSM enabled for that title) |

Confirm and Back are **not** hardcoded to X and O. Which button confirms is the
console's decision — `gSelectButton` is O on a Japanese machine and X elsewhere
— and the footer draws whichever glyph actually works.

### Supplying art

This repository ships no cover art. Point the tools at your own:

```bash
tools/check-art.py <ps2-dir>          # verify every ISO has art under the name OPL will look for
tools/make-cover.py                   # build 216x432 COVXL from full-resolution masters
tools/palettize-art.py                # convert the batch to indexed PNG8
tools/stage-device.sh [art] [out]     # assemble the device tree into _deploy/
```

`check-art.py` is the one to run first. It mounts each ISO the way OPL does —
reading `BOOT2` out of `SYSTEM.CNF` — and checks that `ART/<STARTUP>_COV.png`
exists under that exact name. Filenames on disk are irrelevant to OPL, and this
is the one mismatch that silently produces a grid of placeholder tiles.

Naming, patterns and exact texel dimensions are documented in
[`_art-truecolor/README.md`](_art-truecolor/README.md).

## Designing a theme

Use the previewer. It renders what OPL will actually draw, derived by reading
OPL's renderer rather than its documentation, and it flags every patch-only key
so a theme cannot quietly depend on a build you do not have.

```bash
open opl-theme-previewer.html
```

No build step, no dependencies, no server. Full guide:
[`docs/THEME-PREVIEWER.md`](docs/THEME-PREVIEWER.md).

## Tests

```bash
node tests/run.mjs
```

93 assertions, zero dependencies. The suite extracts the core logic straight out
of `opl-theme-previewer.html` between marker comments, so there is no second
copy to drift.

## Documentation

| | |
|---|---|
| [`docs/THEME-PREVIEWER.md`](docs/THEME-PREVIEWER.md) | The previewer: loading, editing, validation, known differences |
| [`docs/THEME-FORMAT.md`](docs/THEME-FORMAT.md) | Theme format derived from OPL's source, including the 14 places the official guide and the code disagree |
| [`docs/INSTALL.md`](docs/INSTALL.md) | Building, flashing, deploying to a device |
| [`docs/BOOTING.md`](docs/BOOTING.md) | FMCB channels, the L1 hold, and which build you are actually running |
| [`docs/SHELF-CAMPAIGN.md`](docs/SHELF-CAMPAIGN.md) | Phase ledger and the decisions that changed the plan |
| [`docs/HDD-LAYOUT.md`](docs/HDD-LAYOUT.md) | Partition layout |
| [`docs/GENRES.md`](docs/GENRES.md) | The sixteen-value genre vocabulary |
| [`patches/README.md`](patches/README.md) | Every key each patch adds, and why |

## Licence

[GPL-3.0-or-later](LICENSE).

That is not a free choice: `src/cheatman.c`, which patch 09 modifies, comes from
PS2rd under GPL-3.0-or-later, so its terms attach to the combined work. Most
other vendored OPL sources are Academic Free License 3.0 and keep their notices
verbatim. Fonts, art policy and the full picture are in
[`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).

Open PS2 Loader is the work of the ps2homebrew community; SHELF's slide reuses
the shape of Maximus32's screen crossfade; in-game screenshots come from
maximus32 and doctorxyz. None of this would exist without them.
