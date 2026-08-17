# SHELF — Phase 7: Home

What you were last doing, and what you have been doing. Both from data OPL
already keeps, so nothing here invents a store of its own.

## Sources

| shown | from |
|---|---|
| recent list, newest first | `oplRecent*` — `conf_last.cfg`, max 8, **global** rather than per-device |
| minutes per game | `Playtime` in the game's CFG, written by patch 08 on return |
| library totals | one pass over the active list's CFGs |

The library pass **is** the index scan Phase 0 anticipated and Phase 8 reuses
for `Group=`/`Label=`. It runs once per list, keyed on the support object and
the item count — the same self-healing shape the Library cache uses, and for the
same reason: an invalidation hook is something that has to be remembered from
every path that rebuilds. 28 files of roughly 150 bytes is nothing to read once
and unaffordable to read sixty times a second.

## Layout

```
0..196    hero — the highlighted recent entry
78        CONTINUE  (or RECENTLY PLAYED when it is not the newest)
100       title
128       "4 h 12 m played"
240       six recent tiles, the theme's cell geometry
410       "9 h 20 m across 4 of 28 titles"
450..480  footer, the theme's own
```

The eyebrow changes with position on purpose. **Continue** means the thing you
were doing; the second tile along is not that, and calling it Continue would be
a small lie repeated every time you moved.

## The recent list is global; art is not

`oplRecent*` outlives any one device, but art resolves through the *current*
device's ART folder, and `itemLaunch` needs a list index. So a game last played
from another device shows its title, falls back for its picture, and cannot be
launched from here — `homeIndexOf` returns -1 and Cross does nothing.

That is an ordinary outcome rather than a fault, and it is why the launch path
looks a game up by startup id rather than trusting the recent index to mean
anything on the device now selected.

## Totals say what they measure

"9 h 20 m across 4 of 28 titles" rather than "9 h 20 m". The bare figure reads
as a library statistic when it is really the sum of four games; stating the
denominator is the difference between a number and a claim.

Games with no `Playtime` key are counted in the denominator and not the
numerator, so the ratio answers "how much of this have I actually played".

## What to judge on hardware

- The eyebrow flipping between CONTINUE and RECENTLY PLAYED as you move.
- A game played from another device: title present, art fallen back, Cross inert.
- Whether the totals line matches what the CFGs say — it is a fresh scan, and
  wrong arithmetic there would be silent.
- That the scan does not stall the first frame on entry. If it does, it wants
  moving off the render path the way the font slot was.
