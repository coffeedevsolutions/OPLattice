/*
  Copyright 2010, Volca
  Licenced under Academic Free License version 3.0
  Review OpenUsbLd README & LICENSE files for further details.
*/

#include <stdio.h>
#include <kernel.h>

#include "include/opl.h"
#include "include/renderman.h"
#include "include/ioman.h"
#include "include/textures.h"

// Allocateable space in vram, as indicated in GsKit's code
#define __VRAM_SIZE 4194304

GSGLOBAL *gsGlobal;
s32 guiThreadID;

static int order;
static short int vmode = -1;
static u8 hires = 0;
static u8 guiWakeupCount;
static int vsync_id = -1;

#define NUM_RM_VMODES 14
#define RM_VMODE_AUTO 0

// RM Vmode -> GS Vmode conversion table
struct rm_mode
{
    char mode;
    char hsync; // In KHz
    short int width;
    short int height;
    short int passes;
    short int VCK;
    short int interlace;
    short int field;
    short int aratio;
    short int PAR1; // Pixel Aspect Ratio 1 (For video modes with non-square pixels, like PAL/NTSC)
    short int PAR2; // Pixel Aspect Ratio 2 (For video modes with non-square pixels, like PAL/NTSC)
};

// clang-format off
static struct rm_mode rm_mode_table[NUM_RM_VMODES] = {
    // 24 bit color mode with black borders
    {-1,                 16,  640,   -1,  1, 4, GS_INTERLACED,    GS_FIELD, RM_ARATIO_4_3, -1, 15}, // AUTO
    {GS_MODE_PAL,        16,  640,  512,  1, 4, GS_INTERLACED,    GS_FIELD, RM_ARATIO_4_3, 16, 15}, // PAL@50Hz
    {GS_MODE_NTSC,       16,  640,  448,  1, 4, GS_INTERLACED,    GS_FIELD, RM_ARATIO_4_3, 14, 15}, // NTSC@60Hz
    {GS_MODE_DTV_480P,   31,  640,  448,  1, 2, GS_NONINTERLACED, GS_FRAME, RM_ARATIO_4_3, 14, 15}, // DTV480P@60Hz
    {GS_MODE_DTV_576P,   31,  640,  512,  1, 2, GS_NONINTERLACED, GS_FRAME, RM_ARATIO_4_3, 16, 15}, // DTV576P@50Hz
    {GS_MODE_VGA_640_60, 31,  640,  480,  1, 2, GS_NONINTERLACED, GS_FRAME, RM_ARATIO_4_3,  1,  1}, // VGA640x480@60Hz
    // 24 bit color mode full screen, multi-pass (2 passes, HIRES)
    {GS_MODE_PAL,        16,  704,  576,  2, 4, GS_INTERLACED,    GS_FIELD, RM_ARATIO_4_3, 12, 11}, // PAL@50Hz
    {GS_MODE_NTSC,       16,  704,  480,  2, 4, GS_INTERLACED,    GS_FIELD, RM_ARATIO_4_3, 10, 11}, // NTSC@60Hz
    {GS_MODE_DTV_480P,   31,  704,  480,  2, 2, GS_NONINTERLACED, GS_FRAME, RM_ARATIO_4_3, 10, 11}, // DTV480P@60Hz
    {GS_MODE_DTV_576P,   31,  704,  576,  2, 2, GS_NONINTERLACED, GS_FRAME, RM_ARATIO_4_3, 12, 11}, // DTV576P@50Hz
    // 16 bit color mode full screen, multi-pass (3 passes, HIRES)
    {GS_MODE_DTV_720P,   31, 1280,  720,  3, 1, GS_NONINTERLACED, GS_FRAME, RM_ARATIO_16_9, 1,  1}, // HDTV720P@60Hz
    {GS_MODE_DTV_1080I,  31, 1920, 1080,  3, 1, GS_INTERLACED,    GS_FRAME, RM_ARATIO_16_9, 1,  1}, // HDTV1080I@60Hz
    // 24 bit color mode for older systems (non-interlaced, low resolution)
    {GS_MODE_PAL,        16,  640,  256,  1, 4, GS_NONINTERLACED, GS_FRAME, RM_ARATIO_4_3,  8, 15}, // PAL@50Hz
    {GS_MODE_NTSC,       16,  640,  224,  1, 4, GS_NONINTERLACED, GS_FRAME, RM_ARATIO_4_3,  7, 15}, // NTSC@60Hz
};
// clang-format on

// Display Aspect Ratio
static int iAspectWidth = 4;
static enum rm_aratio DAR = RM_ARATIO_4_3;

// Display dimensions after overscan compensation
static int iDisplayWidth;
static int iDisplayHeight;
static int iDisplayXOff;
static int iDisplayYOff;

// Transposition values including overscan compensation
static float fRenderXOff = 0.0f;
static float fRenderYOff = 0.0f;
/* The overscan inset, kept separate so a scroll can be added on top of it
   without losing it. */
static float fRenderYBase = 0.0f;
static float fRenderXBase = 0.0f;

const u64 gColWhite = GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80);  // Alpha 0x80 -> solid white
const u64 gColBlack = GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x80);  // Alpha 0x80 -> solid black
const u64 gColDarker = GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x60); // Alpha 0x60 -> transparent overlay color
const u64 gColFocus = GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x50);  // Alpha 0x50 -> transparent overlay color

const u64 gDefaultCol = GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80); // Special color for texture multiplication
const u64 gDefaultAlpha = GS_SETREG_ALPHA(0, 1, 0, 1, 0);

void rmInvalidateTexture(GSTEXTURE *txt)
{
    gsKit_TexManager_invalidate(gsGlobal, txt);
}

void rmUnloadTexture(GSTEXTURE *txt)
{
    gsKit_TexManager_free(gsGlobal, txt);
}

/* Bytes bound this frame, and the count of distinct binds. Reset per frame.
 *
 * APPROXIMATE, and it has to stay labelled that way. gsKit's block list is
 * file-scope in gsTexManager.c and reachable only through an internal symbol,
 * so nothing OPL-side can observe an eviction. This counts what we ask for; the
 * manager may already hold some of it, and may have thrown some away. It is a
 * diagnostic, not a contract -- useful for "is this page anywhere near the
 * pool", useless for "exactly how full is VRAM". */
static unsigned int vramBoundBytes;
static int vramBoundCount;

unsigned int rmVramBoundBytes(void) { return vramBoundBytes; }
int rmVramBoundCount(void) { return vramBoundCount; }

static void rmAccountBind(GSTEXTURE *txt)
{
    if (!txt)
        return;
    vramBoundBytes += gsKit_texture_size(txt->Width, txt->Height, txt->PSM);
    if (txt->Clut)
        vramBoundBytes += gsKit_texture_size(16, 16, GS_PSM_CT32);
    vramBoundCount++;
}

/* Upload a texture before it is drawn.
 *
 * gsKit has no asynchronous path: both transfer routines put the upload in the
 * current GS packet, so this means *earlier in this frame's draw list*, not
 * during idle time. What it buys is that a texture needed at the bottom of a
 * grid is already resident when the quad for it is issued, instead of causing a
 * transfer mid-draw.
 *
 * One caveat worth knowing before leaning on it: bind increments the use count,
 * and gsKit's eviction predictor scores by binds per frame. A texture prefetched
 * and then not drawn ends the frame looking as wanted as one that was drawn
 * twice. At a row of lookahead that is noise; at aggressive lookahead it would
 * start costing residency for things actually on screen. */
void rmPrefetchTexture(GSTEXTURE *txt)
{
    if (!txt || !txt->Mem)
        return;
    gsKit_TexManager_bind(gsGlobal, txt);
    rmAccountBind(txt);
}

void rmStartFrame(void)
{
    if (hires == 0)
        gsKit_clear(gsGlobal, gColBlack);

    vramBoundBytes = 0;
    vramBoundCount = 0;
    order = 0;
}

void rmEndFrame(void)
{
    if (hires) {
        gsKit_hires_sync(gsGlobal);
        gsKit_hires_flip(gsGlobal);
    } else {
        gsKit_set_finish(gsGlobal);
        gsKit_queue_exec(gsGlobal);

        // Wait for draw ops to finish
        gsKit_finish();

        if (!gsGlobal->FirstFrame) {
            SleepThread();
            guiWakeupCount = 0;

            if (gsGlobal->DoubleBuffering == GS_SETTING_ON) {
                GS_SET_DISPFB2(gsGlobal->ScreenBuffer[gsGlobal->ActiveBuffer & 1] / 8192,
                               gsGlobal->Width / 64, gsGlobal->PSM, 0, 0);

                gsGlobal->ActiveBuffer ^= 1;
            }
        }

        gsKit_setactive(gsGlobal);
    }

    gsKit_TexManager_nextFrame(gsGlobal);
}

static int rmOnVSync(int cause)
{
    if (guiWakeupCount == 0) {
        guiWakeupCount = 1;
        iWakeupThread(guiThreadID);
    }

    ExitHandler();
    return 0;
}

void rmInit()
{
    short int mode = gsKit_check_rom();

    rm_mode_table[RM_VMODE_AUTO].mode = mode;
    rm_mode_table[RM_VMODE_AUTO].height = (mode == GS_MODE_PAL) ? 512 : 448;
    rm_mode_table[RM_VMODE_AUTO].PAR1 = (mode == GS_MODE_PAL) ? 16 : 14;

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);

    // Initialize the DMAC
    dmaKit_chan_init(DMA_CHANNEL_GIF);

    rmSetMode(1);

    order = 0;

    guiWakeupCount = 0;
    guiThreadID = GetThreadId();
}

int rmSetMode(int force)
{
    if (gVMode < RM_VMODE_AUTO || gVMode >= NUM_RM_VMODES)
        gVMode = RM_VMODE_AUTO;

    // we don't want to set the vmode without a reason...
    int changed = (vmode != gVMode || force);
    if (changed) {
        // Cleanup previous gsKit instance
        if (vmode >= 0)
            rmEnd();

        vmode = gVMode;
        hires = (rm_mode_table[vmode].passes > 1) ? 1 : 0;

        if (hires) {
            gsGlobal = gsKit_hires_init_global();
        } else {
            gsGlobal = gsKit_init_global();
            vsync_id = gsKit_add_vsync_handler(&rmOnVSync);
        }
        gsGlobal->Mode = rm_mode_table[vmode].mode;
        gsGlobal->Width = rm_mode_table[vmode].width;
        gsGlobal->Height = rm_mode_table[vmode].height;
        gsGlobal->Interlace = rm_mode_table[vmode].interlace;
        gsGlobal->Field = rm_mode_table[vmode].field;
        gsGlobal->PSM = GS_PSM_CT24;
        // Higher resolution use too much VRAM
        // so automatically switch back to 16bit color depth
        if ((gsGlobal->Width * gsGlobal->Height) > (704 * 576))
            gsGlobal->PSM = GS_PSM_CT16S;
        gsGlobal->PSMZ = GS_PSMZ_16S;
        gsGlobal->ZBuffering = GS_SETTING_OFF;
        gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
        gsGlobal->DoubleBuffering = GS_SETTING_ON;
        gsGlobal->Dithering = GS_SETTING_ON;

        // Do not draw pixels if they are fully transparent
        // gsGlobal->Test->ATE  = GS_SETTING_ON;
        gsGlobal->Test->ATST = 7; // NOTEQUAL to AREF passes
        gsGlobal->Test->AREF = 0x00;
        gsGlobal->Test->AFAIL = 0; // KEEP

        if ((gsGlobal->Interlace == GS_INTERLACED) && (gsGlobal->Field == GS_FRAME))
            gsGlobal->Height /= 2;

        // Coordinate space ranges from 0 to 4096 pixels
        // Center the buffer in the coordinate space
        gsGlobal->OffsetX = ((4096 - gsGlobal->Width) / 2) * 16;
        gsGlobal->OffsetY = ((4096 - gsGlobal->Height) / 2) * 16;

        if (hires) {
            gsKit_hires_init_screen(gsGlobal, rm_mode_table[vmode].passes);
        } else {
            gsKit_init_screen(gsGlobal);
            gsKit_mode_switch(gsGlobal, GS_ONESHOT);
        }

        gsKit_set_test(gsGlobal, GS_ZTEST_OFF);
        gsKit_set_primalpha(gsGlobal, gDefaultAlpha, 0);

        /* The framebuffers are allocated by now, so CurrentPointer is where the
         * texture pool starts and everything above it is what gsKit will hand
         * out. Check the per-texture cap fits inside it -- see texCheckBudget. */
        texCheckBudget(__VRAM_SIZE - gsGlobal->CurrentPointer);

        // reset the contents of the screen to avoid garbage being displayed
        if (hires) {
            gsKit_hires_sync(gsGlobal);
            gsKit_hires_flip(gsGlobal);
        } else {
            gsKit_clear(gsGlobal, gColBlack);
            gsKit_sync_flip(gsGlobal);
        }

        LOG("RENDERMAN New vmode: %d, %d x %d\n", vmode, gsGlobal->Width, gsGlobal->Height);
    }

    rmSetDisplayOffset(gXOff, gYOff);
    rmSetOverscan(gOverscan);
    rmSetAspectRatio((gWideScreen == 0) ? RM_ARATIO_4_3 : RM_ARATIO_16_9);

    return changed;
}

void rmGetScreenExtentsNative(int *w, int *h)
{
    *w = iDisplayWidth;
    *h = iDisplayHeight;
}

void rmGetScreenExtents(int *w, int *h)
{
    // Emulate 640x480 (square pixel VGA)
    *w = 640;
    *h = 480;
}

void rmEnd(void)
{
    if (hires) {
        gsKit_hires_deinit_global(gsGlobal);
    } else {
        gsKit_deinit_global(gsGlobal);
        gsKit_remove_vsync_handler(vsync_id);
    }

    vmode = -1;
}

#define X_SCALE(x) (((x)*iDisplayWidth) / 640)
#define Y_SCALE(y) (((y)*iDisplayHeight) / 480)
/** If txt is null, don't use DIM_UNDEF size */
static void rmSetupQuad(GSTEXTURE *txt, int x, int y, short aligned, int w, int h, short scaled, u64 color, rm_quad_t *q)
{
    if (w == DIM_UNDEF)
        w = txt->Width;
    if (h == DIM_UNDEF)
        h = txt->Height;

    // Legacy scaling
    x = X_SCALE(x);
    y = Y_SCALE(y);
    if (scaled & SCALING_RATIO)
        w = X_SCALE(w * iAspectWidth) >> 2;
    else
        w = X_SCALE(w);
    h = Y_SCALE(h);

    // Align LEFT/HCENTER/RIGHT
    if (aligned & ALIGN_HCENTER)
        q->ul.x = x - (w >> 1);
    else if (aligned & ALIGN_RIGHT)
        q->ul.x = x - w;
    else
        q->ul.x = x;
    q->br.x = q->ul.x + w;

    // Align TOP/VCENTER/BOTTOM
    if (aligned & ALIGN_VCENTER)
        q->ul.y = y - (h >> 1);
    else if (aligned & ALIGN_BOTTOM)
        q->ul.y = y - h;
    else
        q->ul.y = y;
    q->br.y = q->ul.y + h;

    q->color = color;
    if (txt) {
        q->txt = txt;
        q->ul.u = 0;
        q->ul.v = 0;
        q->br.u = txt->Width;
        q->br.v = txt->Height;
    }
}

void rmDrawQuad(rm_quad_t *q)
{
    if ((q->txt->PSM == GS_PSM_CT32) || (q->txt->Clut && q->txt->ClutPSM == GS_PSM_CT32)) {
        gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
        gsKit_set_test(gsGlobal, GS_ATEST_ON);
    } else {
        gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
        gsKit_set_test(gsGlobal, GS_ATEST_OFF);
    }

    gsKit_TexManager_bind(gsGlobal, q->txt);
    rmAccountBind(q->txt);
    gsKit_prim_sprite_texture(gsGlobal, q->txt,
                              q->ul.x + fRenderXOff, q->ul.y + fRenderYOff,
                              q->ul.u, q->ul.v,
                              q->br.x + fRenderXOff, q->br.y + fRenderYOff,
                              q->br.u, q->br.v, order, q->color);
    order++;
}

void rmDrawPixmap(GSTEXTURE *txt, int x, int y, short aligned, int w, int h, short scaled, u64 color)
{
    rm_quad_t quad;
    rmSetupQuad(txt, x, y, aligned, w, h, scaled, color, &quad);
    rmDrawQuad(&quad);
}

void rmDrawOverlayPixmap(GSTEXTURE *overlay, int x, int y, short aligned, int w, int h, short scaled, u64 color,
                         GSTEXTURE *inlay, int ulx, int uly, int urx, int ury, int blx, int bly, int brx, int bry)
{
    rm_quad_t quad;
    rmSetupQuad(overlay, x, y, aligned, w, h, scaled, color, &quad);
    ulx = X_SCALE(ulx * iAspectWidth) >> 2;
    urx = X_SCALE(urx * iAspectWidth) >> 2;
    blx = X_SCALE(blx * iAspectWidth) >> 2;
    brx = X_SCALE(brx * iAspectWidth) >> 2;
    uly = Y_SCALE(uly);
    ury = Y_SCALE(ury);
    bly = Y_SCALE(bly);
    bry = Y_SCALE(bry);

    if ((inlay->PSM == GS_PSM_CT32) || (inlay->Clut && inlay->ClutPSM == GS_PSM_CT32))
        gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
    else
        gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;

    gsKit_TexManager_bind(gsGlobal, inlay);
    rmAccountBind(inlay);
    gsKit_prim_quad_texture(gsGlobal, inlay,
                            quad.ul.x + ulx + fRenderXOff, quad.ul.y + uly + fRenderYOff,
                            0.0f, 0.0f,
                            quad.ul.x + urx + fRenderXOff, quad.ul.y + ury + fRenderYOff,
                            inlay->Width, 0.0f,
                            quad.ul.x + blx + fRenderXOff, quad.ul.y + bly + fRenderYOff,
                            0.0f, inlay->Height,
                            quad.ul.x + brx + fRenderXOff, quad.ul.y + bry + fRenderYOff,
                            inlay->Width, inlay->Height, order, gDefaultCol);
    order++;

    rmDrawQuad(&quad);
}

/* The width, *in virtual units*, that a SCALING_RATIO element declared `w` wide
   will occupy: three quarters in 16:9, unchanged in 4:3.

   Virtual, deliberately. Every position handed to rmDrawRect or rmDrawPixmap is
   virtual and gets X_SCALE applied inside them, so a layout built from an
   already-X_SCALE'd width mixes the two spaces and drifts by the overscan
   inset -- iDisplayWidth is the display width *minus* 2 * iDisplayXOff, not 640,
   so X_SCALE is not the identity it looks like. */
int rmWidthScaled(int w)
{
    return (w * iAspectWidth) >> 2;
}

/* The inverse: the declared width needed to draw `w` virtual pixels. Layout that
   starts from the space available -- "five of these must fill 372" -- has to
   work back to a declared width, and doing that with a hardcoded 4/3 is only
   right in 16:9. */
/* A measured text width, brought back into virtual space.
 *
 * fntCalcDimensions answers in physical pixels. Layout in this tree is virtual.
 * Anywhere a measured width advances a virtual pen -- a row of hints, a
 * right-to-left status bar -- it has to come back through here first, or the row
 * drifts by (X_SCALE - 1) times its own length. Alignment does not need this:
 * ALIGN_RIGHT and ALIGN_HCENTER subtract in physical space, where the width
 * already is. */
int rmUnscaleX(int x)
{
    return iDisplayWidth ? (x * 640) / iDisplayWidth : x;
}

int rmWidthUnscaled(int w)
{
    return (w << 2) / iAspectWidth;
}

void rmDrawRect(int x, int y, int w, int h, u64 color)
{
    float fx = X_SCALE(x) + fRenderXOff;
    float fy = Y_SCALE(y) + fRenderYOff;
    float fw = X_SCALE(w);
    float fh = Y_SCALE(h);

    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
    gsKit_prim_sprite(gsGlobal, fx, fy, fx + fw, fy + fh, order, color);
    order++;
}

/* A vertical alpha ramp as ONE primitive, interpolated by the GS.
 *
 * This replaces a stack of translucent sprites, and the stack was never going to
 * work. The first attempt used eleven bands and read as eleven flat rectangles.
 * The second went to 2px bands, and to survive Y_SCALE truncation at 448 lines
 * it drew each band 3 tall on a pitch of 2 -- which fixed the gaps at 448 and
 * created a worse artefact everywhere else: at 480 lines Y_SCALE is the
 * identity, so every band overlapped its neighbour by a pixel, and an overlap
 * of two translucent sprites is not the same colour as one. 1-(1-a)^2 against a
 * is a large step, so the ramp came out as alternating light and dark scanlines.
 * That is what "the bars still aren't connected" was: not gaps, seams.
 *
 * Stacking translucent sprites cannot express a ramp, because the seam between
 * any two of them is either a gap or a double blend and never neither. So this
 * does what the hardware is for: one gouraud quad, RGBA interpolated per pixel
 * by the rasteriser, with the GS's own dithering on the result. No seams to get
 * wrong, no band count to tune, and one primitive instead of sixty.
 *
 * gsKit emits the four vertices as a triangle strip -- (v1,v2,v3), (v2,v3,v4) --
 * so the order here is top-left, top-right, bottom-left, bottom-right, and the
 * two top vertices carry c0 while the two bottom ones carry c1. */
void rmDrawGradV(int x, int y, int w, int h, u64 c0, u64 c1)
{
    float fx = X_SCALE(x) + fRenderXOff;
    float fy = Y_SCALE(y) + fRenderYOff;
    float fw = X_SCALE(w);
    float fh = Y_SCALE(h);

    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
    gsKit_prim_quad_gouraud(gsGlobal,
                            fx, fy,
                            fx + fw, fy,
                            fx, fy + fh,
                            fx + fw, fy + fh,
                            order, c0, c0, c1, c1);
    order++;
}

void rmDrawLine(int x1, int y1, int x2, int y2, u64 color)
{
    float fx1 = X_SCALE(x1) + fRenderXOff;
    float fy1 = Y_SCALE(y1) + fRenderYOff;
    float fx2 = X_SCALE(x2) + fRenderXOff;
    float fy2 = Y_SCALE(y2) + fRenderYOff;

    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
    gsKit_prim_line(gsGlobal, fx1, fy1, fx2, fy2, order, color);
    order++;
}

/* Shift everything drawn from here on by `dy` virtual pixels.
 *
 * Every primitive path adds fRenderYOff -- rects, lines, pixmaps, and text,
 * which reaches it through rmDrawQuad -- so one value moves a whole page without
 * any of its own coordinates knowing. That is the only reason a scrolling view
 * is cheap here rather than a rewrite of every y in the page.
 *
 * Not the same thing as rmSetDisplayOffset, which moves the scanout window and
 * exists for overscan. */
void rmSetScrollY(int dy)
{
    fRenderYOff = fRenderYBase + (float)Y_SCALE(dy);
}

/* The same trick on the other axis, for the sidebar.
 *
 * X_SCALE, not the aspect squeeze: this is a position, and positions narrow with
 * the display while element widths narrow by iAspectWidth. Using rmWidthScaled
 * here would push the page by three quarters of what the panel actually covers,
 * and the seam would drift with the aspect. */
void rmSetPanX(int dx)
{
    fRenderXOff = fRenderXBase + (float)X_SCALE(dx);
}

void rmSetDisplayOffset(int x, int y)
{
    gsKit_set_display_offset(gsGlobal, x * rm_mode_table[vmode].VCK, y);
}

void rmSetAspectRatio(enum rm_aratio dar)
{
    DAR = dar;

    switch (DAR) {
        case RM_ARATIO_4_3:
            iAspectWidth = 4; // width = width * 4 / 4
            break;
        case RM_ARATIO_16_9:
            iAspectWidth = 3; // width = width * 3 / 4
            break;
    };
}

int rmWideScale(int x)
{
    return (x * iAspectWidth) >> 2;
}

// Get the pixel aspect ratio (how wide or narrow are the pixels?)
float rmGetPAR()
{
    float fPAR = (float)rm_mode_table[vmode].PAR2 / (float)rm_mode_table[vmode].PAR1;

    // In anamorphic mode the pixels are stretched to 16:9
    if ((DAR == RM_ARATIO_16_9) && (rm_mode_table[vmode].aratio == RM_ARATIO_4_3))
        fPAR *= 0.75f;

    // In interlaced frame mode, the pixel are (virtually) twice as high
    if ((gsGlobal->Interlace == GS_INTERLACED) && (gsGlobal->Field == GS_FRAME))
        fPAR *= 2.0f;

    return fPAR;
}

// Get interfaced frame mode
int rmGetInterlacedFrameMode()
{
    if ((gsGlobal->Interlace == GS_INTERLACED) && (gsGlobal->Field == GS_FRAME))
        return 1;

    return 0;
}

int rmScaleX(int x)
{
    return X_SCALE(x);
}

int rmScaleY(int y)
{
    return Y_SCALE(y);
}

int rmUnScaleX(int x)
{
    return (x * 640) / iDisplayWidth;
}

int rmUnScaleY(int y)
{
    return (y * 480) / iDisplayHeight;
}

void rmSetOverscan(int overscan)
{
    iDisplayXOff = (gsGlobal->Width * overscan) / (2 * 1000);
    iDisplayYOff = (gsGlobal->Height * overscan) / (2 * 1000);
    iDisplayWidth = gsGlobal->Width - (2 * iDisplayXOff);
    iDisplayHeight = gsGlobal->Height - (2 * iDisplayYOff);

    fRenderXOff = (float)iDisplayXOff - 0.5f;
    fRenderYOff = (float)iDisplayYOff - 0.5f;
    fRenderYBase = fRenderYOff;
    fRenderXBase = fRenderXOff;

    if (rmGetInterlacedFrameMode() == 1)
        fRenderYOff += 0.25f;
}

unsigned char rmGetHsync(void)
{
    return rm_mode_table[vmode].hsync;
}
