# SHELF — Phase 5 complete: the Apps page

Design was [SHELF-PHASE5.md](SHELF-PHASE5.md); this is what shipped and what
changed on the way.

## Subtitle source: `subtitle=` in `title.cfg`

As recommended. `appScanCallback` already parses that file into a
`config_set_t`, so reading one more key is one line, and `configWrite` preserves
keys it does not recognise, so the value survives anything OPL writes and an
older OPL ignores it rather than choking.

```
APPS/<anything>/title.cfg
    title=wLaunchELF
    boot=BOOT.ELF
    subtitle=File manager
```

`subtitle` is optional and capped at 64 characters. **Legacy `conf_apps.cfg`
entries get no subtitle line at all** — that file is `Name=path` with no third
field, and "Legacy app" would be filling a slot with non-information.

## What the page does not do itself

It enumerates nothing and launches nothing. The list is `appGetList()`, the
count is the support object's `itemGetCount`, and X calls its `itemLaunch` with
the config `itemGetConfig` builds. So with `SHELF UI` off there is no second
launch path that could have drifted from the classic screen's — which is the
property the whole campaign rests on.

## The two honest gaps

**Free space** is an em-dash. Nothing in `bdmsupport.c`, `mmcesupport.c` or
`ethsupport.c` reports capacity; the only capacity call in the tree is
`HDIOC_TOTALSECTOR` for the internal HDD, and that is total, not free. The slot
is real, the value is not, and inventing one would be worse than leaving it.
Making it real means a devctl on the MMCE driver side — another codebase, and
deliberately not folded into a UI phase.

**The clock** reads `--:--`. `sceCdReadClock` is available and already used at
`OSDHistory.c:122`, so this is not blocked on anything, but its fields are BCD
and its RTC runs on JST. A visible clock that is nine hours out is worse than one
that admits it does not know yet, so the binding waits for Phase 8.

## Layout

Three by two, six visible, paged. Nine would fit at 112 tall but the icon would
drop to about 56px with no air under the name. Geometry matches the previewer's
**SHELF Apps** tab, where card widths were checked against real glyph advances
rather than guessed.

Names and subtitles ellipsize by measurement (`fntCalcDimensions`), because a
name that silently runs past its card is the failure this page exists to avoid.

Subtitles need a smaller face than `FNT_DEFAULT`, which is a single size.
`fntLoadFile(NULL, 12)` gets one from the embedded font — no file, no art — and
it survives theme switches, because `fntRelease` is only ever called with a
theme's own ids (`themes.c:1724`).

## Icons are still letter tiles

Deliberate. Six icon textures is a new simultaneous working set on a page that
currently costs nothing in VRAM, and the prefetch wrapper and the `VRAM COVERS`
debug line that would let anyone size that budget honestly are Phase 6's work.
Sizing an icon budget before that instrumentation exists is guessing.

## What to judge on hardware

- The empty state, which is what an unpopulated `APPS/` should show — not a grid
  of nothing with a selection pointing at an item that does not exist.
- D-pad movement, including that UP/DOWN cannot walk off the end of a short list.
- Paging, once there are more than six.
- **X launching identically to the classic Apps screen**, `argv1` included. If it
  does not, the master toggle stops being a clean revert.
- Circle out, from a populated page and an empty one.
