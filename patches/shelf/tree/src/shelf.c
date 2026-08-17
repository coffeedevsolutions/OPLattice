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
#include "include/textures.h"
#include "include/config.h"
#include "include/ioman.h"
#include "include/gui.h"
#include "include/system.h"
#include "include/themes.h"

int gEnableShelfUI;

#define SHELF_WIDTH   218
/* The collapsed sidebar is an icon rail, not a sliver. It is always present on
   a SHELF page -- it is where you are, and hiding the only navigation until you
   remember a gesture is how the pages became a one-way door in the first place.
   On the classic list it doubles as the affordance that the panel exists.

   Icons are drawn from rectangles. The pages load no art by design, and a glyph
   from the font would be at the mercy of whichever face the theme supplies. */
#define SHELF_RAIL_W   28
/* Rail icons are 12 wide and 16 tall, and that ratio is the correction.
 *
 * Virtual coordinates are not square: x is laid across 640 while y goes down
 * 480, so on the 16:9 panel this console drives, twelve across renders as wide
 * as sixteen down. Drawing 12x16 therefore puts a square on the screen.
 *
 * Scaling each rectangle by three quarters instead -- the obvious fix -- is what
 * the previous attempt did, and it rounds every rect on its own: a 2px stroke
 * collapsed to 1, a roof that should step evenly went 2,3,5,6,7,9,11,12, and the
 * grid's dots landed on gaps of 1 and 2. Parts of one shape distorted by
 * different amounts, which reads far worse than the uniform stretch it replaced.
 * Choosing the height rounds nothing, because height is never scaled.
 *
 * The cost is 4:3, where 12x16 is a little tall rather than square. That is the
 * right way round for a console wired to a widescreen panel over HDMI. */
#define RAIL_ICON_W    12
#define SHELF_PEEK     SHELF_RAIL_W

/* Content starts after the rail on every page, so the rail can never sit on
   top of a page's own left edge. */
#define CONTENT_X    (SHELF_RAIL_W + 14)
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
/* Resolved once per frame by shelfSyncFonts: a theme face when the theme supplies
   one, otherwise the embedded face below at the size the layout was drawn for. */
static int appsFontSmall;   /* small face; used by the rail and every page */
static int appsFontBig;     /* display face; the clock, and nothing else yet */
static int appsFontLabel;   /* small values; quieter than body, not the same size */
static int appsFontHead;    /* headers only -- the dot face, never a sentence */
static int fontEmbedSmall, fontEmbedBig, fontEmbedLabel, fontEmbedHead;

/* Theme font slots this shell asks for, by conf_theme.cfg key:
     font5  body     12px
     font6  values    9px
     font7  clock    46px
     font8  headers  11px
   These are the theme's own slots, loaded by thmLoadFonts from the theme folder,
   so the shell needs no path of its own and a theme switch reloads the faces for
   free. thmLoadFonts leaves a slot pointing at fonts[0] when its key is missing
   or its file will not open, which is why the embedded slots stay loaded: a slot
   that did not take its own face is indistinguishable from one that was never
   asked for, and falling back to the default face would silently draw the whole
   page at 17px.

   Five and up because thm_GridHard's own elements already reference font1
   through font4 by number. Claiming a low slot would have restyled the classic
   grid as a side effect of restyling these pages. */
#define SHELF_FNT_BODY  5
#define SHELF_FNT_LABEL 6
#define SHELF_FNT_CLOCK 7
#define SHELF_FNT_HEAD  8

static void shelfHoldCron(void);
static void shelfSyncFonts(void);

/** A 3x3 block of dots: Library. */
/* A rect of a rail icon. Plain, deliberately: see RAIL_ICON_W. */
static void railR(int x, int y, int x0, int y0, int w, int h, u64 c)
{
    rmDrawRect(x + x0, y + y0, w, h, c);
}

/* Nine cells. 3 wide by 4 tall each, so a cell is square on screen too. */
static void railGrid(int x, int y, u64 c)
{
    int i, j;
    for (j = 0; j < 3; j++)
        for (i = 0; i < 3; i++)
            railR(x, y, i * 4, j * 6, 3, 4, c);
}

/** A house: roof from stacked bars, then a body. Home. */
static void railHome(int x, int y, u64 c)
{
    int i;
    /* Six even steps to a 12-wide base, then the body. Even because nothing is
       being rounded any more. */
    for (i = 0; i < 6; i++)
        railR(x, y, 5 - i, i, 2 + i * 2, 2, c);
    railR(x, y, 2, 6, 8, 10, c);
}

/** A pane split by a divider: Apps. */
static void railPanel(int x, int y, u64 c)
{
    railR(x, y, 0, 0, 12, 2, c);
    railR(x, y, 0, 14, 12, 2, c);
    railR(x, y, 0, 0, 2, 16, c);
    railR(x, y, 10, 0, 2, 16, c);
    railR(x, y, 5, 2, 2, 12, c);
}

/** A ring with four teeth: Settings. */
static void railGear(int x, int y, u64 c)
{
    railR(x, y, 3, 0, 6, 2, c);
    railR(x, y, 3, 14, 6, 2, c);
    railR(x, y, 0, 4, 2, 8, c);
    railR(x, y, 10, 4, 2, 8, c);
    railR(x, y, 4, 6, 4, 4, c);
}

/** The collapsed rail: brand mark, the four destinations, and link state.
 *  `active` is the item to mark, or -1 on the classic list where none applies. */
/* Animation runs off guiFrameId, which advances once per rendered frame. There
   is no frame delta to integrate, so everything here is a function of the count
   rather than of elapsed time -- slower on a dropped frame, which is the honest
   failure mode for a menu. */
#define FPS 60

/** Triangle wave, 0..255 across `period` frames. */
static int shelfPulse(int period)
{
    int t = guiFrameId % period, half = period / 2;
    return (t < half) ? (t * 255 / half) : (255 - (t - half) * 255 / half);
}

/** Smooth 0..255 across `period`, offset by `phase` frames.
 *
 *  A raw triangle reverses instantly at each end and reads as a bounce. Putting
 *  it through a smoothstep softens both turns, which is the difference between
 *  something ticking and something drifting. */
static int shelfWave(int period, int phase)
{
    int t = (guiFrameId + phase) % period, half = period / 2;
    int lin = (t < half) ? t * 255 / half : (255 - (t - half) * 255 / half);
    return (int)((long)lin * lin * (765 - 2 * lin) / 65025);
}

/** Vertical drift in pixels for a card, -amp..+amp.
 *
 *  Each card is given its own phase so they do not move as one slab. A shared
 *  phase looks like the screen is bobbing; staggered phases look like separate
 *  things suspended in the same medium, which is the intent. */
static int shelfFloat(int phase, int amp)
{
    return (shelfWave(7 * FPS, phase) - 128) * amp / 128;
}

/** A vertical ramp between two alphas, for scrims and glows.
 *
 *  Two-pixel bands, always. The step count used to be a parameter and it was
 *  set far too low -- eleven bands across ninety pixels is an eight percent
 *  opacity jump every eight pixels, which is plainly visible as stripes on a
 *  dark background. Alpha runs 0..0x80 on the GS, so 2px bands put each step
 *  near one unit and the ramp reads as continuous. Sprites are cheap; the
 *  banding was not worth the primitives it saved. */


static void shelfGradV(int x, int y, int w, int h, int a0, int a1, u64 rgb)
{
    int i, n = h / 2;
    if (n < 2)
        n = 2;
    for (i = 0; i < n; i++)
        rmDrawRect(x, y + i * 2, w, 2,
                   rgb | ((u64)(a0 + (a1 - a0) * i / (n - 1)) << 24));
}

/* The sheet's palette. Declared here rather than with the landing page, which
   is where it started: the rail, the dashboard and the landing are all one sheet
   now, and a colour used by three sections does not belong to any of them. */
#define LAND_BG    GS_SETREG_RGBA(0xC9, 0xBF, 0xA6, 0x80)
#define LAND_INK   GS_SETREG_RGBA(0x3A, 0x2E, 0x22, 0x80)
#define LAND_DIM   GS_SETREG_RGBA(0x3A, 0x2E, 0x22, 0x38)
#define LAND_FAINT GS_SETREG_RGBA(0x3A, 0x2E, 0x22, 0x1C)
#define LAND_RULE  GS_SETREG_RGBA(0x3A, 0x2E, 0x22, 0x4E)
#define LAND_TEXT  GS_SETREG_RGBA(0x3A, 0x2E, 0x22, 0x80)
#define LAND_MUTE  GS_SETREG_RGBA(0x3A, 0x2E, 0x22, 0x54)


/* The mark at the top of the rail, from the theme folder.
 *
 * Art, where everything else in this shell is primitives. That was the right
 * call for a house and a grid of squares, whose every edge is axis-aligned; it
 * is the wrong call for a mark with diagonals in it. Drawn by hand the
 * PlayStation logo distorted differently along its own curves, because a
 * one-pixel diagonal cannot be corrected for anamorphic the way a rectangle can.
 *
 * The file is 18 by 24 texels and drawn declared 24 by 24 with SCALING_RATIO,
 * which multiplies declared width by three quarters -- so it is drawn 18 wide
 * and the display stretches that back to 24. Texel for texel, no resampling. The
 * file is pre-squashed to suit, which is why it looks narrow on disk.
 *
 * White with the logo as its alpha, tinted at draw time, so the mark follows the
 * sheet's ink instead of the file baking a colour in.
 *
 * Reloaded when the theme changes, and asked for exactly once per theme: a
 * missing file sets state to -1 and is never retried, because retrying a failed
 * open every frame is how the font loader used to stall the renderer. */
static GSTEXTURE psLogo;
static int psLogoState;          /* 0 untried, 1 loaded, -1 absent */
static int psLogoTheme = -1;

static GSTEXTURE *shelfLogo(void)
{
    int themeId = thmGetGuiValue();

    if (themeId != psLogoTheme) {
        psLogoTheme = themeId;
        psLogoState = 0;
    }
    if (psLogoState == 0) {
        char path[192];
        char *dir = thmGetFilePath(themeId);

        psLogoState = -1;
        if (dir) {
            snprintf(path, sizeof(path), "%spslogo", dir);
            if (texDiscoverLoad(&psLogo, path, -1) == 0)
                psLogoState = 1;
        }
    }
    return (psLogoState == 1) ? &psLogo : NULL;
}

static void shelfDrawRail(int active)
{
    static const int iconY[4] = {64, 100, 136, 172};
    /* The pages call this as part of their own draw, but while the panel is out
       the panel IS the rail -- expanded. Drawing both would put the icon strip
       next to the item list it turns into. */
    if (state != SHELF_CLOSED)
        return;
    {
    u64 on  = LAND_INK;
    u64 off = LAND_MUTE;
    int i;

    /* The same ground as everything else, and one rule at its right edge. The
       rail used to be a dark slab, which made it a separate object sitting
       beside the page; on the same tan it is a margin of the sheet, and the one
       line is all that is needed to say where the margin ends. */
    rmDrawRect(0, 0, SHELF_RAIL_W, 480, LAND_BG);
    rmDrawRect(SHELF_RAIL_W, 0, 1, 480, LAND_RULE);

    {
        GSTEXTURE *logo = shelfLogo();
        if (logo)
            rmDrawPixmap(logo, (SHELF_RAIL_W - 18) / 2, 12, ALIGN_NONE, 24, 24,
                         SCALING_RATIO, LAND_INK);
    }

    {
        int ix = (SHELF_RAIL_W - RAIL_ICON_W) / 2;
        for (i = 0; i < 4; i++) {
            u64 c = (i == active) ? on : off;
            if (i == active)
                rmDrawRect(0, iconY[i] - 4, 3, 21, on);
            switch (i) {
                case 0: railHome(ix, iconY[i], c);  break;
                case 1: railGrid(ix, iconY[i], c);  break;
                case 2: railPanel(ix, iconY[i], c); break;
                default: railGear(ix, iconY[i], c); break;
            }
        }
    }

    /* The button that opens the panel, where the link dot used to sit. The dot
       reported something nobody was deciding anything on, and it reported it in
       the one place a first-time user looks to find out what to press. 18 by 24,
       because 18 across renders as wide as 24 down and a button drawn as an
       ellipse reads as a mistake rather than a control. The ring is a real
       ellipse sampled per scanline, not the octagon the footer marks use; at 24
       pixels that difference is visible. */
    {
        static const unsigned char l3ring[24][2] = {
            {5, 2}, {4, 3}, {3, 3}, {2, 3}, {1, 3}, {1, 2},
            {1, 1}, {0, 2}, {0, 2}, {0, 1}, {0, 1}, {0, 1},
            {0, 1}, {0, 1}, {0, 1}, {0, 2}, {0, 2}, {1, 1},
            {1, 2}, {1, 3}, {2, 3}, {3, 3}, {4, 3}, {5, 2}
        };
        const int bw = 18, bh = 24, by = 424;
        int bx = (SHELF_RAIL_W - bw) / 2;
        int tw = fntCalcDimensions(appsFontSmall, "L3");
        int k;

        for (k = 0; k < bh; k++) {
            int lx = l3ring[k][0], run = l3ring[k][1];
            rmDrawRect(bx + lx, by + k, run, 1, LAND_INK);
            rmDrawRect(bx + bw - lx - run, by + k, run, 1, LAND_INK);
        }
        fntRenderString(appsFontSmall, bx + (bw - tw) / 2, by + (bh - 12) / 2,
                        ALIGN_NONE, 0, 0, "L3", LAND_INK);
    }
    }
}

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
    shelfSyncFonts();

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

/* How far the page is pushed aside, in virtual pixels.
 *
 * The panel's right edge is at x + SHELF_WIDTH, and SHELF_PEEK of it was always
 * on screen as the rail -- so what the page has to yield is the difference, and
 * the page's left edge tracks the panel's right edge exactly through the slide.
 * Nothing here reads a clock: it is the same eased frame counter the panel
 * itself uses, so the two cannot drift apart. */
int shelfPushX(void)
{
    if (state == SHELF_CLOSED)
        return 0;
    return (int)(shelfEase((float)frame / (float)SHELF_FRAMES) *
                 (float)(SHELF_WIDTH - SHELF_PEEK));
}

void shelfDraw(void)
{
    float t;
    int x, i;

    /* Closed still draws: a sliver at the edge is the only thing telling anyone
     * the panel exists. It also keeps the panel's right edge on screen at all
     * times, so no primitive is ever issued wholly at negative x. */
    if (state == SHELF_CLOSED) {
        /* On a SHELF page the rail is part of the page and marks where you are.
           On the classic list it is the affordance that the panel exists. */
        int page = guiOnShelfPage() ? guiShelfPageIndex() : -1;
        if (!guiOnMainScreen() && page < 0)
            return;
        shelfDrawRail(page);
        return;
    }

    t = shelfEase((float)frame / (float)SHELF_FRAMES);
    x = (int)(-(SHELF_WIDTH - SHELF_PEEK) + t * (SHELF_WIDTH - SHELF_PEEK));

    rmDrawRect(x, 0, SHELF_WIDTH, 480, LAND_BG);
    /* A 1px sprite, not a line. rmDrawLine is the only LINE primitive the panel
     * would issue, and shelf.c had the only *vertical* one in the whole tree --
     * everything else in OPL draws horizontal rules. Since the white flash
     * appears exactly when the panel opens, the untrodden primitive is the first
     * thing to remove; a 1px sprite is visually identical and is the same
     * primitive as everything else here. */
    rmDrawRect(x + SHELF_WIDTH, 0, 1, 480, LAND_RULE);

    fntRenderString(appsFontHead, x + 24, 28, ALIGN_NONE, 0, 0, "SHELF", LAND_INK);

    /* Read at draw time, but both are values OPL already holds -- no queries. */
    {
        const char *net = (gNetworkStartup == 0) ? "Online" : "Offline";

        rmDrawRect(x + 16, 438, SHELF_WIDTH - 32, 1, LAND_RULE);
        fntRenderString(appsFontSmall, x + 24, 450, ALIGN_NONE, 0, 0, net, LAND_MUTE);
    }

    for (i = 0; i < (int)SHELF_ITEMS; i++) {
        int iy = 84 + i * 34;

        /* Selection is a wash rather than a slab: on tan a filled bar would be
           darker than the ink it carries, and the row would read as inverted. */
        if (i == selected) {
            rmDrawRect(x, iy - 8, SHELF_WIDTH, 30, LAND_FAINT);
            rmDrawRect(x, iy - 8, 3, 30, LAND_INK);
        }

        fntRenderString(appsFontSmall, x + 34, iy, ALIGN_NONE, 0, 0, items[i],
                        i == selected ? LAND_INK : LAND_MUTE);
    }
}

/* ------------------------------------------------------------ page stubs */



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
    /* No left edge on a page that has no cursor: L3 only. And no Back -- see the
       note on the three real pages. */
    shelfTrigger(0);
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
   (textBaselineOffset adds size-2 for ALIGN_NONE), so centring a string in the
   bar means subtracting its own size -- not a constant.
   It was a constant, ((HDR_H - 17) / 2), written when everything in this bar was
   FNT_DEFAULT at 17px. The header moved to the 11px face with the theme fonts
   and kept the old offset, which left it sitting three pixels high in its own
   bar. Taking the size as an argument is what stops that happening again. */
#define HDR_H       40
#define HDR_CY(sz)  ((HDR_H - (sz)) / 2)
#define HDR_TEXT_Y  HDR_CY(12)
#define FTR_TEXT_Y  462   /* == LIB_FTR_TEXT; one hint row height everywhere */

#define APPS_COLS   3
#define APPS_PER    (APPS_COLS * 2)
#define APPS_MARGIN CONTENT_X
#define APPS_GAP    16
#define APPS_CW     ((640 - APPS_MARGIN - 24 - (APPS_COLS - 1) * APPS_GAP) / APPS_COLS)
#define APPS_CH     170
#define APPS_Y0     64

/* The PS2 face buttons, drawn small and in ink.
 *
 * Drawn, not typed, and that is not a style choice. None of the three faces on
 * this card carries U+25CB, U+25A1 or U+25B3 -- SUSE Mono, Bitcount and the
 * embedded PoeVetica all lack every one of them, so a footer written as text
 * would come out as blanks on the console. It only looks right in a browser
 * because the browser silently substitutes a system font. Two primitives each
 * costs nothing and cannot fail that way.
 *
 * Monochrome. The buttons carried their own hues, which on a light sheet made
 * the footer the most saturated thing on a page that has no other colour in it.
 * The shape says which button it is; the colour was saying it twice.
 *
 * Sized to the 9px label rather than standing over it: the glyph box is nine
 * pixels tall and so is each mark, so the row reads as one line of type.
 *
 * Returns the width consumed so a caller can lay a row out without measuring
 * twice -- measured with the font it actually draws with, which it was not: it
 * asked FNT_DEFAULT at 17px for the width of a string it drew at 12, and every
 * hint row has been reserving about a third more space than it uses. */
static int shelfHint(int x, int y, int kind, const char *label)
{
    /* Seven wide by nine tall, which is the same trick the rail icons use: x is
       laid across 640 and y down 480, so seven across renders as wide as nine
       down and the square is a square. At 9 by 9 -- what these were -- the circle
       came out an oval and the square a rectangle, by the same third that made
       the rail icons look stretched. Nothing is scaled to get here, so no part of
       a mark rounds differently from any other part. */
    static const unsigned char ring[9][2] = {   /* left edge, run; mirrored */
        {2, 3}, {1, 1}, {0, 1}, {0, 1}, {0, 1}, {0, 1}, {0, 1}, {1, 1}, {2, 3}
    };
    /* Column per scanline of the cross. Hand-authored rather than computed:
       rounding i * 6 / 8 puts both half-steps in the same direction and the X
       comes out lopsided. This one mirrors exactly about the centre row. */
    static const unsigned char cross[9] = {0, 1, 2, 2, 3, 4, 4, 5, 6};
    const u64 col = LAND_INK;
    int cy = y + 4;          /* centre of the 9px label's glyph box */
    int i, w;

    if (kind == 2) {         /* square */
        rmDrawRect(x, cy - 4, 7, 1, col);
        rmDrawRect(x, cy + 4, 7, 1, col);
        rmDrawRect(x, cy - 4, 1, 9, col);
        rmDrawRect(x + 6, cy - 4, 1, 9, col);
    } else if (kind == 0) {  /* cross */
        for (i = 0; i < 9; i++) {
            rmDrawRect(x + cross[i], cy - 4 + i, 1, 1, col);
            rmDrawRect(x + 6 - cross[i], cy - 4 + i, 1, 1, col);
        }
    } else {                 /* circle */
        for (i = 0; i < 9; i++) {
            int lx = ring[i][0], t = ring[i][1];
            rmDrawRect(x + lx, cy - 4 + i, t, 1, col);
            if (i != 0 && i != 8)
                rmDrawRect(x + 7 - lx - t, cy - 4 + i, t, 1, col);
        }
    }
    fntRenderString(appsFontLabel, x + 12, y, ALIGN_NONE, 0, 0, label, LAND_TEXT);
    w = 12 + fntCalcDimensions(appsFontLabel, label);
    return w + 18;
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
    fontEmbedSmall = (id == FNT_ERROR) ? FNT_DEFAULT : id;
    /* 16:9 squeezes glyphs to three quarters width, so a display size that
       would be overbearing at 4:3 reads correctly here. */
    id = fntLoadFile(NULL, 46);
    fontEmbedBig = (id == FNT_ERROR) ? FNT_DEFAULT : id;
    id = fntLoadFile(NULL, 9);
    fontEmbedLabel = (id == FNT_ERROR) ? fontEmbedSmall : id;
    /* Headers sit at 11 rather than with the 9px values. The dot face needs the
       extra two pixels: below 11 its grid stops resolving and the lettering
       closes up into a smudge. */
    id = fntLoadFile(NULL, 11);
    fontEmbedHead = (id == FNT_ERROR) ? fontEmbedSmall : id;

    appsFontSmall = fontEmbedSmall;
    appsFontBig = fontEmbedBig;
    appsFontLabel = fontEmbedLabel;
    appsFontHead = fontEmbedHead;
}

/* A theme slot that never loaded a face of its own is left equal to fonts[0], so
   that comparison is the test for "the theme actually supplied this". */
static int shelfFace(int slot, int fallback)
{
    if (gTheme && slot > 0 && slot < THM_MAX_FONTS && gTheme->fonts[slot] != gTheme->fonts[0])
        return gTheme->fonts[slot];
    return fallback;
}

/* Re-resolved every frame rather than cached at theme-load time: thmLoad frees
   and reloads these handles, and a stale id here would index a released slot. */
static void shelfSyncFonts(void)
{
    appsFontSmall = shelfFace(SHELF_FNT_BODY, fontEmbedSmall);
    appsFontLabel = shelfFace(SHELF_FNT_LABEL, fontEmbedLabel);
    appsFontBig = shelfFace(SHELF_FNT_CLOCK, fontEmbedBig);
    appsFontHead = shelfFace(SHELF_FNT_HEAD, fontEmbedHead);
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

    rmDrawRect(SHELF_RAIL_W, 0, 640 - SHELF_RAIL_W, 40, LAND_BG);
    rmDrawRect(SHELF_RAIL_W, 40, 640 - SHELF_RAIL_W, 1, LAND_RULE);
    fntRenderString(appsFontHead, CONTENT_X, HDR_CY(11), ALIGN_NONE, 0, 0, "APPS", LAND_INK);

    /* Placeholder until Phase 8 binds it. sceCdReadClock is available and
       already used at OSDHistory.c:122, but its fields are BCD and its RTC runs
       on JST, so an honest clock needs an offset this phase cannot configure. */
    w = fntCalcDimensions(appsFontSmall, "--:--");
    fntRenderString(appsFontSmall, rx - w, HDR_TEXT_Y, ALIGN_NONE, 0, 0, "--:--", LAND_MUTE);
    rx -= w + 22;

    /* Free space has no query for this device class. Nothing in bdmsupport,
       mmcesupport or ethsupport reports capacity; the only capacity call in the
       tree is HDIOC_TOTALSECTOR for the internal HDD, which is total and not
       free. The slot is real, the value is not, and inventing one is worse. */
    w = fntCalcDimensions(appsFontSmall, "\xe2\x80\x94 free");
    fntRenderString(appsFontSmall, rx - w, HDR_TEXT_Y, ALIGN_NONE, 0, 0, "\xe2\x80\x94 free", LAND_MUTE);
    rx -= w + 22;

    {
        const char *net = (gNetworkStartup == 0) ? "NET" : "OFF";
        u64 col = (gNetworkStartup == 0) ? GS_SETREG_RGBA(0x2E, 0x6E, 0x3C, 0x80)
                                         : LAND_MUTE;
        w = fntCalcDimensions(appsFontSmall, net);
        fntRenderString(appsFontSmall, rx - w, HDR_TEXT_Y, ALIGN_NONE, 0, 0, net, col);
        rmDrawRect(rx - w - 14, HDR_CY(8), 5, 5, col);
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

    rmDrawRect(0, 0, 640, 480, LAND_BG);
    appsStatusBar();

    if (total <= 0) {
        /* An empty state, not a grid of nothing with a selection index pointing
           at an item that does not exist. */
        fntRenderString(FNT_DEFAULT, CONTENT_X, 120, ALIGN_NONE, 0, 0,
                        "No applications found.", LAND_TEXT);
        fntRenderString(appsFontSmall, CONTENT_X, 148, ALIGN_NONE, 0, 0,
                        "Put an ELF and a title.cfg under APPS/ on a device OPL can see.",
                        LAND_MUTE);
    }

    for (i = 0; i < APPS_PER && first + i < total; i++) {
        int idx = first + i;
        int cx = APPS_MARGIN + (i % APPS_COLS) * (APPS_CW + APPS_GAP);
        int cy = APPS_Y0 + (i / APPS_COLS) * (APPS_CH + APPS_GAP);
        int on = (idx == appsSel);
        int ix = cx + (APPS_CW - 88) / 2;
        char initial[2];

        /* Outlined, not filled. A selected slab on tan comes out darker than the
           ink it carries and the row reads as inverted, which is the same trap the
           drawer's selection fell into. */
        rmDrawRect(cx, cy, APPS_CW, APPS_CH, on ? LAND_FAINT : LAND_BG);
        if (on) {
            u64 e = LAND_INK;
            rmDrawRect(cx, cy, APPS_CW, 2, e);
            rmDrawRect(cx, cy + APPS_CH - 2, APPS_CW, 2, e);
            rmDrawRect(cx, cy, 2, APPS_CH, e);
            rmDrawRect(cx + APPS_CW - 2, cy, 2, APPS_CH, e);
        } else {
            rmDrawRect(cx, cy, APPS_CW, 1, LAND_RULE);
            rmDrawRect(cx, cy + APPS_CH - 1, APPS_CW, 1, LAND_RULE);
        }

        /* Icon placeholder. Real icons are Phase 6's business: six textures is a
           new simultaneous working set on a page that currently costs nothing in
           VRAM, and the prefetch wrapper and VRAM debug line that would let
           anyone size that budget honestly do not exist yet. */
        rmDrawRect(ix, cy + 20, 88, 88, LAND_FAINT);
        if (!apps)
            continue;

        initial[0] = apps[idx].title[0];
        initial[1] = '\0';
        appsCentred(FNT_DEFAULT, cx + APPS_CW / 2, cy + 73, initial,
                    APPS_CW - 16, LAND_MUTE);
        appsCentred(FNT_DEFAULT, cx + APPS_CW / 2, cy + 134, apps[idx].title,
                    APPS_CW - 16, LAND_TEXT);
        /* Legacy entries have no sidecar and so get no line at all rather than
           an invented one: conf_apps.cfg is Name=path, with no third field. */
        if (apps[idx].subtitle[0])
            appsCentred(appsFontSmall, cx + APPS_CW / 2, cy + 154,
                        apps[idx].subtitle, APPS_CW - 16, LAND_MUTE);
    }

    /* Ends where the cards end, not 2 x margin in. The grid's right edge is
       640 - 24, not 640 - APPS_MARGIN, so the symmetric version stopped 70px
       short of the column it was supposed to sit under. */
    rmDrawRect(APPS_MARGIN, 438, 640 - APPS_MARGIN - 24, 1, LAND_RULE);
    {
        int hx = APPS_MARGIN;
        /* No Back hint, because there is no Back. A footer that advertises a
           button which does nothing is worse than a footer with one fewer. */
        shelfHint(hx, FTR_TEXT_Y, 0, "Launch");
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
            fntRenderString(FNT_DEFAULT, rx - w, FTR_TEXT_Y, ALIGN_NONE, 0, 0, v, LAND_MUTE);
        }
        snprintf(v, sizeof(v), "~%u KB / %d binds", rmVramBoundBytes() >> 10,
                 rmVramBoundCount());
        w = fntCalcDimensions(appsFontSmall, v);
        fntRenderString(appsFontSmall, rx - w, 424, ALIGN_NONE, 0, 0, v, LAND_DIM);
    }

    shelfDrawRail(guiShelfPageIndex());
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


    /* No Back. Home, Library and Apps are top-level views, not somewhere you
       arrived from -- there is nothing above them to return to, and a Back that
       drops you onto the classic list makes the shell feel like a detour off it
       rather than the thing you booted into. L3 opens the panel; the panel is
       the whole of the navigation. Details and Settings keep theirs, because
       those you genuinely did arrive at from somewhere. */
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
/* The hero's caption sits on the bottom of its own container, inset by the same
   amount it is inset from the left -- CONTENT_X - SHELF_RAIL_W, which is 14.
   Derived rather than written down, so it follows the rail if that moves again. */
#define LIB_HERO_PAD  (CONTENT_X - SHELF_RAIL_W)
#define LIB_HERO_META_Y (LIB_HERO_H - LIB_HERO_PAD - 12)
#define LIB_HERO_TEXT_Y (LIB_HERO_META_Y - 11 - 17)
/* The covers start where the scale beside them starts. It was 240 against an
   alphabet at 222, so the ruler began 18 pixels above the first thing it was
   measuring. Moving the grid rather than the scale is what keeps the second row
   showing more of itself above the footer. */
#define LIB_GRID_Y    222
/* The theme's own footer: botbar is a 30px strip at y=-30, and HintText sits at
   y=-26 in font2 (12px) #8894A2. Matched rather than invented, so the shelf and
   the screen the console boots into agree about where the bottom of the page is. */
/* The footer, and the hint row centred in it. Height first, position derived:
   it was a 30px bar with the text nailed to 454, which centred a 12px string and
   then kept the number when the hints dropped to 9 -- leaving them six pixels
   high in their own bar. Deriving the text from the height is what stops that
   happening the next time a font size moves. */
#define LIB_FTR_H     26
#define LIB_FTR_Y     (480 - LIB_FTR_H)
/* The alphabet index down the left of the grid.
   The span no longer has to seat 26 caps -- only nine are letters now and the
   rest are dots -- so it gives back the room it was borrowing at both ends and
   stands clear of the hero above and the footer below rather than running into
   them. 206 over 25 gaps is 8.24 apiece, and the named letters are three marks
   apart, so they clear each other by nearly 25. */
#define LIB_ALPHA_Y0  LIB_GRID_Y    /* top of the scale == top of the first cover */
#define LIB_ALPHA_Y1  428
#define LIB_ALPHA_DX  22            /* left of the grid, which moves with aspect */
#define LIB_ALPHA_N   26
/* Nine letters named, the rest dots: A D G J M P S V Z. That is every third,
   except that three does not divide twenty-five, so a plain step lands on Y and
   never reaches the letter the scale is supposed to run to. Spreading a fixed
   count across the span instead puts a label on both ends and pays for it with
   one gap of four at the bottom rather than a missing Z. */
#define LIB_ALPHA_LABELS 9
#define LIB_FTR_TEXT  (LIB_FTR_Y + (LIB_FTR_H - 9) / 2)

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

/* The alphabet index, and the marker that rides it.
 *
 * Fixed, by construction: it is drawn from constants and the grid's own left
 * edge, so paging the tiles moves nothing here. Only the marker moves.
 *
 * The marker is proportional -- row over rows, mapped onto the A..Z span --
 * rather than a lookup of the selected title's initial. That is what was asked
 * for, and it has the advantage of being true whatever order the list is in.
 * The letters are therefore a ruler rather than a promise: they say how far
 * through you are, and they only line up with actual initials when the active
 * sort is A-Z. Worth knowing before trusting them to jump to a letter.
 *
 * Eased rather than snapped, because the thing it is reporting -- which row of
 * six you are on -- moves in jumps, and a marker that jumps with it reads as a
 * redraw rather than as movement. libAlphaPos is 1/16px so the ease has
 * somewhere to go between whole pixels. */
static int libAlphaPos = -1;

static int libAlphaY(int i)
{
    return LIB_ALPHA_Y0 + i * (LIB_ALPHA_Y1 - LIB_ALPHA_Y0) / (LIB_ALPHA_N - 1);
}

/* Whether mark i carries its letter. Integer division lands k=0 on A and
   k=LABELS-1 exactly on Z, which is the whole reason for counting labels rather
   than stepping letters. */
static int libAlphaLabelled(int i)
{
    int k;
    for (k = 0; k < LIB_ALPHA_LABELS; k++)
        if (k * (LIB_ALPHA_N - 1) / (LIB_ALPHA_LABELS - 1) == i)
            return 1;
    return 0;
}

static void libDrawAlphabet(int gridX, int total)
{
    int ax = gridX - LIB_ALPHA_DX;      /* letters */
    int lx = ax - 9;                    /* the line, and the marker on it */
    int rows = (total + LIB_COLS - 1) / LIB_COLS;
    int row = (total > 0) ? libSel / LIB_COLS : 0;
    int tgt, d, my, near, i;

    if (total <= 0)
        return;                         /* nothing to be an index of */

    tgt = (rows > 1) ? LIB_ALPHA_Y0 + row * (LIB_ALPHA_Y1 - LIB_ALPHA_Y0) / (rows - 1)
                     : LIB_ALPHA_Y0;
    tgt <<= 4;
    if (libAlphaPos < 0)
        libAlphaPos = tgt;              /* first frame: place it, do not fly it in */
    d = tgt - libAlphaPos;
    /* Integer ease. Snapping inside a pixel stops the quarter-step stalling out
       short of the target, which would leave the marker permanently a hair off. */
    if (d > -16 && d < 16)
        libAlphaPos = tgt;
    else
        libAlphaPos += d / 4;
    my = libAlphaPos >> 4;

    rmDrawRect(lx + 3, LIB_ALPHA_Y0, 1, LIB_ALPHA_Y1 - LIB_ALPHA_Y0 + 8, LAND_RULE);

    /* Whichever letter the marker is nearest reads as ink; the rest are a scale.
       Computed from the marker rather than from the row so it tracks the ease. */
    near = (my - LIB_ALPHA_Y0) * (LIB_ALPHA_N - 1) * 2 + (LIB_ALPHA_Y1 - LIB_ALPHA_Y0);
    near /= (LIB_ALPHA_Y1 - LIB_ALPHA_Y0) * 2;
    if (near < 0) near = 0;
    if (near > LIB_ALPHA_N - 1) near = LIB_ALPHA_N - 1;

    for (i = 0; i < LIB_ALPHA_N; i++) {
        u64 col = (i == near) ? LAND_TEXT : LAND_DIM;
        int y = libAlphaY(i);
        if (libAlphaLabelled(i)) {
            char c[2];
            c[0] = (char)('A' + i);
            c[1] = '\0';
            fntRenderString(appsFontLabel, ax, y, ALIGN_NONE, 0, 0, c, col);
        } else {
            /* Flush with the letters' left edge, not centred under them. Every
               cap in this face reports bitmap_left 0 and is four pixels wide at
               9px, so the ink starts exactly at ax and a dot at ax + 2 sat in
               the letter's right half. Vertically it is centred on where the cap
               would be rather than on the glyph box, whose top is y. */
            rmDrawRect(ax, y + 3, 2, 3, col);
        }
    }

    rmDrawRect(lx, my + 3, 9, 2, LAND_INK);
}

void shelfRenderLibrary(void)
{
    int total = libSync();
    int pitchX = rmWideScale(LIB_CELL_W);
    int drawnW = rmWideScale(LIB_ART_W);
    int gridW  = pitchX * LIB_COLS;
    /* Centred in the space beside the rail, not in the whole screen. */
    int x0     = SHELF_RAIL_W + (640 - SHELF_RAIL_W - gridW) / 2;
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

    rmDrawRect(0, 0, 640, 480, LAND_BG);

    /* Hero: the highlighted game, not the last played. */
    if (total > 0) {
        GSTEXTURE *hero = libHero(libSel);
        if (hero)
            rmDrawPixmap(hero, SHELF_RAIL_W, 0, ALIGN_NONE, 640 - SHELF_RAIL_W,
                         LIB_HERO_H, SCALING_NONE, gDefaultCol);
        else
            /* Ink, not tan. This stands in for artwork, and the title drawn on
               it is light because normally it sits on a photograph. */
            rmDrawRect(SHELF_RAIL_W, 0, 640 - SHELF_RAIL_W, LIB_HERO_H, LAND_INK);
        for (i = 0; i < 12; i++)
            rmDrawRect(SHELF_RAIL_W, LIB_HERO_H - 132 + i * 11, 640 - SHELF_RAIL_W, 11,
                       GS_SETREG_RGBA(0x1E, 0x18, 0x12, 4 + i * 7));
    }

    /* The theme's own detail fields, in the theme's order: name, the two
       attribute strips, then the description. */
    if (total > 0) {
        const char *name = libMetaName[0] ? libMetaName
                         : (libList && libList->itemGetName
                            ? libList->itemGetName(libList, libSel) : NULL);
        /* Bottom-aligned, with the same 14 the text is inset from the container's
           left edge (CONTENT_X - SHELF_RAIL_W). The metadata line is the one that
           has to land on it, so it is placed first and the title hangs above. */
        if (name)
            fntRenderString(FNT_DEFAULT, CONTENT_X, LIB_HERO_TEXT_Y, ALIGN_NONE, 0, 0, name,
                            GS_SETREG_RGBA(0xF2, 0xF5, 0xF8, 0x80));
        /* One line, not four. The full set belongs on the details page, which
           Square now opens; repeating it here only crowded the picture. */
        if (libMetaA[0])
            fntRenderString(appsFontSmall, CONTENT_X, LIB_HERO_META_Y, ALIGN_NONE, 0, 0, libMetaA,
                            GS_SETREG_RGBA(0x88, 0x94, 0xA2, 0x80));
    }

    if (total <= 0) {
        fntRenderString(FNT_DEFAULT, CONTENT_X, 120, ALIGN_NONE, 0, 0,
                        "Nothing to show yet.", LAND_TEXT);
        fntRenderString(appsFontSmall, CONTENT_X, 148, ALIGN_NONE, 0, 0,
                        "This page follows the device the main list is on. Pick one there first.",
                        LAND_MUTE);
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
            rmDrawRect(cx, cy, drawnW, LIB_ART_H, LAND_FAINT);

        if (idx == libSel) {
            u64 e = LAND_INK;
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
                                idx == libSel ? LAND_TEXT : LAND_MUTE);
        }
    }

    libDrawAlphabet(x0, total);

    /* Drawn last, so the bottom row of tiles runs under it. */
    rmDrawRect(SHELF_RAIL_W, LIB_FTR_Y, 640 - SHELF_RAIL_W, 480 - LIB_FTR_Y, LAND_BG);
    rmDrawRect(SHELF_RAIL_W, LIB_FTR_Y, 640 - SHELF_RAIL_W, 1, LAND_RULE);
    {
        int hx = CONTENT_X, w, rx = 605;
        hx += shelfHint(hx, LIB_FTR_TEXT, 0, "Play");
        shelfHint(hx, LIB_FTR_TEXT, 2, "Details");
        if (total > 0) {
            snprintf(buf, sizeof(buf), "%d of %d", libSel + 1, total);
            w = fntCalcDimensions(appsFontSmall, buf);
            /* Right-aligned, and only drawn if the hints have not reached it --
               overlapping is worse than omitting a count you can infer. */
            if (rx - w > hx + 8)
                fntRenderString(appsFontSmall, rx - w, LIB_FTR_TEXT, ALIGN_NONE, 0, 0,
                                buf, LAND_MUTE);
        }
    }

    shelfDrawRail(guiShelfPageIndex());
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


    /* No Back. Home, Library and Apps are top-level views, not somewhere you
       arrived from -- there is nothing above them to return to, and a Back that
       drops you onto the classic list makes the shell feel like a detour off it
       rather than the thing you booted into. L3 opens the panel; the panel is
       the whole of the navigation. Details and Settings keep theirs, because
       those you genuinely did arrive at from somewhere. */
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

#define HOME_TILES   5
#define HOME_COLS    5
#define HOME_GAP     10
#define HOME_HERO_H  132
#define HOME_M       CONTENT_X
#define HOME_COL_W   184
#define HOME_R_X     (HOME_M + HOME_COL_W + 12)
#define HOME_R_W     (640 - HOME_R_X - 24)

static image_cache_t *homeCover, *homeHero;
static int homeCovId[OPL_RECENT_MAX], homeCovUid[OPL_RECENT_MAX];
static int homeHeroId[OPL_RECENT_MAX], homeHeroUid[OPL_RECENT_MAX];
/* Index into the recent list for the *strip*, which starts at 1: entry 0 is
   the hero, and a page that showed the same game twice would be listing it
   rather than featuring it. */
static int homeSel = 1;
/* 0 = the Continue card, 1 = the recent strip. Which section the cursor is
   on, as distinct from which game is selected -- both sections show the
   same game, so selection alone could not say where the cursor was. */
static int homeFocus;

/* Home is two panels stacked, and the page scrolls between them. 0 is the
   landing, 1 is the dashboard; homeScrollT counts frames through the move.
   rmSetScrollY does the work, so neither panel's coordinates know the other
   exists. */
#define HOME_SCROLL_FRAMES 22
static void homeDrawDash(void);

/* Library-wide figures, scanned once per list; the landing's stream reads them
   too, so they are declared before either user. */
static item_list_t *homeScanList;
static int homeScanCount, homeTotalMinutes, homeTotalTitles, homeTotalPlayed;
static int homeView;
static int homeScrollT;


/* The landing: a schematic, not a photograph.
 *
 * Tan ground, dark brown ink, flat rules and corner brackets -- the look of a
 * drawing rather than a screen. Everything on it is real: the clock is the RTC,
 * and the stream on the right is this build's actual configuration rather than
 * invented telemetry, which is the whole reason it is worth showing.
 */
/** A 45-degree run, as a staircase. There is no diagonal primitive; at one pixel
 *  per step the stair is the line. `dir` picks the quadrant. */
static void shelfDiag(int x, int y, int len, int dx, int dy, u64 col)
{
    int i;
    for (i = 0; i < len; i++)
        rmDrawRect(x + i * dx, y + i * dy, 1, 1, col);
}

/** One line of the stream. Real values, read at draw time. */
static void landLine(int slot, char *out, size_t n)
{
    item_list_t *list = menuGetActiveList();
    switch (slot % 10) {
        case 0: snprintf(out, n, "BUILD  %s", OPL_VERSION); break;
        case 1: snprintf(out, n, "VMODE  %s", gWideScreen ? "DTV480P 16:9"
                                                          : "DTV480P 4:3"); break;
        case 2: snprintf(out, n, "LINK   %s", gNetworkStartup == 0 ? "UP" : "DOWN"); break;
        case 3: snprintf(out, n, "DEV    %s",
                         (list && list->itemGetPrefix) ? list->itemGetPrefix(list) : "-"); break;
        case 4: snprintf(out, n, "TITLES %d", homeTotalTitles); break;
        case 5: snprintf(out, n, "PLAYED %d", homeTotalPlayed); break;
        case 6: snprintf(out, n, "MINS   %d", homeTotalMinutes); break;
        case 7: snprintf(out, n, "VRAM   %u", rmVramBoundBytes()); break;
        case 8: snprintf(out, n, "BINDS  %d", rmVramBoundCount()); break;
        default: snprintf(out, n, "TZ     %+d", configGetTimezone()); break;
    }
}

static void homeDrawLanding(int hh, int mm, int haveClock)
{
    const int RIGHT = 596, ROWS = 9, PITCH = 16;
    const int STEP = 26;                  /* frames per new line */
    int i, head = guiFrameId / STEP, typed = (guiFrameId % STEP) * 20 / STEP;
    char buf[64];

    rmDrawRect(0, 0, 640, 480, LAND_BG);

    /* Frame: corner brackets and a hairline inset, which is what makes a flat
       fill read as a drawing rather than as an empty screen. */
    for (i = 0; i < 4; i++) {
        int bx = (i & 1) ? 596 : 30, by = (i & 2) ? 432 : 26;
        int sx = (i & 1) ? -1 : 1, sy = (i & 2) ? -1 : 1;
        rmDrawRect(bx - (sx < 0 ? 26 : 0), by, 26, 1, LAND_INK);
        rmDrawRect(bx, by - (sy < 0 ? 20 : 0), 1, 20, LAND_INK);
        shelfDiag(bx + sx * 6, by + sy * 6, 10, sx, sy, LAND_DIM);
    }
    rmDrawRect(30, 240, 566, 1, LAND_FAINT);

    /* Edge runners: a lit segment travelling each side, and diagonals cutting
       the corners. Ninety and forty-five degrees only -- anything else would
       stop it looking drafted. */
    {
        int p = guiFrameId % 640;
        rmDrawRect(p - 60, 26, 60, 1, LAND_INK);
        rmDrawRect(580 - p, 458, 60, 1, LAND_INK);
        rmDrawRect(30, (guiFrameId * 2 / 3) % 480, 1, 40, LAND_DIM);
        rmDrawRect(596, 440 - ((guiFrameId * 2 / 3) % 480), 1, 40, LAND_DIM);
        shelfDiag(30 + (guiFrameId / 4) % 90, 26 + (guiFrameId / 4) % 90, 24, 1, 1, LAND_DIM);
        shelfDiag(596 - (guiFrameId / 5) % 90, 458 - (guiFrameId / 5) % 90, 24, -1, -1, LAND_DIM);
    }

    if (haveClock) {
        snprintf(buf, sizeof(buf), "%02d", hh);
        fntRenderString(appsFontBig, 44, 150, ALIGN_NONE, 0, 0, buf, LAND_INK);
        {
            /* No manual gap either side of the colon. The two pixels that used
               to be here were measured against the embedded face; on a grid
               face the advance is the design, and padding it opens a hole. */
            int hw = fntCalcDimensions(appsFontBig, buf);
            fntRenderString(appsFontBig, 44 + hw, 150, ALIGN_NONE, 0, 0, ":",
                            GS_SETREG_RGBA(0x3A, 0x2E, 0x22,
                                           0x38 + shelfPulse(2 * FPS) / 3));
            snprintf(buf, sizeof(buf), "%02d", mm);
            fntRenderString(appsFontBig, 44 + hw + fntCalcDimensions(appsFontBig, ":"),
                            150, ALIGN_NONE, 0, 0, buf, LAND_INK);
        }
        fntRenderString(appsFontHead, 46, 212, ALIGN_NONE, 0, 0,
                        hh < 5 ? "NIGHT" : hh < 12 ? "MORNING"
                        : hh < 18 ? "AFTERNOON" : "EVENING", LAND_DIM);
    }

    /* The stream. Right-aligned and stacked, newest at the bottom and revealed a
       character at a time; older lines fade as they rise. Right-aligned because
       a ragged left edge is what makes a column of values read as output rather
       than as a paragraph. */
    for (i = 0; i < ROWS; i++) {
        int age = ROWS - 1 - i;                    /* 0 = newest */
        int y = 148 + i * PITCH;
        int alpha;
        landLine(head - age, buf, sizeof(buf));
        if (age == 0 && typed < (int)strlen(buf))
            buf[typed] = '\0';
        alpha = 0x60 - age * 0x0A;
        if (alpha < 0x10)
            alpha = 0x10;
        {
            int w = fntCalcDimensions(appsFontLabel, buf);
            fntRenderString(appsFontLabel, RIGHT - w, y, ALIGN_NONE, 0, 0, buf,
                            GS_SETREG_RGBA(0x3A, 0x2E, 0x22, alpha));
            if (age == 0)
                rmDrawRect(RIGHT + 3, y + 1, 4, 8,
                           GS_SETREG_RGBA(0x3A, 0x2E, 0x22,
                                          (guiFrameId / 15) & 1 ? 0x60 : 0x10));
        }
    }

    fntRenderString(appsFontLabel, 46, 400, ALIGN_NONE, 0, 0, "DOWN FOR HOME",
                    GS_SETREG_RGBA(0x3A, 0x2E, 0x22,
                                   0x24 + shelfPulse(3 * FPS) / 3));
}

/* Library-wide figures, scanned once per list. This is the index pass Phase 0
   anticipated and Phase 8 reuses for Group=/Label=. Keyed on the support object
   and the item count, so it re-runs on a device change and never otherwise. */
#define HOME_TOP_N 4
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

/** Ease 0..1 across the scroll, as a 0..480 offset. Smoothstep, so the page
 *  settles rather than stops. */
static int homeScrollOffset(void)
{
    int t = homeScrollT * 255 / HOME_SCROLL_FRAMES;
    int sm = (int)((long)t * t * (765 - 2 * t) / 65025);
    return sm * 480 / 255;
}

void shelfRenderHome(void)
{
    int hh = 0, mm = 0, days = 0, haveClock = homeLocalTime(&hh, &mm, &days);
    int total = oplRecentCount();
    int off = homeScrollOffset();

    /* Advance the scroll here rather than in the input handler: the handler
       does not run during a screen transition, and a page caught mid-scroll
       would sit frozen halfway. */
    if (homeView == 1 && homeScrollT < HOME_SCROLL_FRAMES) homeScrollT++;
    if (homeView == 0 && homeScrollT > 0)                 homeScrollT--;

    /* Two panels, one above the other. rmSetScrollY moves everything a panel
       draws, so neither knows the other is there. */
    rmSetScrollY(-off);
    homeDrawLanding(hh, mm, haveClock);
    rmSetScrollY(480 - off);
    homeDrawDash();
    rmSetScrollY(0);

    /* Chrome does not scroll. */
    rmDrawRect(SHELF_RAIL_W, LIB_FTR_Y, 640 - SHELF_RAIL_W, 480 - LIB_FTR_Y,
               GS_SETREG_RGBA(0x3A, 0x2E, 0x22, 0x1A));
    rmDrawRect(SHELF_RAIL_W, LIB_FTR_Y, 640 - SHELF_RAIL_W, 1, LAND_RULE);
    {
        int hx = CONTENT_X;
        if (homeView == 0) {
            hx += shelfHint(hx, LIB_FTR_TEXT, 0, "Home");
        } else if (total > 0) {
            hx += shelfHint(hx, LIB_FTR_TEXT, 0,
                            homeFocus >= 2 ? "Open" : homeFocus == 0 ? "Resume" : "Play");
            shelfHint(hx, LIB_FTR_TEXT, 2, "Details");
        }
    }
    shelfDrawRail(guiShelfPageIndex());
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

/** A panel, drifting on its own phase. Returns the drift so the caller can
 *  offset its contents by the same amount -- a card that moves while its text
 *  stays put is worse than one that does not move at all. */
/** A panel on the dashboard: an outline and a label, not a filled slab.
 *
 *  Square corners, because the page is a drawing now and a drawing does not
 *  round its boxes. The fill is a barely-there wash rather than a block, so the
 *  ground reads as one sheet with things drawn on it instead of as cards
 *  floating over a background. */
static int homeCard(int x, int y, int w, int h, const char *label, int phase)
{
    int dy = shelfFloat(phase, 2);
    rmDrawRect(x, y + dy, w, h, GS_SETREG_RGBA(0x3A, 0x2E, 0x22, 0x0C));
    rmDrawRect(x, y + dy, w, 1, LAND_RULE);
    rmDrawRect(x, y + dy + h - 1, w, 1, LAND_RULE);
    rmDrawRect(x, y + dy, 1, h, LAND_RULE);
    rmDrawRect(x + w - 1, y + dy, 1, h, LAND_RULE);
    if (label)
        fntRenderString(appsFontHead, x + 10, y + dy + 7, ALIGN_NONE, 0, 0, label,
                        LAND_DIM);
    return dy;
}

static void homeDrawDash(void)
{
    int total = oplRecentCount();
    int hh = 0, mm = 0, days = 0, haveClock;
    int i, y, dyA = 0, dyB = 0, dyC = 0, dyH;
    char buf[96], t[24], when[24];

    homeScan();
    haveClock = homeLocalTime(&hh, &mm, &days);
    if (homeSel >= total) homeSel = total - 1;
    if (homeSel < 1)      homeSel = 1;
    if (!homeCover) {
        for (i = 0; i < OPL_RECENT_MAX; i++)
            homeCovId[i] = homeCovUid[i] = homeHeroId[i] = homeHeroUid[i] = -1;
        /* BG is the hero card's own background; COVHD serves both the inset on
           that card and the strip beneath it, so one cache covers both. */
        homeHero  = cacheInitCache(2, "ART", 1, "BG", 2);
        homeCover = cacheInitCache(3, "ART", 1, "COVHD", HOME_TILES + 2);
    }

    rmDrawRect(0, 0, 640, 480, LAND_BG);

    /* ---- header ---- */
    fntRenderString(appsFontHead, HOME_M, 14, ALIGN_NONE, 0, 0, "HOME",
                    LAND_MUTE);
    {
        int rx = 640 - HOME_M, w;
        if (haveClock) {
            snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);
            w = fntCalcDimensions(appsFontSmall, buf);
            rmDrawRect(rx - w - 16, 8, w + 16, 20, GS_SETREG_RGBA(0x3A, 0x2E, 0x22, 0x14));
            fntRenderString(appsFontSmall, rx - w - 8, 12, ALIGN_NONE, 0, 0, buf,
                            LAND_TEXT);
            rx -= w + 26;
        }
        {
            const char *net = (gNetworkStartup == 0) ? "NET" : "OFFLINE";
            u64 col = (gNetworkStartup == 0) ? GS_SETREG_RGBA(0x64, 0xC8, 0x78, 0x80)
                                             : GS_SETREG_RGBA(0x6E, 0x76, 0x81, 0x80);
            w = fntCalcDimensions(appsFontSmall, net);
            rmDrawRect(rx - w - 26, 8, w + 26, 20, GS_SETREG_RGBA(0x3A, 0x2E, 0x22, 0x14));
            rmDrawRect(rx - w - 18, 16, 5, 5, col);
            fntRenderString(appsFontSmall, rx - w - 8, 12, ALIGN_NONE, 0, 0, net, col);
        }
    }

    /* ---- left column: clock, most played, system ---- */
    dyA = homeCard(HOME_M, 40, HOME_COL_W, 122, "CLOCK", 0);
    if (haveClock) {
        const char *greet = hh < 5 ? "Good night" : hh < 12 ? "Good morning"
                          : hh < 18 ? "Good afternoon" : "Good evening";
        /* No wash. The hour-tinted gradient here was a holdover from the dark
           dashboard, where it kept a static panel from reading as dead. On the
           schematic sheet it reads as a smudge instead -- a drawing does not
           shade its boxes, and the greeting already carries the time of day. */
        int hw;
        fntRenderString(appsFontSmall, HOME_M + 12, 66 + dyA, ALIGN_NONE, 0, 0, greet,
                        LAND_MUTE);
        /* Hours, colon and minutes drawn separately so the colon can breathe
           without the digits moving. A blink that shifts the time is worse than
           no blink. */
        snprintf(buf, sizeof(buf), "%02d", hh);
        fntRenderString(appsFontBig, HOME_M + 10, 86 + dyA, ALIGN_NONE, 0, 0, buf,
                        LAND_TEXT);
        hw = fntCalcDimensions(appsFontBig, buf);
        fntRenderString(appsFontBig, HOME_M + 10 + hw, 86 + dyA, ALIGN_NONE, 0, 0, ":",
                        GS_SETREG_RGBA(0x3A, 0x2E, 0x22, 0x30 + shelfPulse(2 * FPS) / 4));
        snprintf(buf, sizeof(buf), "%02d", mm);
        fntRenderString(appsFontBig, HOME_M + 10 + hw + fntCalcDimensions(appsFontBig, ":"),
                        86 + dyA, ALIGN_NONE, 0, 0, buf, LAND_TEXT);
    } else {
        fntRenderString(appsFontSmall, HOME_M + 12, 90 + dyA, ALIGN_NONE, 0, 0,
                        "RTC unreadable", LAND_DIM);
    }

    /* Two destinations rather than a ranking. This column had the only space on
       the page for something actionable, and a most-played list is something to
       read at rather than something to press. */
    {
        static const char *lbl[2] = {"Library", "Apps"};
        int bw = (HOME_COL_W - 10) / 2, bh = 46, by = 172;
        int k;
        dyB = shelfFloat(90, 2);
        for (k = 0; k < 2; k++) {
            int bx = HOME_M + k * (bw + 10);
            int on = (homeFocus == 2 + k);
            int lf = on ? 2 : 0;
            u64 face = on ? GS_SETREG_RGBA(0x3A, 0x2E, 0x22, 0x2E)
                          : GS_SETREG_RGBA(0x3A, 0x2E, 0x22, 0x0C);
            int tw2 = fntCalcDimensions(appsFontSmall, lbl[k]);

            rmDrawRect(bx - lf, by + dyB - lf, bw + 2 * lf, bh + 2 * lf, face);
            rmDrawRect(bx - lf, by + dyB - lf, bw + 2 * lf, 1, LAND_RULE);
            rmDrawRect(bx - lf, by + dyB - lf + bh + 2 * lf - 1, bw + 2 * lf, 1, LAND_RULE);
            rmDrawRect(bx - lf, by + dyB - lf, 1, bh + 2 * lf, LAND_RULE);
            rmDrawRect(bx - lf + bw + 2 * lf - 1, by + dyB - lf, 1, bh + 2 * lf, LAND_RULE);
            if (on) {
                u64 e = GS_SETREG_RGBA(0x3A, 0x2E, 0x22,
                                       0x50 + shelfPulse(3 * FPS) / 5);
                rmDrawRect(bx - lf + 3, by + dyB - lf, bw + 2 * lf - 6, 2, e);
                rmDrawRect(bx - lf + 3, by + dyB - lf + bh + 2 * lf - 2,
                           bw + 2 * lf - 6, 2, e);
                rmDrawRect(bx - lf, by + dyB - lf + 3, 2, bh + 2 * lf - 6, e);
                rmDrawRect(bx - lf + bw + 2 * lf - 2, by + dyB - lf + 3, 2,
                           bh + 2 * lf - 6, e);
            }
            /* The rail's own glyph, so the button and the destination it leads
               to are recognisably the same thing. */
            if (k == 0)
                railGrid(bx + bw / 2 - 6, by + dyB + 10, on ? LAND_INK : LAND_MUTE);
            else
                railPanel(bx + bw / 2 - 6, by + dyB + 10, on ? LAND_INK : LAND_MUTE);
            fntRenderString(appsFontSmall, bx + (bw - tw2) / 2, by + dyB + 28,
                            ALIGN_NONE, 0, 0, lbl[k], on ? LAND_INK : LAND_MUTE);
        }
    }

    dyC = homeCard(HOME_M, 236, HOME_COL_W, 178, "SYSTEM", 190);
    y = 266 + dyC;
    homeFormatTime(t, sizeof(t), homeTotalMinutes);
    snprintf(buf, sizeof(buf), "%d of %d played", homeTotalPlayed, homeTotalTitles);
    fntRenderString(appsFontSmall, HOME_M + 12, y, ALIGN_NONE, 0, 0, buf,
                    LAND_MUTE);
    y += 20;
    if (t[0]) {
        snprintf(buf, sizeof(buf), "%s total", t);
        fntRenderString(appsFontSmall, HOME_M + 12, y, ALIGN_NONE, 0, 0, buf,
                        LAND_MUTE);
        y += 20;
    }
    fntRenderString(appsFontSmall, HOME_M + 12, y, ALIGN_NONE, 0, 0, OPL_VERSION,
                    LAND_DIM);

    /* ---- right column: continue, recently played ---- */
    if (total <= 0) {
        homeCard(HOME_R_X, 40, HOME_R_W, 108, "CONTINUE PLAYING", 45);
        fntRenderString(FNT_DEFAULT, HOME_R_X + 14, 76, ALIGN_NONE, 0, 0,
                        "Nothing played yet.",
                        LAND_TEXT);
        fntRenderString(appsFontSmall, HOME_R_X + 14, 106, ALIGN_NONE, 0, 0,
                        "Launch something and it appears here.",
                        LAND_DIM);
    } else {
        GSTEXTURE *bg  = homeArt(homeHero, homeHeroId, homeHeroUid, 0);
        GSTEXTURE *cov = homeArt(homeCover, homeCovId, homeCovUid, 0);
        config_set_t *cfg = homeCfgOf(0);
        const char *title = oplRecentTitle(0);
        int mins = 0, plays = 0;

        if (cfg) {
            configGetInt(cfg, "Playtime", &mins);
            configGetInt(cfg, "PlayCount", &plays);
        }
        homeWhen(when, sizeof(when), cfg, days);

        /* Focus lifts: the card grows a few pixels on every side and drifts a
           little more, which reads as coming forward rather than as changing
           size. Anything larger and the layout appears to reflow. */
        int lift = (homeFocus == 0) ? 3 : 0;
        int hx0 = HOME_R_X - lift, hy0, hw0 = HOME_R_W + 2 * lift, hh0 = HOME_HERO_H + 2 * lift;
        dyH = shelfFloat(45, lift ? 3 : 2);
        hy0 = 40 - lift + dyH;

        /* The card *is* the artwork. A cover thumbnail on a flat panel was a list row
           wearing a hero's label; the BG is what the game looks like. */
        rmDrawRect(hx0, hy0, hw0, hh0, GS_SETREG_RGBA(0x3A, 0x2E, 0x22, 0x18));
        if (bg)
            rmDrawPixmap(bg, hx0, hy0, ALIGN_NONE, hw0, hh0, SCALING_NONE, gDefaultCol);
        /* Scrim from the bottom, so the type sits on something whatever the art
           does. Everything above it has to stay readable over a white sky. */
        shelfGradV(hx0, hy0 + hh0 - 92, hw0, 92,
                   0x04, 0x76, GS_SETREG_RGBA(0x2A, 0x22, 0x18, 0));

        if (cov)
            rmDrawPixmap(cov, hx0 + hw0 - 62, hy0 + 12, ALIGN_NONE, 56, 84,
                         SCALING_RATIO, gDefaultCol);

        if (homeFocus == 0) {
            u64 e = GS_SETREG_RGBA(0x3A, 0x2E, 0x22, 0x50 + shelfPulse(3 * FPS) / 5);
            rmDrawRect(hx0, hy0, hw0, 2, e);
            rmDrawRect(hx0, hy0 + hh0 - 2, hw0, 2, e);
            rmDrawRect(hx0, hy0, 2, hh0, e);
            rmDrawRect(hx0 + hw0 - 2, hy0, 2, hh0, e);
        }

        fntRenderString(appsFontHead, hx0 + 14, hy0 + hh0 - 84,
                        ALIGN_NONE, 0, 0,
                        "CONTINUE PLAYING",
                        GS_SETREG_RGBA(0xE8, 0xE2, 0xD4, 0x80));
        if (title)
            fntRenderString(FNT_DEFAULT, hx0 + 14, hy0 + hh0 - 68,
                            ALIGN_NONE, HOME_R_W - 84, 24, title,
                            GS_SETREG_RGBA(0xF6, 0xF2, 0xE8, 0x80));
        homeFormatTime(t, sizeof(t), mins);
        buf[0] = '\0';
        if (when[0] && t[0])  snprintf(buf, sizeof(buf), "Last played %s  \xc2\xb7  %s total", when, t);
        else if (when[0])     snprintf(buf, sizeof(buf), "Last played %s", when);
        else if (t[0])        snprintf(buf, sizeof(buf), "%s total", t);
        if (plays > 0) {
            char pl[24];
            snprintf(pl, sizeof(pl), "%s%d launch%s", buf[0] ? "  \xc2\xb7  " : "",
                     plays, plays == 1 ? "" : "es");
            strncat(buf, pl, sizeof(buf) - strlen(buf) - 1);
        }
        if (buf[0])
            fntRenderString(appsFontSmall, hx0 + 14, hy0 + hh0 - 38,
                            ALIGN_NONE, HOME_R_W - 28, 14, buf,
                            GS_SETREG_RGBA(0xD8, 0xD0, 0xC0, 0x80));
                fntRenderString(appsFontHead, HOME_R_X, 188, ALIGN_NONE, 0, 0,
                        "RECENTLY PLAYED", LAND_DIM);
        {
            /* Work back from the space, not forward from a guess. The row has
               to fill HOME_R_W, so the *drawn* width is what the arithmetic
               starts from; the declared width follows from it. Doing it the
               other way round left a quarter of the row empty, because a
               declared width is a third larger than what gets drawn. */
            int dw = (HOME_R_W - (HOME_COLS - 1) * HOME_GAP) / HOME_COLS;
            int tw = rmWidthUnscaled(dw);
            /* SCALING_RATIO makes declared dimensions display true, so a
               cover is simply 2:3 in declared units -- the same 74x111 the
               theme's own grid uses. */
            int th = tw * 3 / 2;
            for (i = 0; i < HOME_TILES && i + 1 < total; i++) {
                /* Each tile drifts on its own phase -- staggered by index so a
                   row does not move as one bar -- and the focused one lifts. */
                int idx = i + 1;               /* entry 0 is the hero */
                int on = (homeFocus == 1 && idx == homeSel);
                int lift = on ? 3 : 0;
                int cx = HOME_R_X + (i % HOME_COLS) * (dw + HOME_GAP) - lift;
                int ty = 202 + shelfFloat(idx * 37, on ? 3 : 2) - lift;
                int tww = dw + 2 * lift, thh = th + 2 * lift;
                GSTEXTURE *bg2 = homeArt(homeCover, homeCovId, homeCovUid, idx);
                config_set_t *c2 = homeCfgOf(idx);
                int m2 = 0;

                if (bg2) rmDrawPixmap(bg2, cx, ty, ALIGN_NONE, rmWidthUnscaled(tww), thh,
                                      SCALING_RATIO, gDefaultCol);
                /* Square, deliberately. A cover is a printed object with square
                   corners; rounding it makes it look like a UI tile rather than
                   like the thing it is a picture of. The panels around it round,
                   the artwork does not. */
                else    rmDrawRect(cx, ty, tww, thh, GS_SETREG_RGBA(0x16, 0x1A, 0x20, 0x80));
                if (idx == homeSel) {
                    u64 e = GS_SETREG_RGBA(0x3A, 0x2E, 0x22,
                                           on ? 0x50 + shelfPulse(3 * FPS) / 5 : 0x60);
                    rmDrawRect(cx, ty, tww, 2, e);
                    rmDrawRect(cx, ty + thh - 2, tww, 2, e);
                    rmDrawRect(cx, ty, 2, thh, e);
                    rmDrawRect(cx + tww - 2, ty, 2, thh, e);
                }
                {
                    const char *nm = oplRecentTitle(idx);
                    if (nm)
                        fntRenderString(appsFontSmall, cx + lift, ty + thh + 6, ALIGN_NONE,
                                        dw, 12, nm,
                                        i == homeSel ? LAND_TEXT : LAND_MUTE);
                }
                if (c2) configGetInt(c2, "Playtime", &m2);
                homeWhen(when, sizeof(when), c2, days);
                homeFormatTime(t, sizeof(t), m2);
                buf[0] = '\0';
                if (when[0] && t[0]) snprintf(buf, sizeof(buf), "%s \xc2\xb7 %s", when, t);
                else if (when[0])    snprintf(buf, sizeof(buf), "%s", when);
                else if (t[0])       snprintf(buf, sizeof(buf), "%s", t);
                if (buf[0])
                    fntRenderString(appsFontSmall, cx + lift, ty + thh + 20, ALIGN_NONE,
                                    dw, 12, buf, LAND_DIM);
            }
        }
    }



    shelfDrawRail(guiShelfPageIndex());
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

    /* No Back. Home, Library and Apps are top-level views, not somewhere you
       arrived from -- there is nothing above them to return to, and a Back that
       drops you onto the classic list makes the shell feel like a detour off it
       rather than the thing you booted into. L3 opens the panel; the panel is
       the whole of the navigation. Details and Settings keep theirs, because
       those you genuinely did arrive at from somewhere. */
    if (total <= 0)
        return;

    if (homeView == 0) {
        /* The landing has one control. Cross is the same as Down here, because
           a page that says DOWN FOR HOME should not also refuse the button
           everything else on this console uses to go forward. */
        if (getKeyOn(KEY_DOWN) || getKeyOn(KEY_CROSS))
            homeView = 1;
        return;
    }

    if (getKeyOn(KEY_UP)) {
        if (homeFocus >= 2 || homeFocus == 0)
            homeView = 0;          /* already at the top of a column */
        else
            homeFocus = 0;
    } else if (getKeyOn(KEY_DOWN) && homeFocus < 2 && total > 1) {
        homeFocus = 1;
    } else if (getKeyOn(KEY_LEFT)) {
        /* Left crosses into the button column, then walks it. */
        if (homeFocus < 2)          homeFocus = 3;
        else if (homeFocus == 3)    homeFocus = 2;
        else if (homeFocus == 1 && homeSel > 1) homeSel--;
    } else if (getKeyOn(KEY_RIGHT)) {
        if (homeFocus == 2)         homeFocus = 3;
        else if (homeFocus == 3)    homeFocus = 0;
        else if (homeFocus == 1 && homeSel < total - 1 && homeSel < HOME_TILES)
            homeSel++;
    }
    else if (homeFocus >= 2 && getKeyOn(KEY_CROSS)) {
        /* Straight to the page. guiSwitchScreen sets the screen the rail reads
           for its own marker, so the sidebar follows without being told. */
        guiSwitchScreen(homeFocus == 2 ? GUI_SCREEN_SHELF_LIBRARY
                                       : GUI_SCREEN_SHELF_APPS);
    }
    else if (getKeyOn(KEY_SQUARE)) {
        idx = homeIndexOf(oplRecentStartup(homeFocus == 0 ? 0 : homeSel));
        if (idx >= 0 && menuSelectIndex(idx))
            guiSwitchScreen(GUI_SCREEN_INFO);
    } else if (getKeyOn(KEY_CROSS)) {
        item_list_t *list = menuGetActiveList();
        idx = homeIndexOf(oplRecentStartup(homeFocus == 0 ? 0 : homeSel));
        if (idx >= 0 && list && list->itemLaunch && list->itemGetConfig)
            list->itemLaunch(list, idx, list->itemGetConfig(list, idx));
    }
}
