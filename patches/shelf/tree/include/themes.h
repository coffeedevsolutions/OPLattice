#ifndef __THEMES_H
#define __THEMES_H

#include "include/textures.h"
#include "include/texcache.h"
#include "include/menusys.h"

#define THM_MAX_FILES 64
#define THM_MAX_FONTS 16

typedef struct
{
    // optional, only for overlays
    int upperLeft_x;
    int upperLeft_y;
    int upperRight_x;
    int upperRight_y;
    int lowerLeft_x;
    int lowerLeft_y;
    int lowerRight_x;
    int lowerRight_y;

    // basic texture information
    char *name;
    GSTEXTURE source;
} image_texture_t;

typedef struct
{
    // Attributes for: AttributeImage
    int currentUid;
    u32 currentConfigId;
    char *currentValue;

    // Attributes  for: AttributeImage & GameImage
    image_cache_t *cache;
    int cacheLinked;

    // Attributes for: GameImage. When useOffset is set, the element renders the
    // list entry `offset` places after the top of the current page instead of
    // the highlighted one, which is what makes free-form tile layouts possible.
    int offset;
    int useOffset;

    // Attributes for: RecentImage. One cache slot per element, because a recent
    // entry is not a submenu item and so has nowhere else to keep them.
    int recentIndex;
    int recentCacheId;
    int recentUid;

    // Attributes for: AttributeImage & GameImage & StaticImage
    image_texture_t *defaultTexture;
    int defaultTextureLinked;

    image_texture_t *overlayTexture;
    int overlayTextureLinked;
} mutable_image_t;

typedef struct
{
    // Attributes for: AttributeText & StaticText
    char *value;
    int sizingMode;

    // Attributes for: GameCountText. Set both and the count renders as
    // prefix + number + suffix instead of the localised "Files found: %i".
    char *countPrefix;
    char *countSuffix;

    // Attributes for: AttributeText
    char *alias;
    int displayMode;

    u32 currentConfigId;
    char *currentValue;
} mutable_text_t;

/// MenuTabs: every visible device drawn at once, with the current one framed.
typedef struct
{
    int pad;          //!< horizontal padding inside each tab
    int frameSize;    //!< outline thickness when no pill is supplied
    u64 selColor;     //!< pill fill, or outline colour
    u64 selTint;      //!< the same colour halved, for tinting the white caps
    u64 selTextColor; //!< label colour on top of the pill
    int hintFont;     //!< font slot for the L1/R1 hints
    u64 hintColor;
    char *prevText;   //!< drawn left of the strip, e.g. "L1"
    char *nextText;

    /* Round caps for the pill. Each is half as wide as the tab is tall; the
     * span between them is filled with a plain rect, so the pill is exact at
     * any label width instead of a stretched rounded rectangle. */
    image_texture_t *capLeft;
    image_texture_t *capRight;

    /* Per-device label overrides, so a theme can say "USB" where OPL's string
     * table says "USB Games". Keyed on the device's own text id rather than its
     * mode, because BDM_MODE0..4 are connection slots -- a USB stick and an
     * MX4SIO card are both BDM devices and would otherwise share one label.
     * NULL falls back to the localised name. */
    char *labelUsb;
    char *labelIlink;
    char *labelMx4sio;
    char *labelMmce;
    char *labelBdm;
    char *labelEth;
    char *labelHdd;
    char *labelApp;
} menu_tabs_t;

typedef struct
{
    int displayedItems;

    // Grid mode. columns == 1 is the classic single-column text list.
    int columns;
    int cellWidth;
    int cellHeight;
    int gap;        //!< inset between the cell pitch and the drawn art
    int showText;   //!< reserve a label line at the bottom of each cell
    int labelHeight; //!< height of that line; shrink it when using a small font
    int frameSize;  //!< selection frame thickness in virtual pixels, 0 to disable

    const char *decorator;
    mutable_image_t *decoratorImage;
} items_list_t;

typedef struct theme_element
{
    int type;
    int posX;
    int posY;
    /* When set, posX is treated as an offset from screen centre and narrowed by
     * the same 3/4 that rmDrawPixmap applies to a scaled width. That lets an
     * element stay aligned with a centred grid, which moves its edges inward in
     * anamorphic 16:9. Applied at draw time, because toggling widescreen calls
     * rmSetAspectRatio without reloading the theme (gui.c:633). */
    short xScaled;
    /* InfoHintText only. 0 hides the Run hint, for themes that draw their own
     * launch affordance and would otherwise say it twice. */
    short showRun;
    short aligned;
    int width;
    int height;
    short scaled;
    u64 color;
    int font;

    void *extended;

    void (*drawElem)(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem);
    void (*endElem)(struct theme_element *elem);

    struct theme_element *next;
} theme_element_t;

typedef struct
{
    theme_element_t *first;
    theme_element_t *last;
} theme_elems_t;

typedef struct
{
    char *filePath;
    char *name;
} theme_file_t;

typedef struct theme
{
    int useDefault;
    int usedHeight;

    unsigned char bgColor[3];
    u64 textColor;
    u64 uiTextColor;
    u64 selTextColor;

    theme_elems_t mainElems;
    theme_elems_t infoElems;
    theme_element_t *gamesItemsList;

    theme_elems_t appsMainElems;
    theme_elems_t appsInfoElems;
    theme_element_t *appsItemsList;

    int gameCacheCount;

    theme_element_t *itemsList;
    theme_element_t *loadingIcon;
    int loadingIconCount;

    GSTEXTURE textures[TEXTURES_COUNT];
    int fonts[THM_MAX_FONTS]; //!< Storage of font handles for removal once not needed
} theme_t;

extern theme_t *gTheme;

void thmInit(void);
void thmReinit(const char *path);
void thmReloadScreenExtents(void);
int thmAddElements(char *path, const char *separator, int forceRefresh);
const char *thmGetValue(void);
GSTEXTURE *thmGetTexture(unsigned int id);
void thmEnd(void);

// Indices are shifted in GUI, as we add the internal default theme at 0
int thmSetGuiValue(int themeID, int reload);
int thmGetGuiValue(void);
int thmFindGuiID(const char *theme);
const char **thmGetGuiList(void);
char *thmGetFilePath(int themeID);

#endif
