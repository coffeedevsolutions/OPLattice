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
#include <stdlib.h>

#include "include/opl.h"
#include "include/renderman.h"
#include "include/fntsys.h"
#include "include/pad.h"
#include "include/shelf.h"
#include "include/appsupport.h"
#include "include/menusys.h"
#include "include/texcache.h"
#include "include/config.h"
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
static void shelfHoldCron(void);

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
    if (state != SHELF_CLOSED || guiOnShelfPage())
        shelfHoldCron();

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
    shelfHoldCron();
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


/** Input for any SHELF page while the sidebar is closed.
 *
 * The pages have no content yet, so the only things to honour are the sidebar
 * trigger and a way back. Circle returns to the classic list, so a stub can
 * never be a dead end -- that matters more than it sounds, because these are
 * reachable on hardware before they do anything.
 */
void shelfHandleInputPage(void)
{
    shelfHoldCron();
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

/* ------------------------------------------------------------------ Apps page

   A card grid over the same list the classic Apps screen shows. Nothing here
   enumerates or launches anything itself: the list comes from appGetList() and
   X calls the support object's own itemLaunch, so with SHELF UI off there is no
   second launch path that could have drifted from the first.

   Six cards, three by two. Nine would fit at 112 tall, but the icon would drop
   to about 56px with no air under the name; an app list is usually short and
   paging beats shrinking. Geometry matches the previewer's SHELF Apps tab,
   where the card widths were checked against real glyph advances.
*/

/* fntRenderString's y is the TOP of the glyph box, not the baseline
   (textBaselineOffset adds size-2 for ALIGN_NONE). A 17px string in a 40px bar
   therefore starts at (40-17)/2, not at 27 -- which hung it 4px below the bar. */
#define HDR_H       40
#define HDR_TEXT_Y  ((HDR_H - 17) / 2)
#define FTR_TEXT_Y  452

#define APPS_COLS   3
#define APPS_PER    (APPS_COLS * 2)
#define APPS_MARGIN 32
#define APPS_GAP    16
#define APPS_CW     ((640 - 2 * APPS_MARGIN - (APPS_COLS - 1) * APPS_GAP) / APPS_COLS)
#define APPS_CH     170
#define APPS_Y0     64

/* The PS2 face buttons, drawn. The theme has cross.png and circle.png but the
   SHELF pages are hardcoded and load no art, and spelling them out ("Cross
   Launch") reads as a debug string rather than a control hint. Two primitives
   each, in the buttons' own colours. Returns the width consumed so the caller
   can lay a row out without measuring twice. */
static int shelfHint(int x, int y, int kind, const char *label)
{
    int cy = y + 8, w;
    u64 col = (kind == 0) ? GS_SETREG_RGBA(0x6B, 0x99, 0xE8, 0x80)   /* cross  */
            : (kind == 2) ? GS_SETREG_RGBA(0xD9, 0x6F, 0xBF, 0x80)   /* square */
                          : GS_SETREG_RGBA(0xE8, 0x55, 0x6B, 0x80);  /* circle */
    if (kind == 2) {
        rmDrawRect(x, cy - 5, 11, 2, col);
        rmDrawRect(x, cy + 4, 11, 2, col);
        rmDrawRect(x, cy - 5, 2, 11, col);
        rmDrawRect(x + 9, cy - 5, 2, 11, col);
    } else if (kind == 0) {
        /* Two bars crossed. Cheaper and crisper at this size than a glyph. */
        int i;
        for (i = 0; i < 11; i++) {
            rmDrawRect(x + i, cy - 5 + i, 2, 2, col);
            rmDrawRect(x + 10 - i, cy - 5 + i, 2, 2, col);
        }
    } else {
        int i;
        for (i = 0; i < 12; i++) {
            int t = (i < 3 || i > 8) ? 3 : 2;
            rmDrawRect(x + (i < 3 ? 3 : (i > 8 ? 3 : 0)), cy - 6 + i, t, 2, col);
            rmDrawRect(x + 11 - (i < 3 ? 3 : (i > 8 ? 3 : 0)) - t, cy - 6 + i, t, 2, col);
        }
        rmDrawRect(x + 3, cy - 6, 6, 2, col);
        rmDrawRect(x + 3, cy + 4, 6, 2, col);
    }
    fntRenderString(FNT_DEFAULT, x + 18, y, ALIGN_NONE, 0, 0, label,
                    GS_SETREG_RGBA(0x5C, 0x66, 0x74, 0x80));
    w = 18 + fntCalcDimensions(FNT_DEFAULT, label);
    return w + 22;
}

/* Hold OPL's auto-start countdown while the shelf owns the screen.
 *
 * The counter is cancelled in getKey() -- pad.c:369 -- and the shelf reads input
 * exclusively through getKeyOn(), which does not touch it. Worse, the launch it
 * counts down to fires from menuHandleInputMain (menusys.c:1238), and that never
 * runs while a shelf page is up. So the display counted 5, 4, 3 ... and then
 * straight past zero into negatives with nothing to stop it.
 *
 * Anyone pressing buttons in the shelf is demonstrably present, which is the
 * whole question the countdown exists to ask. */
static void shelfHoldCron(void)
{
    KeyPressedOnce = 1;
    DisableCron = 1;
}

static int appsSel;
static int appsFontSmall;

/* FNT_DEFAULT is a single size, so a subtitle at the same size is not a
   subtitle. A NULL path makes fntLoadSlot fall back to the embedded face
   (fntsys.c:249), so this costs no file and no art, and it survives theme
   switches because fntRelease is only ever called with a theme's own ids
   (themes.c:1724). */
/* Called once from opl.c after fntInit, never from a render path.
   fntLoadFile builds a FreeType face and takes the font semaphore; doing that
   from inside a frame is both a long stall and a lock the renderer may already
   hold. It also retried every frame whenever it failed, because FNT_DEFAULT is
   0 and the guard was `<= 0`. */
void shelfInitFonts(void)
{
    int id = fntLoadFile(NULL, 12);
    appsFontSmall = (id == FNT_ERROR) ? FNT_DEFAULT : id;
}

static int appsCount(void)
{
    item_list_t *list = appGetObject(1);
    return (list && list->itemGetCount) ? list->itemGetCount(list) : 0;
}

/* Centred, and truncated with an ellipsis if it will not fit. Measuring is the
   whole point: a name that silently runs past its card is the failure this page
   exists to avoid. */
static void appsCentred(int font, int cx, int y, const char *s, int maxw, u64 colour)
{
    char buf[APP_TITLE_MAX + 8];
    int w = fntCalcDimensions(font, s);

    if (w > maxw) {
        int n = (int)strlen(s);
        while (n > 1) {
            snprintf(buf, sizeof(buf), "%.*s\xe2\x80\xa6", n, s);
            if (fntCalcDimensions(font, buf) <= maxw)
                break;
            n--;
        }
        s = buf;
        w = fntCalcDimensions(font, s);
    }
    fntRenderString(font, cx - w / 2, y, ALIGN_NONE, 0, 0, s, colour);
}

static void appsStatusBar(void)
{
    int rx = 608, w;

    rmDrawRect(0, 0, 640, 40, GS_SETREG_RGBA(0x18, 0x1C, 0x22, 0x80));
    rmDrawRect(0, 40, 640, 1, GS_SETREG_RGBA(0x2A, 0x30, 0x38, 0x80));
    fntRenderString(FNT_DEFAULT, 32, HDR_TEXT_Y, ALIGN_NONE, 0, 0, "Apps",
                    GS_SETREG_RGBA(0xF2, 0xF5, 0xF8, 0x80));

    /* Placeholder until Phase 8 binds it. sceCdReadClock is available and
       already used at OSDHistory.c:122, but its fields are BCD and its RTC runs
       on JST, so an honest clock needs an offset this phase cannot configure. */
    w = fntCalcDimensions(FNT_DEFAULT, "--:--");
    fntRenderString(FNT_DEFAULT, rx - w, HDR_TEXT_Y, ALIGN_NONE, 0, 0, "--:--",
                    GS_SETREG_RGBA(0x5C, 0x66, 0x74, 0x80));
    rx -= w + 22;

    /* Free space has no query for this device class. Nothing in bdmsupport,
       mmcesupport or ethsupport reports capacity; the only capacity call in the
       tree is HDIOC_TOTALSECTOR for the internal HDD, which is total and not
       free. The slot is real, the value is not, and inventing one is worse. */
    w = fntCalcDimensions(FNT_DEFAULT, "\xe2\x80\x94 free");
    fntRenderString(FNT_DEFAULT, rx - w, HDR_TEXT_Y, ALIGN_NONE, 0, 0, "\xe2\x80\x94 free",
                    GS_SETREG_RGBA(0x5C, 0x66, 0x74, 0x80));
    rx -= w + 22;

    {
        const char *net = (gNetworkStartup == 0) ? "NET" : "OFF";
        u64 col = (gNetworkStartup == 0) ? GS_SETREG_RGBA(0x64, 0xC8, 0x78, 0x80)
                                         : GS_SETREG_RGBA(0x6E, 0x76, 0x81, 0x80);
        w = fntCalcDimensions(FNT_DEFAULT, net);
        fntRenderString(FNT_DEFAULT, rx - w, HDR_TEXT_Y, ALIGN_NONE, 0, 0, net, col);
        rmDrawRect(rx - w - 14, 17, 8, 8, col);
    }
}

void shelfRenderApps(void)
{
    const app_info_t *apps = appGetList();
    int total = appsCount();
    int page, first, i;

    if (appsSel >= total)
        appsSel = total - 1;
    if (appsSel < 0)
        appsSel = 0;

    page = (total > 0) ? appsSel / APPS_PER : 0;
    first = page * APPS_PER;

    rmDrawRect(0, 0, 640, 480, GS_SETREG_RGBA(0x0F, 0x12, 0x16, 0x80));
    appsStatusBar();

    if (total <= 0) {
        /* An empty state, not a grid of nothing with a selection index pointing
           at an item that does not exist. */
        fntRenderString(FNT_DEFAULT, 32, 120, ALIGN_NONE, 0, 0,
                        "No applications found.",
                        GS_SETREG_RGBA(0xF2, 0xF5, 0xF8, 0x80));
        fntRenderString(appsFontSmall, 32, 148, ALIGN_NONE, 0, 0,
                        "Put an ELF and a title.cfg under APPS/ on a device OPL can see.",
                        GS_SETREG_RGBA(0x5C, 0x66, 0x74, 0x80));
    }

    for (i = 0; i < APPS_PER && first + i < total; i++) {
        int idx = first + i;
        int cx = APPS_MARGIN + (i % APPS_COLS) * (APPS_CW + APPS_GAP);
        int cy = APPS_Y0 + (i / APPS_COLS) * (APPS_CH + APPS_GAP);
        int on = (idx == appsSel);
        int ix = cx + (APPS_CW - 88) / 2;
        char initial[2];

        rmDrawRect(cx, cy, APPS_CW, APPS_CH,
                   on ? GS_SETREG_RGBA(0x26, 0x2C, 0x35, 0x80)
                      : GS_SETREG_RGBA(0x1C, 0x20, 0x27, 0x80));
        if (on) {
            u64 e = GS_SETREG_RGBA(0xF2, 0xF5, 0xF8, 0x80);
            rmDrawRect(cx, cy, APPS_CW, 2, e);
            rmDrawRect(cx, cy + APPS_CH - 2, APPS_CW, 2, e);
            rmDrawRect(cx, cy, 2, APPS_CH, e);
            rmDrawRect(cx + APPS_CW - 2, cy, 2, APPS_CH, e);
        } else {
            u64 e = GS_SETREG_RGBA(0x2A, 0x30, 0x38, 0x80);
            rmDrawRect(cx, cy, APPS_CW, 1, e);
            rmDrawRect(cx, cy + APPS_CH - 1, APPS_CW, 1, e);
        }

        /* Icon placeholder. Real icons are Phase 6's business: six textures is a
           new simultaneous working set on a page that currently costs nothing in
           VRAM, and the prefetch wrapper and VRAM debug line that would let
           anyone size that budget honestly do not exist yet. */
        rmDrawRect(ix, cy + 20, 88, 88, GS_SETREG_RGBA(0x2E, 0x35, 0x3F, 0x80));
        if (!apps)
            continue;

        initial[0] = apps[idx].title[0];
        initial[1] = '\0';
        appsCentred(FNT_DEFAULT, cx + APPS_CW / 2, cy + 73, initial,
                    APPS_CW - 16, GS_SETREG_RGBA(0x5C, 0x66, 0x74, 0x80));
        appsCentred(FNT_DEFAULT, cx + APPS_CW / 2, cy + 134, apps[idx].title,
                    APPS_CW - 16, GS_SETREG_RGBA(0xF2, 0xF5, 0xF8, 0x80));
        /* Legacy entries have no sidecar and so get no line at all rather than
           an invented one: conf_apps.cfg is Name=path, with no third field. */
        if (apps[idx].subtitle[0])
            appsCentred(appsFontSmall, cx + APPS_CW / 2, cy + 154,
                        apps[idx].subtitle, APPS_CW - 16,
                        GS_SETREG_RGBA(0x5C, 0x66, 0x74, 0x80));
    }

    rmDrawRect(APPS_MARGIN, 438, 640 - 2 * APPS_MARGIN, 1,
               GS_SETREG_RGBA(0x2A, 0x30, 0x38, 0x80));
    {
        int hx = APPS_MARGIN;
        hx += shelfHint(hx, FTR_TEXT_Y, 0, "Launch");
        shelfHint(hx, FTR_TEXT_Y, 1, "Back");
    }
    /* The acceptance test made visible. Deliberately labelled with a tilde:
       this counts what was asked for this frame and cannot see gsKit's
       evictions, because its block list is file-scope in gsTexManager.c and
       reachable only through an internal symbol. Useful for "is this page
       anywhere near the pool", useless for "how full is VRAM". */
    /* Right-aligned and stacked above the hints rather than centred: centring it
       put it straight through the control row. */
    {
        char v[48];
        int w, rx = 640 - APPS_MARGIN;
        if (total > APPS_PER) {
            int pages = (total + APPS_PER - 1) / APPS_PER;
            snprintf(v, sizeof(v), "%d / %d", page + 1, pages);
            w = fntCalcDimensions(FNT_DEFAULT, v);
            fntRenderString(FNT_DEFAULT, rx - w, FTR_TEXT_Y, ALIGN_NONE, 0, 0, v,
                            GS_SETREG_RGBA(0x5C, 0x66, 0x74, 0x80));
        }
        snprintf(v, sizeof(v), "~%u KB / %d binds", rmVramBoundBytes() >> 10,
                 rmVramBoundCount());
        w = fntCalcDimensions(appsFontSmall, v);
        fntRenderString(appsFontSmall, rx - w, 424, ALIGN_NONE, 0, 0, v,
                        GS_SETREG_RGBA(0x3C, 0x44, 0x4E, 0x80));
    }
}

void shelfHandleInputApps(void)
{
    int total = appsCount();

    shelfHoldCron();
    /* Reachable from the pages it routes to, or the only way back into the
       panel is out to the main list and in again. atLeftEdge is 0: these pages
       have no meaningful left edge, so L3 is the trigger. */
    if (shelfHasInput()) {
        shelfHandleInput();
        return;
    }
    if (shelfTrigger(0))
        return;


    if (getKeyOn(KEY_CIRCLE)) {
        guiSwitchScreen(GUI_SCREEN_MAIN);
        return;
    }
    if (total <= 0)
        return;

    if (getKeyOn(KEY_LEFT) && appsSel > 0)
        appsSel--;
    else if (getKeyOn(KEY_RIGHT) && appsSel < total - 1)
        appsSel++;
    else if (getKeyOn(KEY_UP) && appsSel >= APPS_COLS)
        appsSel -= APPS_COLS;
    else if (getKeyOn(KEY_DOWN) && appsSel + APPS_COLS < total)
        appsSel += APPS_COLS;
    else if (getKeyOn(KEY_CROSS)) {
        /* The support object's own launch, with the config it builds itself.
           Anything else would be a second launch path to keep in step with the
           classic screen, which is exactly what the master toggle promises not
           to have. */
        item_list_t *list = appGetObject(1);
        if (list && list->itemLaunch && list->itemGetConfig)
            list->itemLaunch(list, appsSel, list->itemGetConfig(list, appsSel));
    }
}


/* --------------------------------------------------------------- Library page

   The same list the main screen is showing, as a cover grid. It does not
   enumerate anything itself -- menuGetActiveList() hands back the support object
   the classic screen selected -- so the two can never disagree about what is
   installed, and switching device on the main screen switches this page too.

   Geometry follows the rule the art correction established: a GameImage is
   drawn at three quarters of its declared width in 16:9, and for a 2:3 cover to
   *display* as 2:3 the drawn texels must be 1:2. Declaring 96x144 gives 72x144
   drawn in 16:9 and 96x144 in 4:3, and both display 2:3. Column pitch is
   computed with rmWidthScaled so the layout agrees with what rmSetupQuad will
   actually do rather than with the number written here.
*/

/* The grid is the theme's, mirrored. thm_GridHard's ItemsList is columns=6,
   cell_width=88, cell_height=136, gap=14, label_height=11, frame=3, and
   drawItemsListGrid derives the art box from those as cellWidth-gap by
   cellHeight-gap-labelHeight. These are duplicated rather than read from gTheme
   because the shelf pages are hardcoded by design -- Phase 4 deliberately did
   not build theme-format integration -- but they must stay in step with
   conf_theme.cfg, and a difference will show as tiles that do not line up with
   the classic grid.

   One and a half rows: a full row with the next half-visible, so it reads as
   scrollable rather than as everything there is. */
#define LIB_COLS      6
#define LIB_CELL_W    88
#define LIB_CELL_H    136
#define LIB_GAP       14
#define LIB_LABEL_H   11
#define LIB_FRAME     3
#define LIB_ART_W     (LIB_CELL_W - LIB_GAP)             /* 74 declared */
#define LIB_ART_H     (LIB_CELL_H - LIB_GAP - LIB_LABEL_H) /* 111 */
#define LIB_PER       LIB_COLS
#define LIB_HERO_H    196
#define LIB_GRID_Y    240
/* The theme's own footer: botbar is a 30px strip at y=-30, and HintText sits at
   y=-26 in font2 (12px) #8894A2. Matched rather than invented, so the shelf and
   the screen the console boots into agree about where the bottom of the page is. */
#define LIB_FTR_Y     450
#define LIB_FTR_TEXT  454

static image_cache_t *libCache;
static image_cache_t *libHeroCache;
static int *libHeroId, *libHeroUid;
static int libHeroLast = -1;
static int *libCacheId, *libCacheUid;
static item_list_t *libList;          /* what the arrays were sized against */
static int libCount, libSel;

/* Metadata for the highlighted title only, and cached against the index it was
   read for. itemGetConfig reads the game's CFG off the device, so calling it
   every frame would be a filesystem hit per frame for a line of text. */
static int libMetaIdx = -1;
static char libMetaA[96];    /* Genre, Release, Developer          */
static char libMetaB[96];    /* #Media, #Format, Rating, #Size     */
static char libMetaDesc[192];
static char libMetaName[128];

/* Join present values with the theme's separator, skipping absent ones so they
   take their bullet with them -- the same rule AttributeList follows, because
   this is meant to read as the same page. */
static void libJoin(char *out, size_t n, config_set_t *cfg, const char **keys, int count)
{
    int i, used = 0;
    out[0] = '\0';
    for (i = 0; i < count; i++) {
        const char *v = NULL;
        if (!configGetStr(cfg, keys[i], &v) || !v || !*v)
            continue;
        if (!strcmp(keys[i], CONFIG_ITEM_SIZE))
            used += snprintf(out + used, n - used, used ? "  \xc2\xb7  %s MiB" : "%s MiB", v);
        else
            used += snprintf(out + used, n - used, used ? "  \xc2\xb7  %s" : "%s", v);
        if (used >= (int)n)
            break;
    }
}

/* Exactly the fields the theme's info page carries, read once per selection --
   itemGetConfig reads the game's CFG off the device, so per frame would be a
   filesystem hit for a line of text. */
static void libReadMeta(int idx)
{
    static const char *rowA[] = {"Genre", "Release", "Developer"};
    static const char *rowB[] = {CONFIG_ITEM_MEDIA, CONFIG_ITEM_FORMAT, "Rating", CONFIG_ITEM_SIZE};
    config_set_t *cfg;
    const char *v = NULL;

    if (idx == libMetaIdx)
        return;
    libMetaIdx = idx;
    libMetaA[0] = libMetaB[0] = libMetaDesc[0] = libMetaName[0] = '\0';
    if (!libList || !libList->itemGetConfig)
        return;
    cfg = libList->itemGetConfig(libList, idx);
    if (!cfg)
        return;

    if (configGetStr(cfg, CONFIG_ITEM_NAME, &v) && v)
        snprintf(libMetaName, sizeof(libMetaName), "%s", v);
    libJoin(libMetaA, sizeof(libMetaA), cfg, rowA, 3);
    libJoin(libMetaB, sizeof(libMetaB), cfg, rowB, 4);
    v = NULL;
    if (configGetStr(cfg, "Description", &v) && v)
        snprintf(libMetaDesc, sizeof(libMetaDesc), "%s", v);
}

static int libSync(void)
{
    item_list_t *list = menuGetActiveList();
    int count = (list && list->itemGetCount) ? list->itemGetCount(list) : 0;

    /* A NULL list happens transiently while devices are being (re)selected.
       Treating it as a change made libSync free and malloc on alternating
       frames -- called twice a frame, from render and from input -- which is
       heap thrash rather than a cache. Hold the previous arrays instead. */
    if (!list)
        return libCount;

    if (list != libList || count != libCount) {
        free(libCacheId);
        free(libCacheUid);
        free(libHeroId);
        free(libHeroUid);
        libCacheId = libCacheUid = libHeroId = libHeroUid = NULL;
        if (count > 0) {
            libCacheId = malloc(count * sizeof(int));
            libCacheUid = malloc(count * sizeof(int));
            libHeroId = malloc(count * sizeof(int));
            libHeroUid = malloc(count * sizeof(int));
            if (libCacheId && libCacheUid && libHeroId && libHeroUid) {
                memset(libCacheId, -1, count * sizeof(int));
                memset(libCacheUid, -1, count * sizeof(int));
                memset(libHeroId, -1, count * sizeof(int));
                memset(libHeroUid, -1, count * sizeof(int));
            } else {
                free(libCacheId); free(libCacheUid);
                free(libHeroId); free(libHeroUid);
                libCacheId = libCacheUid = libHeroId = libHeroUid = NULL;
                count = 0;
            }
        }
        libList = list;
        libCount = count;
        libMetaIdx = -1;
        if (libSel >= count)
            libSel = count > 0 ? count - 1 : 0;
    }

    /* One hero at a time, so a cache of two: the selected game's, and room for
       the one being moved to before the old one is dropped. */
    if (!libHeroCache && count > 0)
        libHeroCache = cacheInitCache(1, "ART", 1, "BG", 2);

    /* Allocated on first use, so with SHELF UI off nothing is ever built. */
    if (!libCache && count > 0)
        /* Suffix, not filename fragment: mmceGetImage builds "%s%s/%s_%s" and
           texDiscoverLoad appends ".png", so passing "_COV" asked for
           SERIAL__COV.png and nothing ever loaded. */
        libCache = cacheInitCache(0, "ART", 1, "COV", LIB_PER + LIB_COLS);

    return count;
}

/* The selected game's key art, full screen and heavily scrimmed.
 *
 * The BG art is 418x180 and displays 3.09:1; the screen is 16:9. Fitting it
 * honestly would need a 276-tall band, which is 57% of the page and leaves no
 * room for a grid. Stretching it instead is the right trade *because* of the
 * scrim: at roughly a quarter brightness behind a dark wash the aspect error
 * reads as atmosphere rather than distortion, which is what this image is for
 * here. It is not being presented as the artwork -- the details page does that,
 * at native size.
 */
static GSTEXTURE *libHero(int idx)
{
    char *startup;
    if (!libHeroCache || !libHeroId || !libList || !libList->itemGetStartup)
        return NULL;
    /* -2 is cacheGetTexture's "no such art, never ask again". Right for a cover;
       wrong for the hero, which is one request against a dozen covers and so the
       most likely to lose a race with a busy device -- and one lost race would
       blank the banner for the session. Retry when the selection returns. */
    if (libHeroId[idx] == -2 && idx != libHeroLast)
        libHeroId[idx] = -1;
    libHeroLast = idx;
    startup = libList->itemGetStartup(libList, idx);
    if (!startup)
        return NULL;
    return cacheGetTexture(libHeroCache, libList, &libHeroId[idx], &libHeroUid[idx], startup);
}

static GSTEXTURE *libCover(int idx)
{
    char *startup;
    if (!libCache || !libCacheId || !libList || !libList->itemGetStartup)
        return NULL;
    startup = libList->itemGetStartup(libList, idx);
    if (!startup)
        return NULL;
    return cacheGetTexture(libCache, libList, &libCacheId[idx], &libCacheUid[idx], startup);
}

void shelfRenderLibrary(void)
{
    int total = libSync();
    int pitchX = rmWideScale(LIB_CELL_W);
    int drawnW = rmWideScale(LIB_ART_W);
    int gridW  = pitchX * LIB_COLS;
    int x0     = (640 - gridW) / 2;
    int first, i, n;
    char buf[80];

    if (total > 0) {
        if (libSel >= total) libSel = total - 1;
        if (libSel < 0)      libSel = 0;
        libReadMeta(libSel);
    }
    /* The selected row is the top row, so the half row beneath is always the
       next one along -- the peek shows where you are going, not where you were. */
    first = (total > 0) ? (libSel / LIB_COLS) * LIB_COLS : 0;

    rmDrawRect(0, 0, 640, 480, GS_SETREG_RGBA(0x0A, 0x0C, 0x0F, 0x80));

    /* Hero: the highlighted game, not the last played. */
    if (total > 0) {
        GSTEXTURE *hero = libHero(libSel);
        if (hero)
            rmDrawPixmap(hero, 0, 0, ALIGN_NONE, 640, LIB_HERO_H, SCALING_NONE,
                         gDefaultCol);
        else
            rmDrawRect(0, 0, 640, LIB_HERO_H, GS_SETREG_RGBA(0x14, 0x17, 0x1C, 0x80));
        for (i = 0; i < 12; i++)
            rmDrawRect(0, LIB_HERO_H - 132 + i * 11, 640, 11,
                       GS_SETREG_RGBA(0x0A, 0x0C, 0x0F, 4 + i * 7));
    }

    /* The theme's own detail fields, in the theme's order: name, the two
       attribute strips, then the description. */
    if (total > 0) {
        const char *name = libMetaName[0] ? libMetaName
                         : (libList && libList->itemGetName
                            ? libList->itemGetName(libList, libSel) : NULL);
        if (name)
            fntRenderString(FNT_DEFAULT, 32, 100, ALIGN_NONE, 0, 0, name,
                            GS_SETREG_RGBA(0xF2, 0xF5, 0xF8, 0x80));
        /* One line, not four. The full set belongs on the details page, which
           Square now opens; repeating it here only crowded the picture. */
        if (libMetaA[0])
            fntRenderString(appsFontSmall, 32, 128, ALIGN_NONE, 0, 0, libMetaA,
                            GS_SETREG_RGBA(0x88, 0x94, 0xA2, 0x80));
    }

    if (total <= 0) {
        fntRenderString(FNT_DEFAULT, 32, 120, ALIGN_NONE, 0, 0,
                        "Nothing to show yet.",
                        GS_SETREG_RGBA(0xF2, 0xF5, 0xF8, 0x80));
        fntRenderString(appsFontSmall, 32, 148, ALIGN_NONE, 0, 0,
                        "This page follows the device the main list is on. Pick one there first.",
                        GS_SETREG_RGBA(0x5C, 0x66, 0x74, 0x80));
    }

    /* Hero first: one request against a row of covers, and asking last put it
       behind a queue they had just filled. */
    if (total > 0)
        rmPrefetchTexture(libHero(libSel));
    for (i = LIB_PER; i < LIB_PER + LIB_COLS && first + i < total; i++)
        rmPrefetchTexture(libCover(first + i));

    /* Two rows drawn, the second clipped by the grid box to half a cell. */
    for (n = 0; n < LIB_COLS * 2 && first + n < total; n++) {
        int idx = first + n;
        int cx = x0 + (n % LIB_COLS) * pitchX;
        int cy = LIB_GRID_Y + (n / LIB_COLS) * LIB_CELL_H;
        /* Full size, always. Passing a shorter height to rmDrawPixmap does not
           clip -- it scales -- so the half row came out squashed rather than cut.
           The bottom row is drawn whole and the footer, drawn afterwards, covers
           whatever runs past it, which is what "continues off the page" means. */
        GSTEXTURE *cov = libCover(idx);

        if (cy >= LIB_FTR_Y)
            break;

        if (cov)
            rmDrawPixmap(cov, cx, cy, ALIGN_NONE, LIB_ART_W, LIB_ART_H,
                         SCALING_RATIO, gDefaultCol);
        else
            rmDrawRect(cx, cy, drawnW, LIB_ART_H,
                       GS_SETREG_RGBA(0x14, 0x17, 0x1C, 0x80));

        if (idx == libSel) {
            u64 e = GS_SETREG_RGBA(0xF2, 0xF5, 0xF8, 0x80);
            rmDrawRect(cx, cy, drawnW, LIB_FRAME, e);
            rmDrawRect(cx, cy + LIB_ART_H - LIB_FRAME, drawnW, LIB_FRAME, e);
            rmDrawRect(cx, cy, LIB_FRAME, LIB_ART_H, e);
            rmDrawRect(cx + drawnW - LIB_FRAME, cy, LIB_FRAME, LIB_ART_H, e);
        }

        if (cy + LIB_ART_H + LIB_LABEL_H <= LIB_FTR_Y && libList && libList->itemGetName) {
            char *t = libList->itemGetName(libList, idx);
            if (t)
                fntRenderString(appsFontSmall, cx, cy + LIB_ART_H, ALIGN_NONE,
                                drawnW, LIB_LABEL_H, t,
                                idx == libSel ? GS_SETREG_RGBA(0xF2, 0xF5, 0xF8, 0x80)
                                              : GS_SETREG_RGBA(0x88, 0x94, 0xA2, 0x80));
        }
    }

    /* Drawn last, so the bottom row of tiles runs under it. */
    rmDrawRect(0, LIB_FTR_Y, 640, 480 - LIB_FTR_Y, GS_SETREG_RGBA(0x14, 0x17, 0x1C, 0x80));
    {
        int hx = 35, w, rx = 605;
        hx += shelfHint(hx, LIB_FTR_TEXT, 0, "Play");
        hx += shelfHint(hx, LIB_FTR_TEXT, 2, "Details");
        shelfHint(hx, LIB_FTR_TEXT, 1, "Back");
        if (total > 0) {
            snprintf(buf, sizeof(buf), "%d of %d", libSel + 1, total);
            w = fntCalcDimensions(appsFontSmall, buf);
            /* Right-aligned, and only drawn if the hints have not reached it --
               overlapping is worse than omitting a count you can infer. */
            if (rx - w > hx + 8)
                fntRenderString(appsFontSmall, rx - w, LIB_FTR_TEXT, ALIGN_NONE, 0, 0,
                                buf, GS_SETREG_RGBA(0x88, 0x94, 0xA2, 0x80));
        }
    }
}

void shelfHandleInputLibrary(void)
{
    int total = libSync();

    shelfHoldCron();
    /* Reachable from the pages it routes to, or the only way back into the
       panel is out to the main list and in again. atLeftEdge is 0: these pages
       have no meaningful left edge, so L3 is the trigger. */
    if (shelfHasInput()) {
        shelfHandleInput();
        return;
    }
    if (shelfTrigger(0))
        return;


    if (getKeyOn(KEY_CIRCLE)) {
        guiSwitchScreen(GUI_SCREEN_MAIN);
        return;
    }
    if (total <= 0)
        return;

    if (getKeyOn(KEY_LEFT) && libSel > 0)
        libSel--;
    else if (getKeyOn(KEY_RIGHT) && libSel < total - 1)
        libSel++;
    /* One row, so up and down page: there is no row above or below to reach. */
    /* A row at a time, which is also what scrolls the grid. */
    else if (getKeyOn(KEY_UP) && libSel >= LIB_COLS)
        libSel -= LIB_COLS;
    else if (getKeyOn(KEY_DOWN))
        libSel = (libSel + LIB_COLS < total) ? libSel + LIB_COLS : total - 1;
    else if (getKeyOn(KEY_SQUARE)) {
        /* The theme's info page, not a second rendering of it. menuSelectIndex
           hands the selection to the classic screen, which owns that layout. */
        if (menuSelectIndex(libSel))
            guiSwitchScreen(GUI_SCREEN_INFO);
    } else if (getKeyOn(KEY_CROSS) && libList && libList->itemLaunch && libList->itemGetConfig)
        libList->itemLaunch(libList, libSel, libList->itemGetConfig(libList, libSel));
}

/* ------------------------------------------------------------------ Home page

   Built to the mockup, and honest about the parts that have no data behind
   them. Everything shown here comes from something OPL already records:

     clock          sceCdReadClock, corrected by the console's own OSD timezone
     Playtime       minutes, written by patch 08 on return from a session
     LastPlayed     "DD-MM-YYYY", written by patch 08 on launch
     PlayCount      launches, same
     recent order   oplRecent*, conf_last.cfg, global rather than per-device
     network        gNetworkStartup
     version        OPL_VERSION, compiled in

   Four things in the mockup are NOT here, because inventing them is worse than
   omitting them: a star rating (no source -- the CFG's Rating is CERO/ESRB, a
   classification, not a score); free space (no query exists for this device
   class); "save synced" (MMCE state, no API); and captures (IGS writes BMPs to
   the memory card and nothing has produced one yet). Each is a data problem,
   not a layout one, and none is fixed by drawing a box for it.
*/

#define HOME_TILES   4
#define HOME_M       24
#define HOME_COL_W   184
#define HOME_R_X     (HOME_M + HOME_COL_W + 12)
#define HOME_R_W     (640 - HOME_R_X - HOME_M)

static image_cache_t *homeCover, *homeHero;
static int homeCovId[OPL_RECENT_MAX], homeCovUid[OPL_RECENT_MAX];
static int homeHeroId[OPL_RECENT_MAX], homeHeroUid[OPL_RECENT_MAX];
static int homeSel;

/* Library-wide figures, scanned once per list. This is the index pass Phase 0
   anticipated and Phase 8 reuses for Group=/Label=. Keyed on the support object
   and the item count, so it re-runs on a device change and never otherwise. */
#define HOME_TOP_N 4
static item_list_t *homeScanList;
static int homeScanCount, homeTotalMinutes, homeTotalTitles, homeTotalPlayed;
static char homeTopName[HOME_TOP_N][40];
static int  homeTopMins[HOME_TOP_N];

static int homeDaysFromCivil(int y, int m, int d)
{
    int era, yoe, doy, doe;
    y -= (m <= 2);
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/** Local time from the RTC. The clock runs on JST whatever the console's
 *  region, and configGetTimezone() is the offset from GMT the owner already set
 *  in the OSD -- so no new setting is needed, and a wrong clock is the console's
 *  own to fix. Returns 0 if the RTC is unreadable. */
static int homeLocalTime(int *hh, int *mm, int *days)
{
    sceCdCLOCK c;
    int minutes;

    if (!sceCdReadClock(&c))
        return 0;
    *days = homeDaysFromCivil(2000 + btoi(c.year), btoi(c.month & 0x7F), btoi(c.day));
    minutes = btoi(c.hour) * 60 + btoi(c.minute) - 540 + configGetTimezone();
    while (minutes < 0)     { minutes += 1440; (*days)--; }
    while (minutes >= 1440) { minutes -= 1440; (*days)++; }
    *hh = minutes / 60;
    *mm = minutes % 60;
    return 1;
}

/** "yesterday", "3d ago", "today" -- day granularity, which is all the stored
 *  DD-MM-YYYY supports. Empty when the key is absent or unparseable. */
static void homeWhen(char *out, size_t n, config_set_t *cfg, int todayDays)
{
    const char *v = NULL;
    int d, m, y, ago;

    out[0] = '\0';
    if (!cfg || !configGetStr(cfg, "LastPlayed", &v) || !v)
        return;
    if (sscanf(v, "%d-%d-%d", &d, &m, &y) != 3 || m < 1 || m > 12 || d < 1 || d > 31)
        return;
    ago = todayDays - homeDaysFromCivil(y, m, d);
    if (ago < 0)       snprintf(out, n, "today");
    else if (ago == 0) snprintf(out, n, "today");
    else if (ago == 1) snprintf(out, n, "yesterday");
    else               snprintf(out, n, "%dd ago", ago);
}

static void homeFormatTime(char *out, size_t n, int minutes)
{
    if (minutes <= 0)        out[0] = '\0';
    else if (minutes < 60)   snprintf(out, n, "%d m", minutes);
    else                     snprintf(out, n, "%d h", minutes / 60);
}

static void homeScan(void)
{
    item_list_t *list = menuGetActiveList();
    int count = (list && list->itemGetCount) ? list->itemGetCount(list) : 0;
    int i, k;

    if (!list || (list == homeScanList && count == homeScanCount))
        return;
    homeScanList = list;
    homeScanCount = count;
    homeTotalMinutes = homeTotalTitles = homeTotalPlayed = 0;
    for (k = 0; k < HOME_TOP_N; k++) { homeTopMins[k] = 0; homeTopName[k][0] = '\0'; }
    if (!list->itemGetConfig || !list->itemGetName)
        return;

    for (i = 0; i < count; i++) {
        config_set_t *cfg = list->itemGetConfig(list, i);
        int mins = 0;
        homeTotalTitles++;
        if (!cfg)
            continue;
        configGetInt(cfg, "Playtime", &mins);
        if (mins <= 0)
            continue;
        homeTotalMinutes += mins;
        homeTotalPlayed++;
        /* Insertion into a four-deep ranking; nothing here needs a sort. */
        for (k = 0; k < HOME_TOP_N; k++) {
            if (mins > homeTopMins[k]) {
                int j;
                char *nm = list->itemGetName(list, i);
                for (j = HOME_TOP_N - 1; j > k; j--) {
                    homeTopMins[j] = homeTopMins[j - 1];
                    strncpy(homeTopName[j], homeTopName[j - 1], sizeof(homeTopName[0]) - 1);
                    homeTopName[j][sizeof(homeTopName[0]) - 1] = '\0';
                }
                homeTopMins[k] = mins;
                snprintf(homeTopName[k], sizeof(homeTopName[0]), "%s", nm ? nm : "");
                break;
            }
        }
    }
}

static int homeIndexOf(const char *startup)
{
    item_list_t *list = menuGetActiveList();
    int i, count;
    if (!list || !startup || !list->itemGetCount || !list->itemGetStartup)
        return -1;
    count = list->itemGetCount(list);
    for (i = 0; i < count; i++) {
        char *st = list->itemGetStartup(list, i);
        if (st && !strcmp(st, startup))
            return i;
    }
    return -1;
}

static config_set_t *homeCfgOf(int recentIdx)
{
    item_list_t *list = menuGetActiveList();
    int i = homeIndexOf(oplRecentStartup(recentIdx));
    return (i >= 0 && list && list->itemGetConfig) ? list->itemGetConfig(list, i) : NULL;
}

static GSTEXTURE *homeArt(image_cache_t *cache, int *ids, int *uids, int idx)
{
    item_list_t *list = menuGetActiveList();
    const char *st = oplRecentStartup(idx);
    if (!cache || !list || !st)
        return NULL;
    return cacheGetTexture(cache, list, &ids[idx], &uids[idx], (char *)st);
}

static void homeCard(int x, int y, int w, int h, const char *label)
{
    rmDrawRect(x, y, w, h, GS_SETREG_RGBA(0x16, 0x1A, 0x20, 0x80));
    rmDrawRect(x, y, w, 1, GS_SETREG_RGBA(0x2A, 0x30, 0x38, 0x80));
    if (label)
        fntRenderString(appsFontSmall, x + 12, y + 10, ALIGN_NONE, 0, 0, label,
                        GS_SETREG_RGBA(0x5C, 0x66, 0x74, 0x80));
}

void shelfRenderHome(void)
{
    int total = oplRecentCount();
    int hh = 0, mm = 0, days = 0, haveClock;
    int i, y;
    char buf[96], t[24], when[24];

    homeScan();
    if (total > 0) {
        if (homeSel >= total) homeSel = total - 1;
        if (homeSel < 0)      homeSel = 0;
    }
    if (!homeCover) {
        for (i = 0; i < OPL_RECENT_MAX; i++)
            homeCovId[i] = homeCovUid[i] = homeHeroId[i] = homeHeroUid[i] = -1;
        homeHero  = cacheInitCache(2, "ART", 1, "COVHD", 2);
        homeCover = cacheInitCache(3, "ART", 1, "BG", HOME_TILES + 2);
    }
    haveClock = homeLocalTime(&hh, &mm, &days);

    rmDrawRect(0, 0, 640, 480, GS_SETREG_RGBA(0x0A, 0x0C, 0x0F, 0x80));

    /* ---- header ---- */
    fntRenderString(appsFontSmall, HOME_M, 14, ALIGN_NONE, 0, 0, "HOME",
                    GS_SETREG_RGBA(0x88, 0x94, 0xA2, 0x80));
    {
        int rx = 640 - HOME_M, w;
        if (haveClock) {
            snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);
            w = fntCalcDimensions(appsFontSmall, buf);
            rmDrawRect(rx - w - 16, 8, w + 16, 20, GS_SETREG_RGBA(0x1C, 0x20, 0x27, 0x80));
            fntRenderString(appsFontSmall, rx - w - 8, 12, ALIGN_NONE, 0, 0, buf,
                            GS_SETREG_RGBA(0xF2, 0xF5, 0xF8, 0x80));
            rx -= w + 26;
        }
        {
            const char *net = (gNetworkStartup == 0) ? "NET" : "OFFLINE";
            u64 col = (gNetworkStartup == 0) ? GS_SETREG_RGBA(0x64, 0xC8, 0x78, 0x80)
                                             : GS_SETREG_RGBA(0x6E, 0x76, 0x81, 0x80);
            w = fntCalcDimensions(appsFontSmall, net);
            rmDrawRect(rx - w - 26, 8, w + 26, 20, GS_SETREG_RGBA(0x1C, 0x20, 0x27, 0x80));
            rmDrawRect(rx - w - 18, 16, 5, 5, col);
            fntRenderString(appsFontSmall, rx - w - 8, 12, ALIGN_NONE, 0, 0, net, col);
        }
    }

    /* ---- left column: clock, most played, system ---- */
    homeCard(HOME_M, 40, HOME_COL_W, 108, "CLOCK");
    if (haveClock) {
        const char *greet = hh < 5 ? "Good night" : hh < 12 ? "Good morning"
                          : hh < 18 ? "Good afternoon" : "Good evening";
        fntRenderString(appsFontSmall, HOME_M + 12, 32 + 30, ALIGN_NONE, 0, 0, greet,
                        GS_SETREG_RGBA(0xF2, 0xF5, 0xF8, 0x80));
        snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);
        fntRenderString(FNT_DEFAULT, HOME_M + 12, 82, ALIGN_NONE, 0, 0, buf,
                        GS_SETREG_RGBA(0xF2, 0xF5, 0xF8, 0x80));
    } else {
        fntRenderString(appsFontSmall, HOME_M + 12, 70, ALIGN_NONE, 0, 0,
                        "RTC unreadable", GS_SETREG_RGBA(0x5C, 0x66, 0x74, 0x80));
    }

    homeCard(HOME_M, 156, HOME_COL_W, 132, "MOST PLAYED");
    y = 182;
    for (i = 0; i < HOME_TOP_N; i++) {
        int barW;
        if (!homeTopMins[i])
            break;
        homeFormatTime(t, sizeof(t), homeTopMins[i]);
        fntRenderString(appsFontSmall, HOME_M + 12, y, ALIGN_NONE, 0, 0, homeTopName[i],
                        GS_SETREG_RGBA(0xF2, 0xF5, 0xF8, 0x80));
        {
            int w = fntCalcDimensions(appsFontSmall, t);
            fntRenderString(appsFontSmall, HOME_M + HOME_COL_W - 12 - w, y, ALIGN_NONE,
                            0, 0, t, GS_SETREG_RGBA(0x5C, 0x66, 0x74, 0x80));
        }
        /* Bars are relative to the top entry, so the shape says "how it
           compares", which is the only comparison a bar can honestly make. */
        barW = homeTopMins[0] ? (HOME_COL_W - 24) * homeTopMins[i] / homeTopMins[0] : 0;
        rmDrawRect(HOME_M + 12, y + 15, HOME_COL_W - 24, 2,
                   GS_SETREG_RGBA(0x2A, 0x30, 0x38, 0x80));
        rmDrawRect(HOME_M + 12, y + 15, barW, 2, GS_SETREG_RGBA(0x64, 0xC8, 0x78, 0x80));
        y += 28;
    }
    if (!homeTopMins[0])
        fntRenderString(appsFontSmall, HOME_M + 12, 186, ALIGN_NONE, 0, 0,
                        "No sessions recorded yet.",
                        GS_SETREG_RGBA(0x5C, 0x66, 0x74, 0x80));

    homeCard(HOME_M, 296, HOME_COL_W, 118, "SYSTEM");
    y = 322;
    homeFormatTime(t, sizeof(t), homeTotalMinutes);
    snprintf(buf, sizeof(buf), "%d of %d played", homeTotalPlayed, homeTotalTitles);
    fntRenderString(appsFontSmall, HOME_M + 12, y, ALIGN_NONE, 0, 0, buf,
                    GS_SETREG_RGBA(0x88, 0x94, 0xA2, 0x80));
    y += 20;
    if (t[0]) {
        snprintf(buf, sizeof(buf), "%s total", t);
        fntRenderString(appsFontSmall, HOME_M + 12, y, ALIGN_NONE, 0, 0, buf,
                        GS_SETREG_RGBA(0x88, 0x94, 0xA2, 0x80));
        y += 20;
    }
    fntRenderString(appsFontSmall, HOME_M + 12, y, ALIGN_NONE, 0, 0, OPL_VERSION,
                    GS_SETREG_RGBA(0x5C, 0x66, 0x74, 0x80));

    /* ---- right column: continue, recently played ---- */
    if (total <= 0) {
        homeCard(HOME_R_X, 40, HOME_R_W, 108, "CONTINUE PLAYING");
        fntRenderString(FNT_DEFAULT, HOME_R_X + 14, 76, ALIGN_NONE, 0, 0,
                        "Nothing played yet.",
                        GS_SETREG_RGBA(0xF2, 0xF5, 0xF8, 0x80));
        fntRenderString(appsFontSmall, HOME_R_X + 14, 106, ALIGN_NONE, 0, 0,
                        "Launch something and it appears here.",
                        GS_SETREG_RGBA(0x5C, 0x66, 0x74, 0x80));
    } else {
        GSTEXTURE *cov = homeArt(homeHero, homeHeroId, homeHeroUid, homeSel);
        config_set_t *cfg = homeCfgOf(homeSel);
        const char *title = oplRecentTitle(homeSel);
        int mins = 0, plays = 0;

        if (cfg) {
            configGetInt(cfg, "Playtime", &mins);
            configGetInt(cfg, "PlayCount", &plays);
        }
        homeWhen(when, sizeof(when), cfg, days);

        homeCard(HOME_R_X, 40, HOME_R_W, 108, "CONTINUE PLAYING");
        if (homeSel == 0) {
            u64 e = GS_SETREG_RGBA(0xF2, 0xF5, 0xF8, 0x80);
            rmDrawRect(HOME_R_X, 40, HOME_R_W, 2, e);
            rmDrawRect(HOME_R_X, 146, HOME_R_W, 2, e);
            rmDrawRect(HOME_R_X, 40, 2, 108, e);
            rmDrawRect(HOME_R_X + HOME_R_W - 2, 40, 2, 108, e);
        }
        if (cov)
            rmDrawPixmap(cov, HOME_R_X + HOME_R_W - 66, 52, ALIGN_NONE, 56, 84,
                         SCALING_RATIO, gDefaultCol);

        if (title)
            fntRenderString(FNT_DEFAULT, HOME_R_X + 14, 62, ALIGN_NONE,
                            HOME_R_W - 90, 24, title,
                            GS_SETREG_RGBA(0xF2, 0xF5, 0xF8, 0x80));
        homeFormatTime(t, sizeof(t), mins);
        buf[0] = '\0';
        if (when[0] && t[0])      snprintf(buf, sizeof(buf), "Last played %s  \xc2\xb7  %s total", when, t);
        else if (when[0])         snprintf(buf, sizeof(buf), "Last played %s", when);
        else if (t[0])            snprintf(buf, sizeof(buf), "%s total", t);
        if (buf[0])
            fntRenderString(appsFontSmall, HOME_R_X + 14, 92, ALIGN_NONE, 0, 0, buf,
                            GS_SETREG_RGBA(0x88, 0x94, 0xA2, 0x80));
        if (plays > 0) {
            snprintf(buf, sizeof(buf), "%d launch%s", plays, plays == 1 ? "" : "es");
            fntRenderString(appsFontSmall, HOME_R_X + 14, 110, ALIGN_NONE, 0, 0, buf,
                            GS_SETREG_RGBA(0x5C, 0x66, 0x74, 0x80));
        }
        {
            const char *lbl = homeSel == 0 ? "Resume" : "Play";
            int w = fntCalcDimensions(appsFontSmall, lbl);
            rmDrawRect(HOME_R_X + 14, 126, w + 22, 18,
                       GS_SETREG_RGBA(0x2E, 0x35, 0x3F, 0x80));
            rmDrawRect(HOME_R_X + 20, 132, 6, 6, GS_SETREG_RGBA(0x64, 0xC8, 0x78, 0x80));
            fntRenderString(appsFontSmall, HOME_R_X + 32, 128, ALIGN_NONE, 0, 0, lbl,
                            GS_SETREG_RGBA(0xF2, 0xF5, 0xF8, 0x80));
        }

        fntRenderString(appsFontSmall, HOME_R_X, 162, ALIGN_NONE, 0, 0,
                        "RECENTLY PLAYED", GS_SETREG_RGBA(0x5C, 0x66, 0x74, 0x80));
        {
            int tw = (HOME_R_W - 3 * 10) / HOME_TILES;
            int th = tw * 180 / 418;          /* the BG art's own proportions */
            for (i = 0; i < HOME_TILES && i < total; i++) {
                int cx = HOME_R_X + i * (tw + 10);
                GSTEXTURE *bg = homeArt(homeCover, homeCovId, homeCovUid, i);
                config_set_t *c2 = homeCfgOf(i);
                int m2 = 0;

                if (bg) rmDrawPixmap(bg, cx, 182, ALIGN_NONE, tw, th, SCALING_NONE, gDefaultCol);
                else    rmDrawRect(cx, 182, tw, th, GS_SETREG_RGBA(0x16, 0x1A, 0x20, 0x80));
                if (i == homeSel) {
                    u64 e = GS_SETREG_RGBA(0xF2, 0xF5, 0xF8, 0x80);
                    rmDrawRect(cx, 182, tw, 2, e);
                    rmDrawRect(cx, 182 + th - 2, tw, 2, e);
                    rmDrawRect(cx, 182, 2, th, e);
                    rmDrawRect(cx + tw - 2, 182, 2, th, e);
                }
                {
                    const char *nm = oplRecentTitle(i);
                    if (nm)
                        fntRenderString(appsFontSmall, cx, 182 + th + 6, ALIGN_NONE,
                                        tw, 12, nm,
                                        i == homeSel ? GS_SETREG_RGBA(0xF2, 0xF5, 0xF8, 0x80)
                                                     : GS_SETREG_RGBA(0x88, 0x94, 0xA2, 0x80));
                }
                if (c2) configGetInt(c2, "Playtime", &m2);
                homeWhen(when, sizeof(when), c2, days);
                homeFormatTime(t, sizeof(t), m2);
                buf[0] = '\0';
                if (when[0] && t[0]) snprintf(buf, sizeof(buf), "%s \xc2\xb7 %s", when, t);
                else if (when[0])    snprintf(buf, sizeof(buf), "%s", when);
                else if (t[0])       snprintf(buf, sizeof(buf), "%s", t);
                if (buf[0])
                    fntRenderString(appsFontSmall, cx, 182 + th + 20, ALIGN_NONE,
                                    tw, 12, buf, GS_SETREG_RGBA(0x5C, 0x66, 0x74, 0x80));
            }
        }
    }

    rmDrawRect(0, LIB_FTR_Y, 640, 480 - LIB_FTR_Y, GS_SETREG_RGBA(0x14, 0x17, 0x1C, 0x80));
    {
        int hx = 35;
        if (total > 0) {
            hx += shelfHint(hx, LIB_FTR_TEXT, 0, homeSel == 0 ? "Resume" : "Play");
            hx += shelfHint(hx, LIB_FTR_TEXT, 2, "Details");
        }
        shelfHint(hx, LIB_FTR_TEXT, 1, "Back");
    }
}

void shelfHandleInputHome(void)
{
    int total = oplRecentCount();
    int idx;

    shelfHoldCron();

    if (shelfHasInput()) {
        shelfHandleInput();
        return;
    }
    if (shelfTrigger(0))
        return;

    if (getKeyOn(KEY_CIRCLE)) {
        guiSwitchScreen(GUI_SCREEN_MAIN);
        return;
    }
    if (total <= 0)
        return;

    if (getKeyOn(KEY_LEFT) && homeSel > 0)
        homeSel--;
    else if (getKeyOn(KEY_RIGHT) && homeSel < total - 1 && homeSel < HOME_TILES - 1)
        homeSel++;
    else if (getKeyOn(KEY_SQUARE)) {
        idx = homeIndexOf(oplRecentStartup(homeSel));
        if (idx >= 0 && menuSelectIndex(idx))
            guiSwitchScreen(GUI_SCREEN_INFO);
    } else if (getKeyOn(KEY_CROSS)) {
        item_list_t *list = menuGetActiveList();
        idx = homeIndexOf(oplRecentStartup(homeSel));
        if (idx >= 0 && list && list->itemLaunch && list->itemGetConfig)
            list->itemLaunch(list, idx, list->itemGetConfig(list, idx));
    }
}
