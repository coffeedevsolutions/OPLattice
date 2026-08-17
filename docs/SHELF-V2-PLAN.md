# SHELF — the second hardware pass

Everything below came off one hardware session. Ordered by what it costs to be
wrong about, not by how annoying it is: the input convention first, because it
is the only item that makes the shell *feel* broken rather than look unfinished.

Three of these turn out to share a single root cause. That is written up once,
under **A**, and referenced rather than repeated.

---

## A. One root cause, three symptoms — text alignment

**Symptoms reported:** the landing page's terminal column drifts out toward the
middle on some lines; the Library and Apps button labels are not centred in their
buttons; the L3 badge's text sits off its badge.

**Cause.** `fntCalcDimensions` returns a width in **physical** pixels — it sums
glyph advances from the rasteriser, which works at `ws = 0.75`. Every layout
number in `shelf.c` is **virtual**. Mixing them is wrong on its own, but the
reason the error *varies per string* is `fntRenderString`:

```c
x = rmScaleX(x);                       /* virtual -> physical, FIRST */
if (aligned & ALIGN_HCENTER) x -= fntCalcDimensions(id, string) >> 1;
```

It scales `x` and *then* subtracts the physical width. Hand-computing
`RIGHT - fntCalcDimensions(...)` in virtual space and passing that in gets the
subtraction scaled too, so the right edge lands at

    X_SCALE(RIGHT) - X_SCALE(w) + w    instead of    X_SCALE(RIGHT)

an error of `w x (X_SCALE_factor - 1)` — proportional to the string's own
length, which is exactly "some lines float out into the middle". It is invisible
when `iDisplayWidth == 640` and appears the moment overscan compensation moves
it, which is why the previewer looks right and the console does not.

**Fix.** Stop hand-computing. Pass `ALIGN_RIGHT` / `ALIGN_HCENTER` and let
`fntRenderString` do it after scaling, which is already correct. Audit every
`fntCalcDimensions` call used for **placement** rather than measurement:

- landing stream (`RIGHT - w`)  — right-align
- Apps status bar's right cluster (three of them, chained through `rx`)
- Library's "N of M" count
- the Apps VRAM debug line
- Home's Library/Apps button labels — centre
- the L3 badge's label — centre
- the clock's colon and minutes, which chain off `fntCalcDimensions(hh)`
- `shelfHint`'s returned width, which drives every footer row's pen

The chained ones (status bar, hint rows) need the pen kept in **one** space.
Cleanest is to lay those rows out right-to-left with `ALIGN_RIGHT` per item and
a virtual gap between, rather than accumulating measured widths at all.

**Also check while in here:** `fntCalcDimensions` is used for the Home clock's
colon offset, so the colon spacing has the same drift.

---

## B. Input convention — X and O are used both ways

**Reported:** the shell mixes X-selects/O-backs with the console's own
O-selects/X-backs. This is a JP console, so OPL's menus use **O to select, X to
go back**.

**Cause.** OPL keeps the user's convention in `gSelectButton`
(`KEY_CIRCLE` on a JP machine). The shell uses it in some places and raw
`KEY_CROSS` in others — the landing's "Cross is the same as Down", Home's launch
and Details bindings, Library's and Apps's launch, and the panel's own confirm.

**Fix.** One rule, applied everywhere: `gSelectButton` confirms,
`gSelectButton == KEY_CIRCLE ? KEY_CROSS : KEY_CIRCLE` cancels. No raw
`KEY_CROSS` or `KEY_CIRCLE` left in an input path. The footer hint marks must
follow the same source, so the drawn glyph matches the button that works —
`shelfHint`'s `kind` should take "confirm"/"cancel" rather than a shape.

This is the item most worth doing first: it is the difference between a shell
that feels wrong and one that feels finished.

---

## C. Hero scrims are bands, not gradients

**Reported:** the dark wash over the hero art on Home and Library reads as thick
strips of flat colour.

**Cause.** Both scrims are hand-rolled as **12 bands of 11 pixels** with alpha
stepping by 7 — a visible 4% jump every 11 rows. `shelfGradV` was fixed to draw
2px bands during the first pass and these two were never moved onto it.

**Fix.** Route both through `shelfGradV`. Worth checking the step is fine enough
at the hero's height: 132px over 2px bands is 66 steps, which at alpha 0x00-0x48
is under 2 levels per band and should be invisible.

---

## D. Hero art is soft

**Reported:** the hero backgrounds are not high quality despite the palettising
work.

**Cause.** Not the palettising — the **source is smaller than the box**. `BG` is
generated at 418x180 (`tools/palettize-art.py`), and the Library hero draws it at
`640 - SHELF_RAIL_W` = **612 wide x 196**, `SCALING_NONE`. That is a 1.46x
upscale on the GS, which is bilinear and soft. Home's hero is ~378 wide, so it is
close to 1:1 and should already look better — worth confirming that asymmetry on
screen, because it tells us the diagnosis is right.

**Fix.** A dedicated pattern rather than resizing `BG`, which the classic theme
also consumes at its own size. Add `HERO` at 612x196, generated from the same
truecolor bank, palettised the same way. One resident at a time plus a row of
prefetch, so the VRAM cost is one texture — measure it against the Library page's
budget, which is already the tightest of the four.

---

## E. Home's recent strip — first tile

**Reported:** the leftmost game under Continue Playing cannot be hovered.

**Found.** A genuine off-by-one in the label, at minimum:

```c
int idx = i + 1;                                   /* recent index */
int on  = (homeFocus == 1 && idx == homeSel);      /* frame: correct */
...
i == homeSel ? LAND_TEXT : LAND_MUTE               /* label: WRONG, uses i */
```

The frame compares the recent index, the label compares the loop position, so
tile 0's label can never light — `homeSel` is clamped to `>= 1`. Whether the tile
is genuinely unselectable or merely *looks* it needs one check on hardware; the
label bug is real either way and may be the whole of it.

---

## F. Library is not alphabetical

**Cause.** The Library page mirrors `menuGetActiveList()`, so it inherits
whatever sort OPL is set to, and OPL's default is not A-Z.

**Fix.** Decide whether the page sorts for itself or follows the console. Given
this page now has an **alphabet scale** down its left edge, following the console
is not really an option: the scale is a lie in any other order, and I already
flagged it as "a ruler, not an index". Sort A-Z within the page, and then the
marker can track the selected title's actual initial instead of its row — which
is what the scale looks like it is promising.

---

## G. Motion

- **Remove the idle float.** `shelfFloat` bobs every card by a pixel or two on a
  7-second cycle. It reads as drift rather than life. Delete it; keep the focus
  lift, which is doing a job.
- **Landing to Home is too slow.** `HOME_SCROLL_FRAMES` is 22 (~370ms). Take it
  to 10-12 (~170-200ms). The easing curve stays; only the duration changes.

---

## H. The clock is wrong — and probably not ours

**Reported:** starts at 00:00 every boot and counts up.

**Assessment.** `homeLocalTime` reads `sceCdReadClock` and corrects JST by
`configGetTimezone()`. Starting at zero and counting is the signature of a PS2
whose **RTC backup battery is flat** — the console loses the time at power-off
and the counter restarts. No software can fix that.

**One check settles it:** boot to the console's own OSD and look at its clock and
date. If the OSD is also wrong, it is the CR2032 on the motherboard.

**Fix either way.** Detect the case and stop presenting a confident wrong time:
if the RTC reads a date before 2001 (before the console existed), treat the clock
as unset and show the panel without a time rather than "00:14". A wrong clock
stated plainly is fine; a wrong clock stated confidently is not.

---

## I. Apps page is empty

**Cause found.** `appsCount()` calls `appGetObject(1)` — `initOnly` — which
returns `NULL` unless `appItemList.enabled` is set, and only `appInit()` sets it.
`appInit` runs from `initSupport(appGetObject(0), APP_MODE, ...)` in `opl.c`,
i.e. as part of the device sweep. So either the APPS device is disabled in OPL's
settings, or the sweep has not reached it by the time the page is first drawn —
newly relevant, because Home is now the boot screen and we no longer pass through
the device menu on the way in.

**Fix.** Confirm which by checking whether APPS appears as a device on the
classic screen. If it is disabled, the page should say so rather than showing an
empty grid. If it is a timing problem, the page should ask through
`appGetObject(0)` and cope with the list arriving a few frames later — the same
self-healing shape `libSync()` already uses for the game list.

---

## J. The L3 badge text

Covered by **A** — the label is centred with a physical width against a virtual
badge. Fix with `ALIGN_HCENTER` and re-measure on hardware, since the rail is
only 28 wide and there is little room for the error.

---

## Order of work

1. **B** input convention — the only one that makes the shell feel broken
2. **A** the alignment audit — one cause, three visible symptoms, plus J
3. **G** motion — trivial, and immediately felt
4. **C** scrims — trivial, and the most visible of the drawing faults
5. **E** and **F** — the two correctness items on Home and Library
6. **D** hero art — needs a new pattern, a regeneration pass and a VRAM measurement
7. **H** and **I** — both start with a check on the console rather than a change

Items 1-5 are one sitting. 6 is an art-pipeline change. 7 is diagnosis first.

## Verify before shipping

The previewer cannot show **A** at all, because its `iDisplayWidth` is always
640 and the drift is zero there. That is worth stating plainly: the tool that
exists to catch this class of bug is blind to this particular one, and the fix
has to be checked on the console. Everything else on the list is visible in the
previewer and should be confirmed there first.
