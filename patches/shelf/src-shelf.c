/*
  SHELF — sidebar shell (Phase 4).

  The slide reuses the shape of OPL's screen crossfade (gui.c, Maximus32's):
  a counter advanced once per frame, run through an easing curve. Fewer frames
  here — a panel that only moves should settle faster than a whole screen
  changing, and 12 at 60Hz is 200ms, about the floor before a slide reads as
  sluggish.

  Nothing is drawn from a texture, so the panel costs no VRAM.
*/

#include <stdio.h>
#include <string.h>

#include "include/opl.h"
#include "include/renderman.h"
#include "include/fntsys.h"
#include "include/pad.h"
#include "include/shelf.h"
#include "include/ioman.h"

int gEnableShelfUI;

#define SHELF_WIDTH   218
#define SHELF_FRAMES   12

/* How long LEFT must be held at the edge before the panel opens.
 *
 * LEFT is not inert at the left edge in either layout -- in a grid it wraps to
 * the last page (menuPrevItem -> menuLastPage) and in a list it steps to the
 * previous device (menuPrevH). So the gesture cannot simply claim the press:
 * it withholds it, and gui.c replays the navigation if the hold is released
 * early. 12 frames is 200ms, comfortably longer than a tap and short enough
 * not to feel like a wait. Two frames, as first sketched, is 33ms and would
 * have fired on nearly every deliberate tap.
 */
#define SHELF_HOLD_FRAMES 12

static enum ShelfState state;
static int frame;          /* 0..SHELF_FRAMES, position within the slide */
static int selected;
static int leftHeld;       /* frames LEFT has been held while at the edge */
static int leftPending;    /* a LEFT press is being withheld */

/* No init function: these are statics, so the zero state is already CLOSED at
 * frame 0, and everything else is reset when the panel opens. */

static const char *items[] = {"Home", "Library", "Apps", "Settings"};
#define SHELF_ITEMS (sizeof(items) / sizeof(items[0]))

/** Smoothstep. The screen fade uses its own curve; this one only has to make a
 *  slide stop rather than halt, and smoothstep is the cheapest thing that does. */
static float shelfEase(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

void shelfUpdate(void)
{
    switch (state) {
        case SHELF_OPENING:
            if (++frame >= SHELF_FRAMES) {
                frame = SHELF_FRAMES;
                state = SHELF_OPEN;
            }
            break;
        case SHELF_CLOSING:
            if (--frame <= 0) {
                frame = 0;
                state = SHELF_CLOSED;
            }
            break;
        default:
            break;
    }
}

int shelfHasInput(void)
{
    /* OPENING and CLOSING included deliberately: a press landing mid-animation
     * must not reach the list underneath, and cancel must work before the panel
     * has finished arriving. */
    return state != SHELF_CLOSED;
}

/** True on the frame a withheld LEFT is released without completing the hold.
 *  The caller replays the navigation it suppressed, so a tap still wraps or
 *  pages exactly as it did before the panel existed. */
int shelfWasWithheld(void)
{
    if (leftPending && !getKeyPressed(KEY_LEFT)) {
        leftPending = 0;
        leftHeld = 0;
        return 1;
    }
    return 0;
}

int shelfTrigger(int atLeftEdge)
{
    if (state != SHELF_CLOSED)
        return 0;

    /* L3: works anywhere, including screens with no meaningful left edge. */
    if (getKeyOn(KEY_L3)) {
        state = SHELF_OPENING;
        selected = 0;
        leftHeld = 0;
        leftPending = 0;
        return 1;
    }

    if (!atLeftEdge || !getKeyPressed(KEY_LEFT)) {
        /* Released before the hold completed. gui.c replays the navigation. */
        leftHeld = 0;
        return 0;
    }

    leftPending = 1;
    if (++leftHeld >= SHELF_HOLD_FRAMES) {
        state = SHELF_OPENING;
        selected = 0;
        leftHeld = 0;
        leftPending = 0;
        return 1;
    }

    /* Withhold the press while the hold is still in question. */
    return 1;
}

void shelfHandleInput(void)
{
    if (state == SHELF_OPENING || state == SHELF_CLOSING)
        return;                     /* swallow, but do not act, mid-slide */

    if (getKeyOn(KEY_UP) && selected > 0)
        selected--;
    else if (getKeyOn(KEY_DOWN) && selected < (int)SHELF_ITEMS - 1)
        selected++;
    else if (getKeyOn(KEY_L3) || getKeyOn(gSelectButton == KEY_CIRCLE ? KEY_CROSS : KEY_CIRCLE))
        state = SHELF_CLOSING;      /* cancel */
    /* Item activation lands in the next phase, with the page stubs. */
}

void shelfDraw(void)
{
    float t;
    int x, i;

    if (state == SHELF_CLOSED)
        return;

    t = shelfEase((float)frame / (float)SHELF_FRAMES);
    x = (int)(-SHELF_WIDTH + t * SHELF_WIDTH);

    /* Dim what is behind, in step with the slide. */
    rmDrawRect(0, 0, 640, 480, GS_SETREG_RGBAQ(0x00, 0x00, 0x00, (int)(t * 0x50), 0x00));

    rmDrawRect(x, 0, SHELF_WIDTH, 480, GS_SETREG_RGBAQ(0x14, 0x17, 0x1C, 0x80, 0x00));
    rmDrawLine(x + SHELF_WIDTH, 0, x + SHELF_WIDTH, 480,
               GS_SETREG_RGBAQ(0x3C, 0x44, 0x4E, 0x80, 0x00));

    fntRenderString(FNT_DEFAULT, x + 24, 28, ALIGN_NONE, 0, 0, "SHELF",
                    GS_SETREG_RGBAQ(0xF2, 0xF5, 0xF8, 0x80, 0x00));

    for (i = 0; i < (int)SHELF_ITEMS; i++) {
        int iy = 84 + i * 34;

        if (i == selected)
            rmDrawRect(x, iy - 8, SHELF_WIDTH, 30,
                       GS_SETREG_RGBAQ(0x22, 0x27, 0x2F, 0x80, 0x00));

        fntRenderString(FNT_DEFAULT, x + 34, iy, ALIGN_NONE, 0, 0, items[i],
                        i == selected ? GS_SETREG_RGBAQ(0xFF, 0xFF, 0xFF, 0x80, 0x00)
                                      : GS_SETREG_RGBAQ(0x88, 0x94, 0xA2, 0x80, 0x00));
    }
}
