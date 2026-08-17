/*
  SHELF — sidebar shell.

  Phase 4: the panel and its state machine only. No page stubs, no items yet;
  the animation is judged on hardware before anything rides on it.

  Everything here is inert unless gEnableShelfUI is set. The gate lives at the
  call sites in gui.c rather than inside these functions, so that with the
  toggle off there is no state advanced and no draw issued at all.
*/

#ifndef __SHELF_H
#define __SHELF_H

enum ShelfState {
    SHELF_CLOSED = 0,
    SHELF_OPENING,
    SHELF_OPEN,
    SHELF_CLOSING
};

extern int gEnableShelfUI;

/** Advance the slide by one frame. Call once per rendered frame. */
void shelfUpdate(void);

/** Draw the panel. Call last, after the active screen has rendered. */
void shelfDraw(void);
int shelfPushX(void);

/** True while the panel owns input — the caller must not dispatch to the
 *  screen handler. Covers OPENING and CLOSING as well as OPEN, so a press
 *  mid-animation cannot reach the list underneath. */
int shelfHasInput(void);

/** Handle a frame of input while the panel owns it. */
void shelfHandleInput(void);

/** Offer a trigger. Returns nonzero if the panel took it, in which case the
 *  caller must not act on the same press.
 *  atLeftEdge: the cursor cannot move further left in the current layout. */
int shelfTrigger(int atLeftEdge);

/** True when a LEFT press at the edge is being withheld pending a hold.
 *  The caller replays the suppressed navigation if this returns to 0 without
 *  the panel opening. */
int shelfWasWithheld(void);

void shelfRenderHome(void);
void shelfRenderLibrary(void);
void shelfRenderApps(void);
void shelfHandleInputPage(void);
void shelfHandleInputApps(void);
void shelfHandleInputLibrary(void);
void shelfHandleInputHome(void);

/** Allocate the small font slot. Call once at startup, after fntInit and
 *  never from a render path -- fntLoadFile builds a FreeType face and takes
 *  the font semaphore. */
void shelfInitFonts(void);

#endif
