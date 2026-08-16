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
#include "include/gui.h"
#include "include/system.h"

int gEnableShelfUI;

#define SHELF_WIDTH   218
#define SHELF_PEEK      5   /* sliver left visible when closed */
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
    else if (getKeyOn(gSelectButton)) {
        static const int route[] = {GUI_SCREEN_SHELF_HOME, GUI_SCREEN_SHELF_LIBRARY,
                                    GUI_SCREEN_SHELF_APPS, GUI_SCREEN_MENU};

        /* Close first: guiSwitchScreen runs its own crossfade, and leaving the
         * panel open across it would composite over the transition. */
        state = SHELF_CLOSING;
        guiSwitchScreen(route[selected]);
    }
}

void shelfDraw(void)
{
    float t;
    int x, i;

    /* Closed still draws: a sliver at the edge is the only thing telling anyone
     * the panel exists. It also keeps the panel's right edge on screen at all
     * times, so no primitive is ever issued wholly at negative x. */
    if (state == SHELF_CLOSED) {
        if (!guiOnMainScreen())
            return;
        rmDrawRect(0, 0, SHELF_PEEK, 480, GS_SETREG_RGBA(0x14, 0x17, 0x1C, 0x70));
        rmDrawRect(SHELF_PEEK, 0, 1, 480, GS_SETREG_RGBA(0x3C, 0x44, 0x4E, 0x60));
        /* A short grabber at the vertical centre, where the eye goes looking. */
        rmDrawRect(0, 216, SHELF_PEEK + 2, 48, GS_SETREG_RGBA(0x5C, 0x66, 0x74, 0x70));
        return;
    }

    t = shelfEase((float)frame / (float)SHELF_FRAMES);
    x = (int)(-(SHELF_WIDTH - SHELF_PEEK) + t * (SHELF_WIDTH - SHELF_PEEK));

    /* Dim what is behind, in step with the slide. */
    rmDrawRect(0, 0, 640, 480, GS_SETREG_RGBA(0x00, 0x00, 0x00, (int)(t * 0x50)));

    rmDrawRect(x, 0, SHELF_WIDTH, 480, GS_SETREG_RGBA(0x14, 0x17, 0x1C, 0x80));
    /* A 1px sprite, not a line. rmDrawLine is the only LINE primitive the panel
     * would issue, and shelf.c had the only *vertical* one in the whole tree --
     * everything else in OPL draws horizontal rules. Since the white flash
     * appears exactly when the panel opens, the untrodden primitive is the first
     * thing to remove; a 1px sprite is visually identical and is the same
     * primitive as everything else here. */
    rmDrawRect(x + SHELF_WIDTH, 0, 1, 480, GS_SETREG_RGBA(0x3C, 0x44, 0x4E, 0x80));

    fntRenderString(FNT_DEFAULT, x + 24, 28, ALIGN_NONE, 0, 0, "SHELF",
                    GS_SETREG_RGBA(0xF2, 0xF5, 0xF8, 0x80));

    /* Read at draw time, but both are values OPL already holds -- no queries. */
    {
        const char *net = (gNetworkStartup == 0) ? "Online" : "Offline";

        rmDrawRect(x + 16, 438, SHELF_WIDTH - 32, 1,
                   GS_SETREG_RGBA(0x2A, 0x30, 0x38, 0x80));
        fntRenderString(FNT_DEFAULT, x + 24, 450, ALIGN_NONE, 0, 0, net,
                        GS_SETREG_RGBA(0x5C, 0x66, 0x74, 0x80));
    }

    for (i = 0; i < (int)SHELF_ITEMS; i++) {
        int iy = 84 + i * 34;

        if (i == selected)
            rmDrawRect(x, iy - 8, SHELF_WIDTH, 30,
                       GS_SETREG_RGBA(0x22, 0x27, 0x2F, 0x80));

        fntRenderString(FNT_DEFAULT, x + 34, iy, ALIGN_NONE, 0, 0, items[i],
                        i == selected ? GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80)
                                      : GS_SETREG_RGBA(0x88, 0x94, 0xA2, 0x80));
    }
}

/* ------------------------------------------------------------ page stubs */

/* Placeholders so routing can be exercised before any page exists. Phases 5-7
 * replace these bodies; the ids and the handler table entries stay. */

static void shelfRenderStub(const char *title, const char *note)
{
    rmDrawRect(0, 0, 640, 480, GS_SETREG_RGBA(0x0A, 0x0C, 0x0F, 0x80));
    fntRenderString(FNT_DEFAULT, 48, 60, ALIGN_NONE, 0, 0, title,
                    GS_SETREG_RGBA(0xF2, 0xF5, 0xF8, 0x80));
    fntRenderString(FNT_DEFAULT, 48, 104, ALIGN_NONE, 0, 0, note,
                    GS_SETREG_RGBA(0x88, 0x94, 0xA2, 0x80));
    fntRenderString(FNT_DEFAULT, 48, 430, ALIGN_NONE, 0, 0,
                    "L3 or hold LEFT for the sidebar",
                    GS_SETREG_RGBA(0x5C, 0x66, 0x74, 0x80));
}

void shelfRenderHome(void)    { shelfRenderStub("Home",    "Phase 7 fills this in."); }
void shelfRenderLibrary(void) { shelfRenderStub("Library", "Phase 6 fills this in."); }
void shelfRenderApps(void)    { shelfRenderStub("Apps",    "Phase 5 fills this in."); }

/** Input for any SHELF page while the sidebar is closed.
 *
 * The pages have no content yet, so the only things to honour are the sidebar
 * trigger and a way back. Circle returns to the classic list, so a stub can
 * never be a dead end -- that matters more than it sounds, because these are
 * reachable on hardware before they do anything.
 */
void shelfHandleInputPage(void)
{
    if (shelfHasInput()) {
        shelfHandleInput();
        return;
    }
    /* No left edge on a page that has no cursor: L3 only. */
    if (shelfTrigger(0))
        return;

    if (getKeyOn(gSelectButton == KEY_CIRCLE ? KEY_CROSS : KEY_CIRCLE))
        guiSwitchScreen(GUI_SCREEN_MAIN);
}
