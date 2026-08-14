# SHELF — campaign record

Phase status and the decisions that changed the plan. Detail lives in the
per-phase documents; this is the ledger.

| phase | scope | status |
|---|---|---|
| 0 | Reconnaissance | **done** — [SHELF-PHASE0.md](SHELF-PHASE0.md) |
| 0a | gsKit addendum | **done** — [SHELF-PHASE0-ADDENDUM.md](SHELF-PHASE0-ADDENDUM.md) |
| 1 | Art dimensions, pipeline, safety fixes | **done** — [SHELF-PHASE1.md](SHELF-PHASE1.md) |
| 2 | Play-stats data dependency | **done** — minimal core, patch 08 |
| ~~3~~ | ~~Streaming texture manager~~ | **STRUCK** — see below |
| 4 | Sidebar shell + page routing | **design done, awaiting trigger choice** — [SHELF-PHASE4.md](SHELF-PHASE4.md) |
| 5 | Apps page | not started |
| 6 | Library grid (absorbs Phase 3's remnants) | not started |
| 7 | Home page | not started |
| 8 | Region dual-launch | not started |

Numbering is kept rather than compacted, so that references to "Phase 6" in the
original campaign brief still mean the library grid.

## Tombstone — Phase 3, streaming texture manager

**Struck after the Phase 0 addendum.** The manager it proposed to build already
exists in gsKit, and is better than the design it would have replaced.

- Eviction is not LRU but a two-frame use-count predictor, which protects
  textures whose bind count is still rising — the right instinct for a menu.
- Texture and CLUT are allocated as one block, transferred together and evicted
  together. The CLUT-pairing hazard the phase was designed around cannot occur.
- The EE-RAM decoded-asset cache already exists as `texcache.c`.

Building alongside it would have reimplemented a working allocator and fought it
for the same VRAM.

Four remnants, and where they went:

| remnant | disposition |
|---|---|
| prefetch wrapper | Phase 6, where the grid consumes it |
| debug-line accounting | Phase 6, OPL-side approximate counter. **Must be labelled approximate** — it counts binds and cannot observe gsKit's evictions. No gsKit patch. |
| `maxSize` guard | **pulled forward into Phase 1**, shipped |
| `themes.c:395` else branch | **Phase 1**, shipped |

## Phase 1.5 — 16-bit framebuffer

Parked indefinitely. Not to be implemented unless a later phase demonstrates
texture-budget pressure. Recorded for completeness: it would halve framebuffer
cost and raise the texture pool from 1,900,544 to 3,047,424.

## Phase 2 — play-stats minimal core

The sibling clock/play-stats prompt has not been run here, so this is the
"implement minimal core" branch. Written to that prompt's conventions so a later
full implementation supersedes it cleanly rather than colliding.

| key | where | format |
|---|---|---|
| `LastPlayed` | game CFG | `DD-MM-YYYY` |
| `PlayCount` | game CFG | integer, incremented at launch |
| `Playtime` | game CFG | **raw integer minutes** |
| `play_stats` | main config | toggle, default 0 |
| `pending_startup`, `pending_stamp` | `CONFIG_LAST` | the in-flight session |

Gated on `play_stats` **and** `gEnableWrite`; the row is hidden when write
operations are off, since `dia` has no disabled state.

**The power-off case.** A session ended with the power button leaves its record
on disc, and the next cold boot would compute launch-to-now — including however
long the console sat switched off. There is no way to tell that from a genuine
marathon after the fact, so **sessions over 12 hours are discarded and logged**
rather than recorded. A missing session is recoverable; a fabricated 40-hour one
silently poisons a total nobody can audit. The record is cleared before the
delta is judged, so an unresolvable one cannot be retried every boot.

**Timezone.** `statsNowMinutes` reads the RTC raw, no conversion. It is only
ever subtracted from another reading of the same clock, so a constant offset
cancels, and JST has no daylight saving. `LastPlayed`'s *displayed* date does
inherit the offset and can be a day out either side of midnight — a known limit
of the minimal core, to be resolved by the full clock feature which will own the
timezone properly.

**Write Operations dependency.** The runtime was gated from the start — both
`oplStatsOnLaunch` and `oplStatsOnReturn` check `gEnableWrite`, so stats have
never been able to write with it off. The *interface* was only gated at
dialog-open time, which left a real hole: turning write operations off inside a
settings session left the Play Stats row visible and still reading On while
doing nothing. `guiUpdater` now re-evaluates it on every change, hiding the row
and its label the moment write operations goes off, matching how OPL already
handles `LASTPLAYED -> AUTOSTARTLAST`. Hidden rather than greyed because `dia`
has no disabled state. The stored value is preserved, so re-enabling write
operations restores the preference.

**Fold-in timing.** `oplStatsOnReturn` runs after `applyConfig`, because
locating the game's CFG needs the device lists to exist. A game whose device is
absent on return loses its minutes, logged — the honest outcome, since there is
nowhere to write them.
