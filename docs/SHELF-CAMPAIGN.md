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
| 4 | Sidebar shell + page routing | **slide built, awaiting hardware judgement** — [SHELF-PHASE4.md](SHELF-PHASE4.md) |
| 5 | Apps page | not started |
| 6 | Library grid (absorbs Phase 3's remnants) | not started |
| 7 | Home page | not started |
| 8 | **Game grouping** (was region dual-launch) | not started — scope changed, see below |

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

## Phase 4 — trigger, as built

**C plus A, both bound.** L3 opens the panel anywhere. LEFT at the left edge
opens it too, but only when **held for 12 frames**.

The hold is not optional, and the verification asked for is the reason. LEFT is
**not inert at the left edge in either layout**:

| layout | LEFT at the leftmost item | source |
|---|---|---|
| grid | wraps to the last page | `menuPrevItem` → `menuLastPage` |
| list | steps to the previous device | `menuPrevH` |

So a bare LEFT press at the edge would have hijacked live navigation under both
GridHard and Ominence-Extended. The panel therefore **withholds** the press
while the hold is in question, and `menusys` replays the suppressed navigation
if the hold is released early — a tap still wraps or pages exactly as before.

12 frames rather than the 2 first sketched: 2 frames is 33 ms and would have
fired on nearly every deliberate tap. 12 is 200 ms, clearly past a tap and short
enough not to feel like waiting. The only cost is up to 200 ms of latency on the
wrap gesture specifically, which nothing else depends on.

Still to confirm on hardware: that the gesture cannot fire during a list refresh
or with a dialog open. `shelfTrigger` is only reachable from
`menuHandleInputMain`, so neither path should reach it, but that is an argument
from call sites rather than an observation.

## Phase 8 — scope change: grouping, not region pairs

**Was:** `AltStartup=<GAME_ID>` pairing two rips of one game, with Run offering
"Play US / Play JP".

**Now:** a general grouping mechanism.

| key | in | meaning |
|---|---|---|
| `Group=` | game CFG | group identity; members share a value |
| `Label=` | game CFG | how this member is named in the picker |

Membership is declared per-CFG and assembled at index-scan time — the same scan
Phase 7 already builds for Home, so grouping costs no additional pass. The
details page grows a version picker over the group's members; the SHELF grid
shows **one tile per group**, while the classic list keeps showing every entry.

**Rationale for the change.** Region pairs and franchise runs are the same
problem wearing different clothes: several discs that are one thing on a shelf.
`AltStartup` encodes a pair and a direction, which does not extend to three
members or to sets with no "primary". `Group`/`Label` says only what is true —
these belong together, and here is what to call each — and lets the UI decide
presentation. Region dual-launch becomes a two-member group with region labels;
a sports franchise is the same mechanism with year labels. No new machinery for
the second case.

Deferred as before: hiding non-primary entries from the classic list, and
cross-device groups. Separate saves per member remain correct behaviour.

## Context correction — the display is a 16:9 flat panel, not a CRT

Recorded because two Phase 1 judgements were made against the wrong display, and
one open question follows from it.

**No VRAM or format conclusion changes.** Texture costs, block alignment, the
`maxSize` clamp and the palettized-versus-32-bit arithmetic are all properties of
the GS and are indifferent to what the cable ends at.

### Dithering must be re-judged for the opposite failure

Phase 1 chose Riemersma error diffusion for backgrounds to avoid banding, and
banding is the *CRT* failure mode — a soft display hides dither noise and shows
gradient steps. A sharp panel does the reverse: it resolves the dither pattern
itself, and error-diffusion noise can read as grain or crawling speckle on flat
areas, particularly after the TV's own scaler has had a go at it.

So the acceptance test inverts. Judge the backgrounds for **visible noise**, not
for banding, and specifically on large flat regions rather than on gradients.

**Ordered-dither fallback**, if Riemersma reads as noisy:

```bash
magick "$src" \
  -resize 640x448^ -gravity center -extent 640x448 \
  -ordered-dither o8x8 -colors 256 \
  -define png:color-type=3 -define png:bit-depth=8 \
  "PNG8:$out"
```

Ordered dither trades irregular grain for a regular crosshatch. It is not
strictly better — a repeating pattern can be more objectionable than noise on a
sharp panel, and it survives upscaling more visibly. Worth comparing both against
`+dither` (no dithering, accepting whatever banding 256 colours produces), since
on a modern panel with a good scaler the undithered version is sometimes the
cleanest of the three. Three variants of one background is a ten-minute test and
settles it properly.

### Interlace: avoid 1px horizontal detail

NTSC (`rm_mode_table[2]`) and 1080i (`[11]`) are both `GS_INTERLACED`. On a CRT
that means flicker on single-pixel horizontal lines; on a flat panel the TV
deinterlaces instead, which trades flicker for combing or softness depending on
its deinterlacer. Either way, **1px horizontal detail is the thing to avoid**.

Current state is clean: every theme asset is a solid block with no internal
rules — the 2px rule that used to sit on top of `infoband.png` was removed
earlier, and `topbar`, `botbar` and the caps are single flat bands. The only
line the sidebar draws is **vertical** (`rmDrawLine(x + SHELF_WIDTH, 0, …, 480)`),
which interlacing does not touch.

The constraint to carry forward: SHELF's pages should use blocks, spacing and
colour changes for separation rather than hairlines. If a horizontal rule is
genuinely wanted, 2px aligned to an even Y is the safe form.

Progressive modes are available and would remove the question entirely —
`DTV_480P` (`[3]`, 640×448 non-interlaced) and `DTV_720P` (`[10]`) are both in
the table. 480p costs nothing and changes no maths, since it is the same
640×448 buffer.

### Open question: which video mode is actually running

`conf_opl.cfg` on channel 1 reads `vmode=11`. `gVMode` indexes `rm_mode_table`
directly (`renderman.c:174`), and index 11 is **`DTV_1080I` — 1920×1080, three
passes, `GS_INTERLACED`, native 16:9**.

If the MMCE build is running that rather than NTSC, then the framebuffer figures
in Phase 0 and Phase 1 describe a mode you are not in, and the 1,900,544-byte
texture pool is wrong. Three things are worth separating:

- **The theme work is unaffected either way.** OPL keeps a 640×480 virtual
  coordinate space and scales it (`X_SCALE`, `Y_SCALE`), so every layout number
  in `thm_GridHard` means the same thing in any mode.
- **The `maxSize` clamp is unaffected.** It computes the pool at runtime from
  `gsGlobal->CurrentPointer`, so it adapts to whatever mode is live. That was
  the right design by luck as much as judgement.
- **The documented budget is mode-specific** and would need recomputing.

That config was read from channel 1, which is an FMCB channel and may belong to
a different OPL install than the one being developed against. Worth checking
Settings → Video Mode on the running build before anything is recomputed.

## Display chain — HDMI mod, 16:9 flat panel

Recorded. Reference video mode **pending confirmation** (see below); the pool
figure is computed for every candidate so that confirming the mode is a lookup
rather than another round of arithmetic.

### Texture pool by video mode

Single-pass modes double-buffer. Multi-pass (hires) modes **force double
buffering off** and split one front buffer across passes (`gsHires.c`), which is
why they can end up with *more* texture room, not less.

| idx | mode | psm | framebuffer | **texture pool** | interlaced |
|---|---|---|---|---|---|
| 2 | NTSC 640×448 | CT24 | 2,293,760 | **1,900,544** | yes |
| 3 | DTV 480p 640×448 | CT24 | 2,293,760 | **1,900,544** | no |
| 5 | VGA 640×480 | CT24 | 2,621,440 | **1,572,864** | no |
| 7 | NTSC hires 704×480 | CT24 | 1,441,792 | **2,752,512** | yes |
| 10 | DTV 720p 1280×720 | CT16S | 1,966,080 | **2,228,224** | no |
| 11 | DTV 1080i 1920×1080 | CT16S | 4,423,680 | **−229,376** | — |

**1080i does not fit.** Its front buffer alone exceeds the 4 MB of VRAM before a
single texture is allocated. The mode is in `rm_mode_table` but cannot allocate,
which means the `vmode=11` config cannot describe a working install — it is
either an older config, or a setting that failed and fell back. That resolves
the question by elimination rather than by testimony.

### Does the mode change any Phase 1 dimension margin?

**No — not materially, and not at all for the per-asset figures.**

Phase 1's margins are measured against `maxSize` (1,474,560), a constant in
`textures.c` that has nothing to do with the video mode. Every per-asset margin
in the Phase 1 table is therefore identical in every mode.

What the mode changes is **simultaneity headroom**, and only mildly:

| | 14 grid covers @160×240 | 21 with prefetch |
|---|---|---|
| cost | 702,464 | 1,053,696 |
| fits in smallest viable pool (VGA, 1,572,864) | yes, 870 KB spare | yes, 519 KB spare |

The 160×240 recommendation holds in every mode that works, including the
tightest. No Phase 1 number needs revising on account of the mode.

### Hairline constraint

Mode-dependent, so it follows the reference-mode decision:

- **Interlaced (2, 7)** — constraint stands as recorded: no 1px horizontal
  detail; 2px on an even Y if a rule is wanted.
- **Progressive (3, 5, 10)** — relaxes to normal 1px freedom.

Nothing currently drawn is affected either way: all theme assets are solid
bands, and the sidebar's only line is vertical.

### Outstanding

Three details were left as unfilled brackets and are still needed before the
ledger can be closed on this:

1. **Reference video mode** — which the running build reports.
2. Whether the `vmode=11` config belongs to this install (the arithmetic above
   says it cannot be live, but the record should say which install it came from).
3. The hairline verdict, which follows automatically from (1).

Answering (1) settles (3) and selects a row from the table above.
