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

void shelfRenderHome(void)    { shelfRenderStub("Home",    "Phase 7 fills this in."); }

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
                          : GS_SETREG_RGBA(0xE8, 0x55, 0x6B, 0x80);  /* circle */
    if (kind == 0) {
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

#define LIB_COLS    6
#define LIB_PER     LIB_COLS      /* one row: a shelf, under a hero */
#define LIB_DECL_W  96                 /* declared; 72 drawn in 16:9 */
#define LIB_COV_H   144
#define LIB_MARGIN  32
#define LIB_HERO_H  200
#define LIB_Y0      224
#define LIB_ROW_H   (LIB_COV_H + 32)

static image_cache_t *libCache;
static image_cache_t *libHeroCache;
static int *libHeroId, *libHeroUid;
static int *libCacheId, *libCacheUid;
static item_list_t *libList;          /* what the arrays were sized against */

/* Metadata for the highlighted title only, and cached against the index it was
   read for. itemGetConfig reads the game's CFG off the device, so calling it
   every frame would be a filesystem hit per frame for a line of text. */
static int libMetaIdx = -1;
static char libMetaLine[96];

static void libReadMeta(int idx)
{
    config_set_t *cfg;
    const char *genre = NULL, *year = NULL, *rating = NULL;
    int n = 0;

    if (idx == libMetaIdx)
        return;
    libMetaIdx = idx;
    libMetaLine[0] = '\0';
    if (!libList || !libList->itemGetConfig)
        return;

    cfg = libList->itemGetConfig(libList, idx);
    if (!cfg)
        return;
    configGetStr(cfg, "Genre", &genre);
    configGetStr(cfg, "Release", &year);
    configGetStr(cfg, "Rating", &rating);

    /* Absent fields take their separator with them, the same rule the details
       page follows -- no dangling bullets on a game with no year. */
    if (genre && *genre)
        n += snprintf(libMetaLine + n, sizeof(libMetaLine) - n, "%s", genre);
    if (year && *year)
        n += snprintf(libMetaLine + n, sizeof(libMetaLine) - n,
                      n ? "  \xc2\xb7  %s" : "%s", year);
    if (rating && *rating)
        snprintf(libMetaLine + n, sizeof(libMetaLine) - n,
                 n ? "  \xc2\xb7  %s" : "%s", rating);
}
static int libCount, libSel;

/* Reallocate and reset when the list changes identity or length.
 *
 * The game list is rebuilt on device refresh, and a cache id left pointing into
 * a rebuilt list is how art gets drawn against the wrong game. Keying on the
 * support pointer and the count means this heals itself rather than depending on
 * an invalidation hook somebody has to remember to call. */
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
    int drawnW = rmWidthScaled(LIB_DECL_W);
    int pitch, x0, page, first, i;
    char buf[64];


    /* Derive the pitch from the space available rather than from the cover
       width, and clamp the cover to it. Building the row out of a fixed cover
       width plus a fixed gap overflowed the screen as soon as the aspect
       setting made the cover wider -- 4:3 gives 96 virtual where 16:9 gives 72,
       and six of the former do not fit. This cannot overflow by construction. */
    pitch = (640 - 2 * LIB_MARGIN) / LIB_COLS;
    if (drawnW > pitch - 8)
        drawnW = pitch - 8;
    x0 = LIB_MARGIN + (pitch - drawnW) / 2;
    page = (total > 0) ? libSel / LIB_PER : 0;
    first = page * LIB_PER;

    rmDrawRect(0, 0, 640, 480, GS_SETREG_RGBA(0x0F, 0x12, 0x16, 0x80));
    rmDrawRect(0, 0, 640, 40, GS_SETREG_RGBA(0x18, 0x1C, 0x22, 0x80));
    rmDrawRect(0, 40, 640, 1, GS_SETREG_RGBA(0x2A, 0x30, 0x38, 0x80));
    fntRenderString(FNT_DEFAULT, 32, HDR_TEXT_Y, ALIGN_NONE, 0, 0, "Library",
                    GS_SETREG_RGBA(0xF2, 0xF5, 0xF8, 0x80));
    /* Name the device, because this page shows one and saying so is the
       difference between a scoped view and a lie by omission. */
    if (libList && libList->itemGetPrefix) {
        char *pfx = libList->itemGetPrefix(libList);
        if (pfx)
            fntRenderString(appsFontSmall, 32 + fntCalcDimensions(FNT_DEFAULT, "Library") + 12,
                            HDR_TEXT_Y + 5, ALIGN_NONE, 0, 0, pfx,
                            GS_SETREG_RGBA(0x5C, 0x66, 0x74, 0x80));
    }
    if (total > 0) {
        int w;
        snprintf(buf, sizeof(buf), "%d titles", total);
        w = fntCalcDimensions(FNT_DEFAULT, buf);
        fntRenderString(FNT_DEFAULT, 608 - w, HDR_TEXT_Y, ALIGN_NONE, 0, 0, buf,
                        GS_SETREG_RGBA(0x5C, 0x66, 0x74, 0x80));
    }

    /* The highlighted title, on the hero it belongs to. */
    if (total > 0 && libList && libList->itemGetName) {
        char *nm = libList->itemGetName(libList, libSel);
        libReadMeta(libSel);
        if (nm)
            fntRenderString(FNT_DEFAULT, 32, LIB_HERO_H - 62, ALIGN_NONE, 0, 0,
                            nm, GS_SETREG_RGBA(0xF2, 0xF5, 0xF8, 0x80));
        if (libMetaLine[0])
            fntRenderString(appsFontSmall, 32, LIB_HERO_H - 36, ALIGN_NONE, 0, 0,
                            libMetaLine, GS_SETREG_RGBA(0xB4, 0xBE, 0xC8, 0x80));
    }

    if (total <= 0) {
        fntRenderString(FNT_DEFAULT, 32, 120, ALIGN_NONE, 0, 0,
                        "Nothing to show yet.",
                        GS_SETREG_RGBA(0xF2, 0xF5, 0xF8, 0x80));
        fntRenderString(appsFontSmall, 32, 148, ALIGN_NONE, 0, 0,
                        "This page mirrors the device the main list is on. Pick one there first.",
                        GS_SETREG_RGBA(0x5C, 0x66, 0x74, 0x80));
    }

    /* One row of lookahead, no more. bind raises the use count, and gsKit's
       predictor scores by binds per frame, so a prefetched-and-undrawn texture
       looks as wanted as one that was drawn. A row is noise; a page would start
       costing residency for what is actually on screen. */
    for (i = LIB_PER; i < LIB_PER + LIB_COLS && first + i < total; i++)
        rmPrefetchTexture(libCover(first + i));

    for (i = 0; i < LIB_PER && first + i < total; i++) {
        int idx = first + i;
        int cx = x0 + (i % LIB_COLS) * pitch;
        int cy = LIB_Y0 + (i / LIB_COLS) * LIB_ROW_H;
        int on = (idx == libSel);
        GSTEXTURE *cov = libCover(idx);

        if (on) {
            u64 e = GS_SETREG_RGBA(0xF2, 0xF5, 0xF8, 0x80);
            rmDrawRect(cx - 3, cy - 3, drawnW + 6, 3, e);
            rmDrawRect(cx - 3, cy + LIB_COV_H, drawnW + 6, 3, e);
            rmDrawRect(cx - 3, cy - 3, 3, LIB_COV_H + 6, e);
            rmDrawRect(cx + drawnW, cy - 3, 3, LIB_COV_H + 6, e);
        }

        if (cov)
            /* SCALING_NONE: drawnW is already the virtual width we want, and
               re-applying the ratio here would shrink it a second time. */
            rmDrawPixmap(cov, cx, cy, ALIGN_NONE, drawnW, LIB_COV_H,
                         SCALING_NONE, gDefaultCol);
        else
            /* Still loading, or no art. The classic grid shows nothing here
               either; a flat tile at least keeps the layout readable. */
            rmDrawRect(cx, cy, drawnW, LIB_COV_H,
                       GS_SETREG_RGBA(0x1C, 0x20, 0x27, 0x80));

        /* Every tile is labelled, not just the highlighted one. A grid of
           unlabelled boxes is only readable if you already recognise the art,
           which is exactly the case where you did not need the label. */
        if (libList && libList->itemGetName) {
            char *name = libList->itemGetName(libList, idx);
            if (name)
                appsCentred(appsFontSmall, cx + drawnW / 2, cy + LIB_COV_H + 6,
                            name, pitch - 4,
                            on ? GS_SETREG_RGBA(0xF2, 0xF5, 0xF8, 0x80)
                               : GS_SETREG_RGBA(0x78, 0x83, 0x8F, 0x80));
        }
    }

    rmDrawRect(32, 438, 576, 1, GS_SETREG_RGBA(0x2A, 0x30, 0x38, 0x80));
    {
        int hx = 32, w, rx = 608;
        hx += shelfHint(hx, FTR_TEXT_Y, 0, "Play");
        shelfHint(hx, FTR_TEXT_Y, 1, "Back");
        if (total > LIB_PER) {
            int pages = (total + LIB_PER - 1) / LIB_PER;
            snprintf(buf, sizeof(buf), "%d / %d", page + 1, pages);
            w = fntCalcDimensions(FNT_DEFAULT, buf);
            fntRenderString(FNT_DEFAULT, rx - w, FTR_TEXT_Y, ALIGN_NONE, 0, 0, buf,
                            GS_SETREG_RGBA(0x5C, 0x66, 0x74, 0x80));
        }
        snprintf(buf, sizeof(buf), "~%u KB / %d binds", rmVramBoundBytes() >> 10,
                 rmVramBoundCount());
        w = fntCalcDimensions(appsFontSmall, buf);
        fntRenderString(appsFontSmall, rx - w, 424, ALIGN_NONE, 0, 0, buf,
                        GS_SETREG_RGBA(0x3C, 0x44, 0x4E, 0x80));
    }
}

void shelfHandleInputLibrary(void)
{
    int total = libSync();

    shelfHoldCron();

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
    else if (getKeyOn(KEY_UP))
        libSel = (libSel >= LIB_PER) ? libSel - LIB_PER : 0;
    else if (getKeyOn(KEY_DOWN))
        libSel = (libSel + LIB_PER < total) ? libSel + LIB_PER : total - 1;
    else if (getKeyOn(KEY_CROSS) && libList && libList->itemLaunch && libList->itemGetConfig)
        libList->itemLaunch(libList, libSel, libList->itemGetConfig(libList, libSel));
}
