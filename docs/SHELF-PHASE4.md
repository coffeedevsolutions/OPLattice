# SHELF — Phase 4 design: sidebar shell + page routing

Design only. The trigger is left open deliberately — options are set out in §2
with what each costs, for you to choose before implementation.

---

## 1. What already exists

**Screens** are `guiSwitchScreen(int)` over `GUI_SCREEN_MAIN / MENU / INFO /
GAME_MENU / APP_MENU` (`gui.h:56`). Adding SHELF pages means adding ids and
handlers to the same table, not building a parallel router.

**Transitions** are already frame-based: `gui.c:1553` runs a 26-frame crossfade
(`transition_frames`, Maximus32's) driven by a `transIndex` counter and an
easing function. The sidebar's slide is the same shape — a counter advanced per
frame, an eased interpolation, drawn last. No new machinery.

**Input** is polled centrally in `guiReadPads` (`gui.c:1526`) and dispatched by
each screen handler. A sidebar intercept sits between the poll and the dispatch.

---

## 2. Trigger — four options, your choice

Every button on the main screen is already claimed. Counted from `menusys.c`:

| button | current use |
|---|---|
| CIRCLE / CROSS | select / back |
| UP / DOWN / LEFT / RIGHT | navigate |
| START | main menu |
| L1 / R1 | page, or device paging in a grid theme *(patch 02/03)* |
| L2 / R2 | page |
| TRIANGLE | game options |
| SQUARE | info screen |
| SELECT | refresh device list |
| R3 | cycle sort *(patch 02)* |

**L3 is the only completely unbound button in the tree.** Grepping `KEY_L3`
across `src/` returns nothing.

### Option A — L3

Free, unambiguous, one line to bind, no collision to reason about. Costs you the
only spare button. On an 8BitDo, L3 is a stick click — reliable but not
discoverable, and easy to hit accidentally while navigating with the stick.

### Option B — tap SELECT at the grid's left edge

Contextual: SELECT keeps refreshing the list everywhere except when the cursor
is already at column 0, where it opens the sidebar instead. Discoverable if the
grid hints it, and it mirrors the mockup's own "◄ at edge Sidebar" footer text.
Costs a behavioural exception that has to be explained in the hint bar, and
makes SELECT's meaning positional.

### Option C — LEFT at the grid's left edge

The most natural gesture — pushing further left off the edge of a grid is how
every console UI opens a left-hand panel, and the mockup's footer already says
"◄ at edge Sidebar". Nothing is lost, because LEFT at column 0 is currently a
no-op. Costs nothing except that a fast scroller may open it unintentionally,
which a short repeat-suppression window handles.

### Option D — hold SELECT

Tap keeps refreshing, hold opens the panel. No positional exception, no button
spent. Costs a hold timer and a slight input latency on the tap path.

**My recommendation is C, with A as the fallback binding.** C is the gesture the
mockup already advertises and it consumes nothing; A is there for when the
cursor is not in a grid — the Apps page, or a device with no games — where "left
edge" has no meaning. Binding both is a few lines more than binding either.

---

## 3. Panel state machine

Four states, advanced once per frame in the render loop:

```
CLOSED  --trigger-->  OPENING  --(t reaches 1)-->  OPEN
OPEN    --cancel -->  CLOSING  --(t reaches 0)-->  CLOSED
```

`t` is a 0..1 progress value stepped by `1/frames` per frame and passed through
the same easing the screen fade uses. Panel x is `-width + ease(t) * width`.

**12 frames** each way. The existing fade is 26 for a full screen change; a
panel that only slides is expected to feel faster, and 12 at 60 Hz is 200 ms,
which is the usual floor before a slide reads as sluggish.

Input is swallowed entirely while not CLOSED. In OPENING and CLOSING that
prevents double-triggering mid-animation; in OPEN the panel owns UP/DOWN,
CROSS/CIRCLE and the cancel button.

## 4. Drawing

Drawn **last**, after the active screen handler has rendered, so it composites
over whatever page is beneath without that page knowing it exists. Uses the
existing quad and font paths (`rmDrawRect`, `fntRenderString`) — no new
primitives, no new textures, so nothing to budget in VRAM.

Styling hardcoded, per the brief. **Theme-format integration is explicitly not
built here** and is noted as future work: the panel would eventually want to be
a `theme_element_t` chain of its own, but inventing that vocabulary before there
are three pages to style with it is the wrong order.

## 5. Routing

| item | target |
|---|---|
| Home | `GUI_SCREEN_SHELF_HOME` — stub |
| Library | `GUI_SCREEN_SHELF_LIBRARY` — stub |
| Apps | `GUI_SCREEN_SHELF_APPS` — stub |
| Settings | `GUI_SCREEN_MENU` — **existing screen, unchanged** |

Stubs render their name and a "not built yet" line. They exist so Phases 5–7
have somewhere to land and so routing can be tested before any page exists.

## 6. Status footer

Online state and free space, both of which OPL already holds — no new queries.
Read at panel-open rather than per frame; neither changes on a timescale that
matters and a free-space stat is not worth a filesystem call every frame.

## 7. Master toggle

`SHELF UI: On/Off`, persisted as `shelf_ui`, **default Off**.

Off must be byte-for-byte stock, which means the gate has to sit at the input
intercept and the draw call, not inside them:

```c
if (gEnableShelfUI && shelfTriggerPressed()) ...
if (gEnableShelfUI) shelfDrawPanel();
```

With it off there is no trigger check, no panel state advanced, no draw, and no
page stubs reachable. The classic device-paged list stays default and reachable
regardless.

Follows the same four-edit pattern as every other toggle: `dialogs.h` id,
`dialogs.c` row, `gui.c` load/store, `opl.c` config key and default.

---

## 8. What I would build, in order

1. Toggle plumbing, defaulted off — so everything after it is already gated.
2. State machine and draw, no items — verify the slide on hardware first, since
   animation smoothness is the one thing that cannot be judged in the previewer.
3. Items and routing to stubs.
4. Status footer.

Each step is independently visible, and step 2 is the one that could disappoint:
if a 12-frame eased slide does not feel right at 60 Hz, better to find
that before four items and a footer are riding on it.

## 9. Risks

**Input swallowing is the likeliest bug.** A panel that opens but does not
release input is a hang from the user's point of view. The cancel path needs to
work from every state, including OPENING — I would allow cancel during OPENING
rather than waiting for OPEN.

**The trigger may fight the grid.** Whichever option you pick, the interaction
with L1/R1 device paging wants checking on hardware: patches 02 and 03 gave the
main screen a two-axis grid, and edge detection has to agree with what the grid
thinks its edges are.

**Nothing here is verifiable in the previewer.** It renders themes, not OPL's
screen system. Phase 4 is the first phase that is hardware-only, which makes the
staged order in §8 worth more than usual.
