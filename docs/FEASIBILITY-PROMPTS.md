# Feasibility report — clock, freeze overlay, achievement telemetry

Assessment of the three prompts in `~/Downloads/prompts/` against
`ps2-mmce/Open-PS2-Loader @ OPL-MMCE-beta-2` (`bb415d2`), the tree this project
already builds and patches.

Everything below was checked against source, not recalled. Line references are
to that tree. Where I could not verify something it says so.

---

## 1. Ranking

Ordered by how fast each can reach something you can see working. Estimates
assume the working style of this project — an agent doing the edits, you
reviewing between phases — and they are estimates, not commitments.

| # | Feature | Effort | Risk | Why it lands there |
|---|---|---|---|---|
| **0** | **In-Game Screenshot (not in any prompt)** | **~half a day** | very low | Already written and shipped in-tree. Gated behind a build flag. Fills the `_SCR` slots you added an hour ago. |
| **1** | Clock display — prompt 1, Goal A | ~1 day | low | RTC read, BCD conversion, toggle pattern and element-adding all have working precedents. |
| **2** | Play stats — prompt 1, Goal B | ~2–3 days | low-medium | CFG round-trip already preserves unknown keys. The only hard part is a design decision, not a technical unknown. |
| **3** | Achievements Phase 1 — server + PCSX2 | ~1 week | low | Entirely Mac-side. No console risk at all. Ordinary Python work. |
| **4** | Achievements Phase 2 — hardware sampler | ~2–4 weeks | medium-high | Both halves exist in-tree but are wired to the wrong device mode. |
| **5** | Freeze overlay — prompt 2 | ~4–8 weeks | high | Every supporting piece exists; the freeze itself is genuinely new and may never work on all titles. |

The two prompts that read as hardest are not equally hard. Achievements is
mostly assembly of parts that exist. The overlay asks for one thing nobody in
this tree has done.

---

## 2. The finding that reframes two of these

`ee_core/` is OPL's resident in-game payload — roughly 7,300 lines that stay
alive inside a running retail game. It already contains:

| File | What it is | Which prompt it serves |
|---|---|---|
| `padhook.c` | IGR pad hook, **on a VBLANK_END interrupt handler**, reading the pad buffer directly | overlay: the freeze trigger |
| `gsm_engine.S`, `gsm_engine_adv.S`, `gsm_api.c` | per-frame display-register interception | overlay: compositing |
| `igs_api.c` | **in-game framebuffer capture** (GPL, maximus32/doctorxyz) | overlay: VRAM path; also `_SCR` art |
| `cheat_engine.S`, `cheat_api.c` | resident engine hooking in-game code addresses | achievements: the sampler |
| `syshook.c`, `iopmgr.c`, `modmgr.c` | syscall hooking, IOP module management across game boot | both |

Both prompts open by asking "is it feasible to keep code alive inside a retail
game." The answer is that this build already does, five different ways, and has
for years. The prompts' Phase 0 investigations are mostly answered by reading
these files.

**`IGR_COMBO_START_SELECT` already exists** (`padhook.c:155`) — the exact combo
the overlay prompt specifies, already detected, already inside a running game.

---

## 3. Prompt 1 — Clock + play stats

### Goal A: the clock — the easy one

**RTC access: solved, with a working precedent in-tree.**

```c
// ee_core is not even needed; this is main-loop code.
sceCdCLOCK time;
sceCdReadClock(&time);
btoi(time.year), btoi(time.month & 0x7F), btoi(time.day)   // OSDHistory.c:121
```

`-lcdvd` is already linked (`Makefile:129`), `sceCdCLOCK` is already used in
`opl.c:1283`, and `OSDHistory.c` shows the BCD conversion *and* the `& 0x7F`
mask on the month byte. The prompt's question 1(a) is answered by existing code.

**Settings toggle: exact template available.** `gEnableWrite` runs the full
path — `diaSetInt(diaConfig, CFG_ENWRITEOP, ...)` at `gui.c:529`, `diaGetInt`
at `gui.c:553`, `configGetInt`/`configSetInt` in `opl.c`, checked at
`menusys.c:89`. A new boolean follows it line for line.

**The one real problem: `attribute=Clock` will not tick.**

```c
// themes.c:243 — drawAttributeText
if (mutableText->currentConfigId != config->uid) {
    mutableText->currentValue = NULL;
    configGetStr(config, mutableText->value, &mutableText->currentValue);
}
```

Attribute text is cached against the *game's* config uid and only re-read when
the selection changes. A `Clock` attribute would render once and then freeze
until you moved the cursor. The prompt's requirement 3 cannot be met as written.

Two ways out:

1. **New element type** (`ClockText`) with its own `drawElem` that formats the
   RTC directly. This is the well-trodden path — patches 02 and 03 in this repo
   add `RecentImage`, `RecentText`, `MenuTabs` and `GameCountText` exactly this
   way. Themes write `type=ClockText` instead of `attribute=Clock`.
2. **Cache bypass** for a small set of reserved dynamic attribute names, keeping
   the prompt's `attribute=` syntax. Smaller theme-side change, slightly uglier
   engine-side.

I would recommend 1 and tell you the syntax differs from the prompt.

**Unverified:** timezone. `osd_config.h` is already included (`opl.h:31`,
`system.c:27`) but I could not check which timezone helpers that ps2sdk exposes
— Docker was not running. Worth ten minutes before committing to an approach.

### Goal B: play stats

**CFG round-trip is safe — verified.** `configWrite` (`config.c:541`) opens with
`O_TRUNC` and rewrites the whole in-memory list in original order. Since
`configRead` parses every `key=value` into that list including unknown ones, the
append-don't-clobber rule holds by construction. Your `Genre`/`Description` keys
survive, and so would `Playtime`.

One wrinkle the prompt does not anticipate: `configWrite` **skips keys beginning
with `#`** (`config.c:551`). That is deliberate — `#Size`, `#Media`, `#Format`
are computed per boot. Do not name any persisted stat with a leading `#`.

**The power-off case is a design decision, not a blocker.** The prompt is right
that it needs an explicit answer. I would propose a sanity cap plus a log line:
any session longer than some threshold is discarded rather than recorded,
because a wrong number is worse than a missing one for a stat you cannot audit.
Whether OPL can distinguish IGR relaunch from cold boot is worth checking in
`syshook.c` / `iopmgr.c` — I did not verify it.

---

## 4. Prompt 2 — Freeze overlay

The most ambitious of the three, and the one I would not start first.

**What already exists** (all confirmed above): the combo detection, on a vblank
interrupt, inside retail games. Per-frame display-register rewriting via GSM.
In-game framebuffer capture via IGS. A resident payload with room to grow.

**What does not exist, and is the whole project:** every one of those precedents
either *observes* the game or *resets* it. None of them **suspends** it. IGR's
path is "detect combo → tear down → reset." Converting that into "detect combo →
drain DMA → hold the EE thread → composite → resume byte-perfect" is new work
with no in-tree template.

The specific unknowns the prompt correctly identifies, and which I cannot answer
from a read:

- Whether the EE thread can be held at a drained vsync boundary and resumed with
  interrupt masks and DMA channel state intact, across arbitrary game engines.
- Whether a VRAM region can be stolen and restored byte-perfect on titles that
  fill VRAM.
- Whether the resident region survives games that scan or clobber memory.

**Realistic expectation:** this will work on some games and not others, and
which is which cannot be predicted from source — only from testing. The prompt's
phased structure (PCSX2 freeze prototype before any menu) is the right shape and
should be followed strictly. If Phase 1 cannot hold and resume three different
engines cleanly, the honest move is to stop there.

The quick-switch half (Phase 3) is much easier than the freeze half, and depends
entirely on it.

---

## 5. Prompt 3 — Achievement telemetry

### Phase 1 is decoupled and should be done regardless

Server-side — PINE from PCSX2, rcheevos evaluation, SQLite, RA API, FTP
writeback — touches no console code and carries no hardware risk. It is ordinary
Python. It is also the part that proves whether the *idea* is fun before anyone
spends a month on residency.

Note the prompt's own caveat is the real risk here, and it is not technical: your
library is NTSC-J and RA sets are hash-linked, frequently US-only. The
manual-mapping override is not a nice-to-have, it is the main path.

### Phase 2 — the good news and the catch

**Good news: both halves exist.** `cheat_engine.S` samples EE RAM in-game.
`modules/network/smap-ingame` and `ingame_smstcpip` are a working in-game
network stack.

**The catch, and it is precise.** In-game modules are chosen by device mode:

```c
// system.c:472-485
else if (!strcmp(mode_str, "ETH_MODE"))  modules |= CORE_IRX_ETH | CORE_IRX_SMB;
else if (!strcmp(mode_str, "MMCE_MODE")) modules |= CORE_IRX_MMCE;
```

Your games load from MMCE. **The in-game network modules are never loaded for
your setup.** This is exactly the prompt's Phase 0 question 1, and the answer is
that the machinery exists but is wired to a mode you do not use.

**But the precedent for fixing it is right there:**

```c
// system.c:850-854
#ifdef __DECI2_DEBUG
    modules |= CORE_IRX_DECI2 | CORE_IRX_ETH;
#elif defined(__INGAME_DEBUG)
#ifdef TTY_UDP
    modules |= CORE_IRX_ETH;
#endif
```

The debug builds already add `CORE_IRX_ETH` **alongside** whatever device mode is
in use — in-game networking coexisting with a non-ETH device driver is something
this tree already does. That is a large de-risking of the prompt's hardest
question, and it means the first hardware experiment is cheap: build with
`TTY_UDP`, confirm packets leave the console while a game runs from MMCE, and
the residency question is answered before any sampler is written.

What remains genuinely unknown is IOP RAM pressure with both the MMCE driver and
the network stack resident, and which games survive it.

---

## 6. Cross-cutting notes

**All three prompts share a hard rule this codebase makes easy.**
Append-don't-clobber is satisfied by `configWrite`'s design, not by care at the
call site. Worth knowing so nobody writes defensive merge code that is not
needed.

**All three want a Settings toggle defaulting to Off.** `gEnableWrite` is the
template; the pattern is four edits (menu item, config key, load, save) and I
have followed it before in this repo.

**Playtime has two prospective writers.** Prompt 1 writes it from RTC deltas,
prompt 3 writes it from telemetry session length, and the overlay proposes a
third from vsync ticks. Both prompts flag the double-counting risk and ask for a
scheme. If more than one of these gets built, decide the authority order once,
up front, and write it down — not per-feature.

**Nothing here conflicts with the theme work.** The clock wants a new element
type, which is additive. The achievements server wants to write CFG keys the
theme already knows how to render.

---

## 7. Recommendation

**Do the screenshot thing first.** `IGS=1` is a build flag on code that is
already written. You added `_SCR` and `_SCR2` slots to the info page today and
have nothing to put in them. This is the shortest path from where you are to
something visibly better, and it costs a rebuild.

**Then the clock.** One day, low risk, and it exercises the element-adding path
again on something small.

**Then achievements Phase 1**, because it is the only one whose value can be
tested without touching the console, and because a `TTY_UDP` build answers the
scariest hardware question for the price of one compile.

**Leave the overlay last** and treat its Phase 1 as a genuine go/no-go. It is
the only one of the three that might simply not work, and it is the one whose
failure would be most expensive to discover late.
