#include "include/opl.h"
#include "include/themes.h"
#include "include/util.h"
#include "include/gui.h"
#include "include/renderman.h"
#include "include/textures.h"
#include "include/ioman.h"
#include "include/fntsys.h"
#include "include/lang.h"
#include "include/pad.h"
#include "include/sound.h"

#define MENU_POS_V     50
#define HINT_HEIGHT    32
#define DECORATOR_SIZE 20

extern const char conf_theme_OPL_cfg;
extern u16 size_conf_theme_OPL_cfg;

theme_t *gTheme;

static int screenWidth;
static int screenHeight;
static int guiThemeID = 0;

static int nThemes = 0;
static theme_file_t themes[THM_MAX_FILES];
static const char **guiThemesNames = NULL;

// Global data
theme_t *gTheme;

enum ELEM_ATTRIBUTE_TYPE {
    ELEM_TYPE_ATTRIBUTE_TEXT = 0,
    ELEM_TYPE_STATIC_TEXT,
    ELEM_TYPE_ATTRIBUTE_IMAGE,
    ELEM_TYPE_GAME_IMAGE,
    ELEM_TYPE_STATIC_IMAGE,
    ELEM_TYPE_BACKGROUND, // A static image can be specified as the background. Otherwise, the plasma background will be drawn.
    ELEM_TYPE_MENU_ICON,
    ELEM_TYPE_MENU_TEXT,
    ELEM_TYPE_ITEMS_LIST,
    ELEM_TYPE_ITEM_ICON,
    ELEM_TYPE_ITEM_COVER,
    ELEM_TYPE_ITEM_TEXT,
    ELEM_TYPE_HINT_TEXT,
    ELEM_TYPE_INFO_HINT_TEXT,
    ELEM_TYPE_LOADING_ICON,
    ELEM_TYPE_BDM_INDEX,
    ELEM_TYPE_GAME_COUNT_TEXT,
    ELEM_TYPE_RECENT_IMAGE,
    ELEM_TYPE_RECENT_TEXT,
    ELEM_TYPE_MENU_TABS,
    ELEM_TYPE_ATTRIBUTE_LIST,
    ELEM_TYPE_COUNT
};

#define DISPLAY_ALWAYS  0
#define DISPLAY_DEFINED 1
#define DISPLAY_NEVER   2

/* Fields one AttributeList may carry. Eight is well past the four the
   details page needs and keeps the key array a fixed, tiny allocation. */
#define THM_LIST_MAX 8

#define SIZING_NONE -1
#define SIZING_CLIP 0
#define SIZING_WRAP 1

static const char *elementsType[ELEM_TYPE_COUNT] = {
    "AttributeText",
    "StaticText",
    "AttributeImage",
    "GameImage",
    "StaticImage",
    "Background",
    "MenuIcon",
    "MenuText",
    "ItemsList",
    "ItemIcon",
    "ItemCover",
    "ItemText",
    "HintText",
    "InfoHintText",
    "LoadingIcon",
    "BdmIndex",
    "GameCountText",
    "RecentImage",
    "RecentText",
    "MenuTabs",
    "AttributeList"};

static char *themeStrDup(config_set_t *themeConfig, const char *name, const char *suffix);

// Common functions for Text ////////////////////////////////////////////////////////////////////////////////////////////////

static void endMutableText(theme_element_t *elem)
{
    mutable_text_t *mutableText = (mutable_text_t *)elem->extended;
    if (mutableText) {
        if (mutableText->value)
            free(mutableText->value);

        if (mutableText->alias)
            free(mutableText->alias);

        if (mutableText->countPrefix)
            free(mutableText->countPrefix);

        if (mutableText->countSuffix)
            free(mutableText->countSuffix);

        free(mutableText);
    }

    free(elem);
}

static mutable_text_t *initMutableText(const char *themePath, config_set_t *themeConfig, theme_t *theme, const char *name, int type, struct theme_element *elem, const char *value, const char *alias, int displayMode, int sizingMode)
{
    mutable_text_t *mutableText = (mutable_text_t *)malloc(sizeof(mutable_text_t));
    mutableText->currentConfigId = 0;
    mutableText->currentValue = NULL;
    mutableText->alias = NULL;
    mutableText->countPrefix = NULL;
    mutableText->countSuffix = NULL;

    char elemProp[64];

    snprintf(elemProp, sizeof(elemProp), "%s_display", name);
    configGetInt(themeConfig, elemProp, &displayMode);
    mutableText->displayMode = displayMode;

    int length = strlen(value) + 1;
    mutableText->value = (char *)malloc(length * sizeof(char));
    memcpy(mutableText->value, value, length);

    snprintf(elemProp, sizeof(elemProp), "%s_wrap", name);
    if (configGetInt(themeConfig, elemProp, &sizingMode)) {
        if (sizingMode > 0)
            sizingMode = SIZING_WRAP;
    }

    if ((elem->width != DIM_UNDEF) || (elem->height != DIM_UNDEF)) {
        if (sizingMode == SIZING_NONE)
            sizingMode = SIZING_CLIP;

        if (elem->width == DIM_UNDEF)
            elem->width = screenWidth;

        if (elem->height == DIM_UNDEF)
            elem->height = screenHeight;
    } else
        sizingMode = SIZING_NONE;
    mutableText->sizingMode = sizingMode;

    if (type == ELEM_TYPE_ATTRIBUTE_TEXT) {
        snprintf(elemProp, sizeof(elemProp), "%s_title", name);
        configGetStr(themeConfig, elemProp, &alias);
        if (!alias) {
            if (value[0] == '#')
                alias = &value[1];
            else
                alias = value;
        }

        char *temp;
        if (!strncmp(alias, "Title", 5))
            temp = _l(_STR_INFO_TITLE);
        else if (!strncmp(alias, "Genre", 5))
            temp = _l(_STR_INFO_GENRE);
        else if (!strncmp(alias, "Release", 7))
            temp = _l(_STR_INFO_RELEASE);
        else if (!strncmp(alias, "Developer", 9))
            temp = _l(_STR_INFO_DEVELOPER);
        else if (!strncmp(alias, "Size", 4))
            temp = _l(_STR_SIZE);
        else if (!strncmp(alias, "Description", 11))
            temp = _l(_STR_INFO_DESCRIPTION);
        else
            temp = (char *)alias;

        length = strlen(temp) + 1 + 2;
        mutableText->alias = (char *)calloc(length, sizeof(char));
        if (mutableText->sizingMode == SIZING_WRAP)
            snprintf(mutableText->alias, length, "%s:\n", temp);
        else
            snprintf(mutableText->alias, length, "%s: ", temp);
    } else {
        if (mutableText->sizingMode == SIZING_WRAP)
            fntFitString(elem->font, mutableText->value, elem->width);
    }

    return mutableText;
}

// StaticText ///////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void drawStaticText(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    mutable_text_t *mutableText = (mutable_text_t *)elem->extended;
    if (mutableText->sizingMode == SIZING_NONE)
        fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, 0, 0, mutableText->value, elem->color);
    else
        fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, mutableText->value, elem->color);
}

static void initStaticText(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name)
{
    const char *value;
    char elemProp[64];

    snprintf(elemProp, sizeof(elemProp), "%s_value", name);
    configGetStr(themeConfig, elemProp, &value);
    if (value) {
        elem->extended = initMutableText(themePath, themeConfig, theme, name, ELEM_TYPE_STATIC_TEXT, elem, value, NULL, DISPLAY_ALWAYS, SIZING_NONE);
        elem->endElem = &endMutableText;
        elem->drawElem = &drawStaticText;
    } else
        LOG("THEMES StaticText %s: NO value, elem disabled !!\n", name);
}

// GameCountText ////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int getGameCount(void *support)
{
    item_list_t *list = (item_list_t *)support;
    return list->itemGetCount(list);
}

static void drawGameCountText(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    mutable_text_t *mutableText = (mutable_text_t *)elem->extended;

    if (config) {
        if (mutableText->currentConfigId != config->uid) {
            // force refresh
            mutableText->currentConfigId = config->uid;

            int count = getGameCount(menu->item->userdata);

            /* A prefix or suffix replaces the localised "Files found: %i", so a
             * theme can render "17 Titles". Deliberately not a printf format
             * from the cfg -- that would hand a user-supplied format string
             * straight to snprintf. */
            if (mutableText->countPrefix || mutableText->countSuffix)
                snprintf(mutableText->value, sizeof(char) * 60, "%s%d%s",
                         mutableText->countPrefix ? mutableText->countPrefix : "",
                         count,
                         mutableText->countSuffix ? mutableText->countSuffix : "");
            else
                snprintf(mutableText->value, sizeof(char) * 60, _l(_STR_FILE_COUNT), count);
        }
    }

    fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, 0, 0, mutableText->value, elem->color);
}

static void initGameCountText(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name)
{
    int length = 60;
    char *countStr = (char *)malloc(length * sizeof(char));
    memset(countStr, 0, length * sizeof(char));

    elem->extended = initMutableText(themePath, themeConfig, theme, name, ELEM_TYPE_ATTRIBUTE_TEXT, elem, countStr, NULL, DISPLAY_ALWAYS, SIZING_NONE);
    ((mutable_text_t *)elem->extended)->countPrefix = themeStrDup(themeConfig, name, "prefix");
    ((mutable_text_t *)elem->extended)->countSuffix = themeStrDup(themeConfig, name, "suffix");
    elem->endElem = &endMutableText;
    elem->drawElem = &drawGameCountText;
}

// AttributeText ////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void drawAttributeText(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    mutable_text_t *mutableText = (mutable_text_t *)elem->extended;
    if (config) {
        if (mutableText->currentConfigId != config->uid) {
            // force refresh
            mutableText->currentConfigId = config->uid;
            mutableText->currentValue = NULL;
            if (configGetStr(config, mutableText->value, (const char **)&mutableText->currentValue)) {
                if (mutableText->sizingMode == SIZING_WRAP)
                    fntFitString(elem->font, mutableText->currentValue, elem->width);
            }
        }
        if (mutableText->currentValue) {
            char result[300];
            if (mutableText->displayMode == DISPLAY_NEVER) {
                if (!strncmp(mutableText->alias, _l(_STR_SIZE), strlen(_l(_STR_SIZE)))) {
                    snprintf(result, sizeof(result), "%s MiB", mutableText->currentValue);
                    if (mutableText->sizingMode == SIZING_NONE)
                        fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, 0, 0, result, elem->color);
                    else
                        fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, result, elem->color);
                } else {
                    if (mutableText->sizingMode == SIZING_NONE)
                        fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, 0, 0, mutableText->currentValue, elem->color);
                    else
                        fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, mutableText->currentValue, elem->color);
                }
            } else {
                if (!strncmp(mutableText->alias, _l(_STR_SIZE), strlen(_l(_STR_SIZE))))
                    snprintf(result, sizeof(result), "%s%s MiB", mutableText->alias, mutableText->currentValue);
                else
                    snprintf(result, sizeof(result), "%s%s", mutableText->alias, mutableText->currentValue);
                if (mutableText->sizingMode == SIZING_NONE)
                    fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, 0, 0, result, elem->color);
                else
                    fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, result, elem->color);
            }
            return;
        }
    }
    if (mutableText->displayMode == DISPLAY_ALWAYS) {
        if (mutableText->sizingMode == SIZING_NONE)
            fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, 0, 0, mutableText->alias, elem->color);
        else
            fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, mutableText->alias, elem->color);
    }
}

// AttributeList ////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* One element that lays several attributes out inline, with a separator between
   the ones that have a value.

   AttributeText positions absolutely, so a strip built from several of them has
   fixed field positions and fixed separator positions, and the gaps therefore
   depend on how long each value happens to be: "Action Adventure" butts against
   its separator while "RPG" leaves a hole. There is no arrangement of the theme
   file that fixes that -- OPL has no inline flow, and `aligned` is a bitmask
   offering left and centre only, with no right-align to hang a field on.

   So the pen has to move, which means measuring, which means C. Each value is
   measured with fntCalcDimensions and the pen advances by its width plus the
   gap. Absent values are skipped entirely along with their separator, so a game
   with no Rating does not leave a dangling bullet or a double gap.

   Clipping is honoured: if the strip would overrun the element's width, drawing
   stops at the last field that fits rather than running off the panel. */

typedef struct
{
    char *attributes;   //!< the raw "A,B,C" list, owned; split in place at init
    const char *keys[THM_LIST_MAX];
    int count;
    char *separator;    //!< drawn between adjacent present values
    int gap;            //!< space either side of the separator
} attribute_list_t;

static void endAttributeList(theme_element_t *elem)
{
    attribute_list_t *al = (attribute_list_t *)elem->extended;
    if (al) {
        if (al->attributes)
            free(al->attributes);
        if (al->separator)
            free(al->separator);
        free(al);
        elem->extended = NULL;
    }
}

static void drawAttributeList(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    attribute_list_t *al = (attribute_list_t *)elem->extended;
    int i, pen, drawn = 0;
    int limit = elem->width > 0 ? elem->posX + elem->width : 640;

    if (!config || !al)
        return;

    pen = elem->posX;
    for (i = 0; i < al->count; i++) {
        const char *value = NULL;
        char buf[64];
        int w;

        if (!configGetStr(config, al->keys[i], &value) || !value || !*value)
            continue;                       /* absent: no field, no separator */

        /* #Size is stored as a bare number and AttributeText appends the unit
           when it draws. Anything reading the same key has to do the same, or
           the size silently becomes a meaningless integer. */
        if (!strcmp(al->keys[i], CONFIG_ITEM_SIZE)) {
            snprintf(buf, sizeof(buf), "%s MiB", value);
            value = buf;
        }

        if (drawn) {
            int sw = fntCalcDimensions(elem->font, al->separator);
            if (pen + al->gap + sw + al->gap > limit)
                break;
            fntRenderString(elem->font, pen + al->gap, elem->posY, ALIGN_NONE, 0, 0,
                            al->separator, elem->color);
            pen += al->gap + sw + al->gap;
        }

        w = fntCalcDimensions(elem->font, value);
        if (pen + w > limit)
            break;                          /* clip rather than overrun */
        fntRenderString(elem->font, pen, elem->posY, ALIGN_NONE, 0, 0, value, elem->color);
        pen += w;
        drawn++;
    }
}

static void initAttributeList(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name)
{
    char elemProp[64];
    const char *attributes = NULL, *separator = NULL;
    attribute_list_t *al;
    char *p;

    snprintf(elemProp, sizeof(elemProp), "%s_attributes", name);
    configGetStr(themeConfig, elemProp, &attributes);
    if (!attributes) {
        LOG("THEMES AttributeList %s: NO attributes, elem disabled !!\n", name);
        return;
    }

    al = (attribute_list_t *)calloc(1, sizeof(attribute_list_t));
    if (!al)
        return;

    al->attributes = strdup(attributes);
    if (!al->attributes) {
        free(al);
        return;
    }

    /* Split on commas in place. Leading spaces are trimmed so "A, B" works. */
    p = al->attributes;
    while (*p && al->count < THM_LIST_MAX) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        al->keys[al->count++] = p;
        while (*p && *p != ',')
            p++;
        if (*p)
            *p++ = '\0';
    }

    snprintf(elemProp, sizeof(elemProp), "%s_separator", name);
    configGetStr(themeConfig, elemProp, &separator);
    al->separator = strdup(separator ? separator : "\xc2\xb7");   /* U+00B7 */

    al->gap = 6;
    snprintf(elemProp, sizeof(elemProp), "%s_gap", name);
    configGetInt(themeConfig, elemProp, &al->gap);
    if (al->gap < 0)
        al->gap = 0;

    elem->extended = al;
    elem->endElem = &endAttributeList;
    elem->drawElem = &drawAttributeList;
    LOG("THEMES AttributeList %s: %d attributes, gap %d\n", name, al->count, al->gap);
}

static void initAttributeText(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name)
{
    const char *attribute;
    char elemProp[64];

    snprintf(elemProp, sizeof(elemProp), "%s_attribute", name);
    configGetStr(themeConfig, elemProp, &attribute);
    if (attribute) {
        elem->extended = initMutableText(themePath, themeConfig, theme, name, ELEM_TYPE_ATTRIBUTE_TEXT, elem, attribute, NULL, DISPLAY_ALWAYS, SIZING_NONE);
        elem->endElem = &endMutableText;
        elem->drawElem = &drawAttributeText;
    } else
        LOG("THEMES AttributeText %s: NO attribute, elem disabled !!\n", name);
}

// Common functions for Image ///////////////////////////////////////////////////////////////////////////////////////////////

static void findDuplicate(theme_element_t *first, const char *cachePattern, const char *defaultTexture, const char *overlayTexture, mutable_image_t *target)
{
    theme_element_t *elem = first;
    while (elem) {
        if ((elem->type == ELEM_TYPE_STATIC_IMAGE) || (elem->type == ELEM_TYPE_ATTRIBUTE_IMAGE) || (elem->type == ELEM_TYPE_GAME_IMAGE) || (elem->type == ELEM_TYPE_BACKGROUND)) {
            mutable_image_t *source = (mutable_image_t *)elem->extended;

            if (cachePattern && source->cache && !strcmp(cachePattern, source->cache->suffix)) {
                target->cache = source->cache;
                target->cacheLinked = 1;
                LOG("THEMES Re-using a cache for pattern %s\n", cachePattern);
            }

            if (defaultTexture && source->defaultTexture && !strcmp(defaultTexture, source->defaultTexture->name)) {
                target->defaultTexture = source->defaultTexture;
                target->defaultTextureLinked = 1;
                LOG("THEMES Re-using the default texture for %s\n", defaultTexture);
            }

            if (overlayTexture && source->overlayTexture && !strcmp(overlayTexture, source->overlayTexture->name)) {
                target->overlayTexture = source->overlayTexture;
                target->overlayTextureLinked = 1;
                LOG("THEMES Re-using the overlay texture for %s\n", overlayTexture);
            }
        }

        elem = elem->next;
    }
}

static void freeImageTexture(image_texture_t *texture)
{
    if (texture) {
        if (texture->source.Mem) {
            rmUnloadTexture(&texture->source);
            free(texture->source.Mem);
            texture->source.Mem = NULL;
        }
        if (texture->source.Clut) {
            free(texture->source.Clut);
            texture->source.Clut = NULL;
        }
        if (texture->name) {
            free(texture->name);
            texture->name = NULL;
        }
        free(texture);
    }
}

static image_texture_t *initImageTexture(const char *themePath, config_set_t *themeConfig, const char *name, const char *imgName, int isOverlay)
{
    image_texture_t *texture = (image_texture_t *)malloc(sizeof(image_texture_t));
    texture->name = NULL;

    int texId = -1;
    int result = 0;

    if (themePath) {
        char path[256];
        snprintf(path, sizeof(path), "%s%s", themePath, imgName);
        if (texDiscoverLoad(&texture->source, path, texId) < 0) {
            /* Was an empty statement. A texture that fails to load drew nothing
             * at all, which is indistinguishable from a theme that never asked
             * for one -- the silent failure this campaign exists to remove. */
            LOG("THEMES texture load FAILED: %s\n", path);
            texMakePlaceholder(&texture->source);
        }
        result = 1;
    } else {
        texId = texLookupInternalTexId(imgName);
        if (texLoadInternal(&texture->source, texId) < 0) {
            LOG("THEMES internal texture load FAILED: %s\n", imgName);
            texMakePlaceholder(&texture->source);
        }
        result = 1;
    }

    if (result) {
        int length = strlen(imgName) + 1;
        texture->name = (char *)malloc(length * sizeof(char));
        memcpy(texture->name, imgName, length);

        if (isOverlay) {
            int intValue;
            char elemProp[64];
            snprintf(elemProp, sizeof(elemProp), "%s_overlay_ulx", name);
            if (configGetInt(themeConfig, elemProp, &intValue))
                texture->upperLeft_x = intValue;
            snprintf(elemProp, sizeof(elemProp), "%s_overlay_uly", name);
            if (configGetInt(themeConfig, elemProp, &intValue))
                texture->upperLeft_y = intValue;
            snprintf(elemProp, sizeof(elemProp), "%s_overlay_urx", name);
            if (configGetInt(themeConfig, elemProp, &intValue))
                texture->upperRight_x = intValue;
            snprintf(elemProp, sizeof(elemProp), "%s_overlay_ury", name);
            if (configGetInt(themeConfig, elemProp, &intValue))
                texture->upperRight_y = intValue;
            snprintf(elemProp, sizeof(elemProp), "%s_overlay_llx", name);
            if (configGetInt(themeConfig, elemProp, &intValue))
                texture->lowerLeft_x = intValue;
            snprintf(elemProp, sizeof(elemProp), "%s_overlay_lly", name);
            if (configGetInt(themeConfig, elemProp, &intValue))
                texture->lowerLeft_y = intValue;
            snprintf(elemProp, sizeof(elemProp), "%s_overlay_lrx", name);
            if (configGetInt(themeConfig, elemProp, &intValue))
                texture->lowerRight_x = intValue;
            snprintf(elemProp, sizeof(elemProp), "%s_overlay_lry", name);
            if (configGetInt(themeConfig, elemProp, &intValue))
                texture->lowerRight_y = intValue;
        }
    } else {
        freeImageTexture(texture);
        texture = NULL;
    }

    return texture;
}

static image_texture_t *initImageInternalTexture(config_set_t *themeConfig, const char *name)
{
    image_texture_t *texture = (image_texture_t *)malloc(sizeof(image_texture_t));
    texture->name = NULL;
    int result;

    if ((result = texLookupInternalTexId(name)) >= 0) {
        result = texLoadInternal(&texture->source, result);
        int length = strlen(name) + 1;
        texture->name = (char *)malloc(length * sizeof(char));
        memcpy(texture->name, name, length);
    }

    if (result < 0) {
        freeImageTexture(texture);
        texture = NULL;
    }

    return texture;
}

static void endMutableImage(struct theme_element *elem)
{
    mutable_image_t *mutableImage = (mutable_image_t *)elem->extended;
    if (mutableImage) {
        if (mutableImage->cache && !mutableImage->cacheLinked)
            cacheDestroyCache(mutableImage->cache);

        if (mutableImage->defaultTexture && !mutableImage->defaultTextureLinked)
            freeImageTexture(mutableImage->defaultTexture);

        if (mutableImage->overlayTexture && !mutableImage->overlayTextureLinked)
            freeImageTexture(mutableImage->overlayTexture);

        free(mutableImage);
    }

    free(elem);
}

static mutable_image_t *initMutableImage(const char *themePath, config_set_t *themeConfig, theme_t *theme, const char *name, int type, const char *cachePattern, int cacheCount, const char *defaultTexture, const char *overlayTexture)
{
    mutable_image_t *mutableImage = (mutable_image_t *)malloc(sizeof(mutable_image_t));
    mutableImage->currentUid = -1;
    mutableImage->currentConfigId = 0;
    mutableImage->currentValue = NULL;
    mutableImage->cache = NULL;
    mutableImage->cacheLinked = 0;
    mutableImage->defaultTexture = NULL;
    mutableImage->defaultTextureLinked = 0;
    mutableImage->overlayTexture = NULL;
    mutableImage->overlayTextureLinked = 0;
    mutableImage->offset = 0;
    mutableImage->useOffset = 0;
    mutableImage->recentIndex = 0;
    mutableImage->recentCacheId = -1;
    mutableImage->recentUid = -1;

    char elemProp[64];

    if (type == ELEM_TYPE_ATTRIBUTE_IMAGE) {
        snprintf(elemProp, sizeof(elemProp), "%s_attribute", name);
        configGetStr(themeConfig, elemProp, &cachePattern);
        LOG("THEMES MutableImage %s: type: %s using cache pattern: %s\n", name, elementsType[type], cachePattern);
    } else if ((type == ELEM_TYPE_GAME_IMAGE) || (type == ELEM_TYPE_BACKGROUND)) {
        snprintf(elemProp, sizeof(elemProp), "%s_pattern", name);
        configGetStr(themeConfig, elemProp, &cachePattern);
        snprintf(elemProp, sizeof(elemProp), "%s_count", name);
        configGetInt(themeConfig, elemProp, &cacheCount);

        // Tile layouts: bind this element to a fixed slot on the page rather
        // than to the highlighted entry. Absent key keeps the legacy behaviour.
        snprintf(elemProp, sizeof(elemProp), "%s_offset", name);
        if (configGetInt(themeConfig, elemProp, &mutableImage->offset)) {
            mutableImage->useOffset = 1;
            if (mutableImage->offset < 0)
                mutableImage->offset = 0;
        }

        LOG("THEMES MutableImage %s: type: %s using cache pattern: %s count: %d offset: %d\n", name, elementsType[type], cachePattern, cacheCount, mutableImage->offset);
    }

    snprintf(elemProp, sizeof(elemProp), "%s_default", name);
    configGetStr(themeConfig, elemProp, &defaultTexture);

    if (type != ELEM_TYPE_BACKGROUND) {
        snprintf(elemProp, sizeof(elemProp), "%s_overlay", name);
        configGetStr(themeConfig, elemProp, &overlayTexture);
    }

    findDuplicate(theme->mainElems.first, cachePattern, defaultTexture, overlayTexture, mutableImage);
    findDuplicate(theme->infoElems.first, cachePattern, defaultTexture, overlayTexture, mutableImage);
    findDuplicate(theme->appsMainElems.first, cachePattern, defaultTexture, overlayTexture, mutableImage);
    findDuplicate(theme->appsInfoElems.first, cachePattern, defaultTexture, overlayTexture, mutableImage);

    if (cachePattern && !mutableImage->cache) {
        if (type == ELEM_TYPE_ATTRIBUTE_IMAGE)
            mutableImage->cache = cacheInitCache(-1, themePath, 0, cachePattern, 1);
        else
            mutableImage->cache = cacheInitCache(theme->gameCacheCount++, "ART", 1, cachePattern, cacheCount);
    }

    if (!themePath)
        if (defaultTexture && !mutableImage->defaultTexture)
            mutableImage->defaultTexture = initImageInternalTexture(themeConfig, defaultTexture);

    if (defaultTexture && !mutableImage->defaultTexture)
        mutableImage->defaultTexture = initImageTexture(themePath, themeConfig, name, defaultTexture, 0);

    if (overlayTexture && !mutableImage->overlayTexture)
        mutableImage->overlayTexture = initImageTexture(themePath, themeConfig, name, overlayTexture, 1);

    return mutableImage;
}

// StaticImage //////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void drawStaticImage(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    mutable_image_t *staticImage = (mutable_image_t *)elem->extended;
    if (staticImage->overlayTexture) {
        rmDrawOverlayPixmap(&staticImage->overlayTexture->source, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol,
                            &staticImage->defaultTexture->source, staticImage->overlayTexture->upperLeft_x, staticImage->overlayTexture->upperLeft_y, staticImage->overlayTexture->upperRight_x, staticImage->overlayTexture->upperRight_y,
                            staticImage->overlayTexture->lowerLeft_x, staticImage->overlayTexture->lowerLeft_y, staticImage->overlayTexture->lowerRight_x, staticImage->overlayTexture->lowerRight_y);
    } else
        rmDrawPixmap(&staticImage->defaultTexture->source, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol);
}

static void initStaticImage(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name, const char *imageName)
{
    mutable_image_t *mutableImage = initMutableImage(themePath, themeConfig, theme, name, elem->type, NULL, 0, imageName, NULL);
    elem->extended = mutableImage;
    elem->endElem = &endMutableImage;

    if (mutableImage->defaultTexture)
        elem->drawElem = &drawStaticImage;
    else
        LOG("THEMES StaticImage %s: NO image name, elem disabled !!\n", name);
}

// GameImage ////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static GSTEXTURE *getGameImageTexture(image_cache_t *cache, void *support, struct submenu_item *item)
{
    if (gEnableArt) {
        item_list_t *list = (item_list_t *)support;
        char *startup = list->itemGetStartup(list, item->id);
        return cacheGetTexture(cache, list, &item->cache_id[cache->userId], &item->cache_uid[cache->userId], startup);
    }

    return NULL;
}

static void drawGameImage(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    mutable_image_t *gameImage = (mutable_image_t *)elem->extended;

    if (item && gameImage->useOffset) {
        // Walk to the tile this element owns. Past the end of the list the
        // element draws nothing at all, so a partly filled last page leaves
        // holes rather than repeating entries.
        struct submenu_list *tile = menu->item->pagestart;
        int step = gameImage->offset;
        while (step-- > 0 && tile)
            tile = tile->next;
        if (!tile)
            return;
        item = tile;
    }

    if (item) {
        GSTEXTURE *texture = getGameImageTexture(gameImage->cache, menu->item->userdata, &item->item);
        if (!texture || !texture->Mem) {
            if (gameImage->defaultTexture)
                texture = &gameImage->defaultTexture->source;
            else {
                if (elem->type == ELEM_TYPE_BACKGROUND)
                    guiDrawBGPlasma();
                return;
            }
        }

        if (gameImage->overlayTexture) {
            rmDrawOverlayPixmap(&gameImage->overlayTexture->source, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol,
                                texture, gameImage->overlayTexture->upperLeft_x, gameImage->overlayTexture->upperLeft_y, gameImage->overlayTexture->upperRight_x, gameImage->overlayTexture->upperRight_y,
                                gameImage->overlayTexture->lowerLeft_x, gameImage->overlayTexture->lowerLeft_y, gameImage->overlayTexture->lowerRight_x, gameImage->overlayTexture->lowerRight_y);
        } else
            rmDrawPixmap(texture, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol);

    } else if (elem->type == ELEM_TYPE_BACKGROUND) {
        if (gameImage->defaultTexture)
            rmDrawPixmap(&gameImage->defaultTexture->source, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol);
        else
            guiDrawBGPlasma();
    }
}

static void initGameImage(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name, const char *pattern, int count, const char *texture, const char *overlay)
{
    mutable_image_t *mutableImage = initMutableImage(themePath, themeConfig, theme, name, elem->type, pattern, count, texture, overlay);
    elem->extended = mutableImage;
    elem->endElem = &endMutableImage;

    if (mutableImage->cache)
        elem->drawElem = &drawGameImage;
    else
        LOG("THEMES GameImage %s: NO pattern, elem disabled !!\n", name);
}

// AttributeImage ///////////////////////////////////////////////////////////////////////////////////////////////////////////

static void drawAttributeImage(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    mutable_image_t *attributeImage = (mutable_image_t *)elem->extended;
    if (config) {
        if (attributeImage->currentConfigId != config->uid) {
            // force refresh
            attributeImage->currentUid = -1;
            attributeImage->currentConfigId = config->uid;
            attributeImage->currentValue = NULL;
            configGetStr(config, attributeImage->cache->suffix, (const char **)&attributeImage->currentValue);
        }
        if (attributeImage->currentValue) {
            if (thmGetGuiValue() == 0) {
                int texId;
                char *seppos = strchr(attributeImage->currentValue, '/');
                if (!seppos)
                    texId = texLookupInternalTexId(attributeImage->currentValue);
                else {
                    char imgName[32];
                    snprintf(imgName, sizeof(imgName), "%s_%s", attributeImage->cache->suffix, &seppos[1]);
                    texId = texLookupInternalTexId(&imgName[0]);
                }
                GSTEXTURE *texture = thmGetTexture(texId);
                if (texture && texture->Mem)
                    rmDrawPixmap(texture, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol);

                return;
            } else {
                int posZ = 0;
                GSTEXTURE *texture = cacheGetTexture(attributeImage->cache, menu->item->userdata, &posZ, &attributeImage->currentUid, attributeImage->currentValue);
                if (texture && texture->Mem) {
                    if (attributeImage->overlayTexture) {
                        rmDrawOverlayPixmap(&attributeImage->overlayTexture->source, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol,
                                            texture, attributeImage->overlayTexture->upperLeft_x, attributeImage->overlayTexture->upperLeft_y, attributeImage->overlayTexture->upperRight_x, attributeImage->overlayTexture->upperRight_y,
                                            attributeImage->overlayTexture->lowerLeft_x, attributeImage->overlayTexture->lowerLeft_y, attributeImage->overlayTexture->lowerRight_x, attributeImage->overlayTexture->lowerRight_y);
                    } else
                        rmDrawPixmap(texture, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol);

                    return;
                }
            }
        }
    }
    if (attributeImage->defaultTexture)
        rmDrawPixmap(&attributeImage->defaultTexture->source, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol);
}

static void initAttributeImage(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name)
{
    mutable_image_t *mutableImage = initMutableImage(themePath, themeConfig, theme, name, elem->type, NULL, 1, NULL, NULL);
    elem->extended = mutableImage;
    elem->endElem = &endMutableImage;

    if (mutableImage->cache)
        elem->drawElem = &drawAttributeImage;
    else
        LOG("THEMES AttributeImage %s: NO attribute, elem disabled !!\n", name);
}

// BasicElement /////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void endBasic(theme_element_t *elem)
{
    if (elem->extended)
        free(elem->extended);

    free(elem);
}

static theme_element_t *initBasic(const char *themePath, config_set_t *themeConfig, theme_t *theme, const char *name, int type, int x, int y, short aligned, int w, int h, short scaled, u64 color, int font)
{
    int intValue;
    unsigned char charColor[3];
    const char *temp;
    char elemProp[64];

    theme_element_t *elem = (theme_element_t *)malloc(sizeof(theme_element_t));

    elem->type = type;
    elem->extended = NULL;
    elem->drawElem = NULL;
    elem->endElem = &endBasic;
    elem->next = NULL;

    snprintf(elemProp, sizeof(elemProp), "%s_x", name);
    if (configGetStr(themeConfig, elemProp, &temp)) {
        if (!strncmp(temp, "POS_MID", 7))
            x = screenWidth >> 1;
        else
            x = atoi(temp);
    }
    if (x < 0)
        elem->posX = screenWidth + x;
    else
        elem->posX = x;

    snprintf(elemProp, sizeof(elemProp), "%s_y", name);
    if (configGetStr(themeConfig, elemProp, &temp)) {
        if (!strncmp(temp, "POS_MID", 7))
            y = screenHeight >> 1;
        else
            y = atoi(temp);
    }
    if (y < 0)
        elem->posY = ceil((screenHeight + y) * theme->usedHeight / screenHeight);
    else
        elem->posY = y;

    snprintf(elemProp, sizeof(elemProp), "%s_width", name);
    if (configGetStr(themeConfig, elemProp, &temp)) {
        if (!strncmp(temp, "DIM_INF", 7))
            elem->width = screenWidth;
        else
            elem->width = atoi(temp);
    } else
        elem->width = w;

    snprintf(elemProp, sizeof(elemProp), "%s_height", name);
    if (configGetStr(themeConfig, elemProp, &temp)) {
        if (!strncmp(temp, "DIM_INF", 7))
            elem->height = screenHeight;
        else
            elem->height = atoi(temp);
    } else
        elem->height = h;

    snprintf(elemProp, sizeof(elemProp), "%s_aligned", name);
    if (configGetInt(themeConfig, elemProp, &intValue))
        elem->aligned = (intValue == 0) ? ALIGN_NONE : ALIGN_CENTER;
    else
        elem->aligned = aligned;

    snprintf(elemProp, sizeof(elemProp), "%s_scaled", name);
    if (configGetInt(themeConfig, elemProp, &intValue))
        elem->scaled = (intValue == 0) ? SCALING_NONE : SCALING_RATIO;
    else
        elem->scaled = scaled;

    snprintf(elemProp, sizeof(elemProp), "%s_color", name);
    if (configGetColor(themeConfig, elemProp, charColor))
        elem->color = GS_SETREG_RGBA(charColor[0], charColor[1], charColor[2], 0x80);
    else
        elem->color = color;

    elem->xScaled = 0;
    snprintf(elemProp, sizeof(elemProp), "%s_x_scaled", name);
    if (configGetInt(themeConfig, elemProp, &intValue))
        elem->xScaled = (intValue != 0);

    elem->font = font;
    snprintf(elemProp, sizeof(elemProp), "%s_font", name);
    if (configGetInt(themeConfig, elemProp, &intValue)) {
        if (intValue > 0 && intValue < THM_MAX_FONTS)
            elem->font = theme->fonts[intValue];
    }

    return elem;
}

// Internal elements ////////////////////////////////////////////////////////////////////////////////////////////////////////
static void drawBackground(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    guiDrawBGPlasma();
}

static void initBackground(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name, const char *pattern, int count, const char *texture)
{
    mutable_image_t *mutableImage = initMutableImage(themePath, themeConfig, theme, name, elem->type, pattern, count, texture, NULL);
    elem->extended = mutableImage;
    elem->endElem = &endMutableImage;

    if (mutableImage->cache)
        elem->drawElem = &drawGameImage;
    else if (mutableImage->defaultTexture)
        elem->drawElem = &drawStaticImage;
    else
        elem->drawElem = &drawBackground;
}

static void drawMenuIcon(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    GSTEXTURE *menuIconTex = thmGetTexture(menu->item->icon_id);
    if (menuIconTex && menuIconTex->Mem)
        rmDrawPixmap(menuIconTex, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol);
}

static int findMenuNext(struct menu_list *menu)
{
    struct menu_list *next = menu->next;
    while (next != NULL && next->item->visible == 0)
        next = next->next;

    return next == NULL ? 0 : next->item->visible;
}

static int findMenuPrev(struct menu_list *menu)
{
    struct menu_list *prev = menu->prev;
    while (prev != NULL && prev->item->visible == 0)
        prev = prev->prev;

    return prev == NULL ? 0 : prev->item->visible;
}

static void drawMenuText(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    GSTEXTURE *leftIconTex = NULL, *rightIconTex = NULL;
    if (findMenuPrev(menu) != 0)
        leftIconTex = thmGetTexture(LEFT_ICON);
    if (findMenuNext(menu) != 0)
        rightIconTex = thmGetTexture(RIGHT_ICON);

    if (elem->aligned) {
        int offset = elem->width >> 1;
        if (leftIconTex && leftIconTex->Mem)
            rmDrawPixmap(leftIconTex, elem->posX - offset, elem->posY, elem->aligned, 20, 20, elem->scaled, gDefaultCol);
        if (rightIconTex && rightIconTex->Mem)
            rmDrawPixmap(rightIconTex, elem->posX + offset, elem->posY, elem->aligned, 20, 20, elem->scaled, gDefaultCol);
    } else {
        if (leftIconTex && leftIconTex->Mem)
            rmDrawPixmap(leftIconTex, elem->posX - leftIconTex->Width, elem->posY, elem->aligned, 20, 20, elem->scaled, gDefaultCol);
        if (rightIconTex && rightIconTex->Mem)
            rmDrawPixmap(rightIconTex, elem->posX + elem->width, elem->posY, elem->aligned, 20, 20, elem->scaled, gDefaultCol);
    }
    fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, 0, 0, menuItemGetText(menu->item), elem->color);
}

static void drawCellFrame(int x, int y, int w, int h, int t, u64 color);

/* MenuTabs //////////////////////////////////////////////////////////////////
 *
 * MenuText shows only the device you are on. This walks the whole device list
 * and draws every visible one, framing the current entry, so the other
 * libraries are on screen rather than implied by a pair of arrows.
 */

static void endMenuTabs(struct theme_element *elem)
{
    menu_tabs_t *tabs = (menu_tabs_t *)elem->extended;

    if (tabs) {
        if (tabs->capLeft)
            freeImageTexture(tabs->capLeft);
        if (tabs->capRight)
            freeImageTexture(tabs->capRight);

        /* Every one of these is a themeStrDup, so the struct cannot just be
         * released -- a theme reload happens on each device insertion too, not
         * only when the user picks a new theme. */
        char *owned[] = {tabs->prevText, tabs->nextText, tabs->labelUsb,
                         tabs->labelIlink, tabs->labelMx4sio, tabs->labelMmce,
                         tabs->labelBdm, tabs->labelEth, tabs->labelHdd,
                         tabs->labelApp};
        for (int i = 0; i < (int)(sizeof(owned) / sizeof(owned[0])); i++) {
            if (owned[i])
                free(owned[i]);
        }

        free(tabs);
    }

    free(elem);
}

/// Theme override for a device's name, else OPL's localised one.
static const char *menuTabLabel(menu_tabs_t *tabs, struct menu_item *item)
{
    const char *label = NULL;

    /* text_id already distinguishes the mass-storage devices from one another,
     * which the mode does not: BDM_MODE0..4 are connection slots, so a USB
     * stick and an MX4SIO card would otherwise be given the same name. */
    switch (item->text_id) {
        case _STR_USB_GAMES:
            label = tabs->labelUsb;
            break;
        case _STR_ILINK_GAMES:
            label = tabs->labelIlink;
            break;
        case _STR_MX4SIO_GAMES:
            label = tabs->labelMx4sio;
            break;
        case _STR_MMCE_GAMES:
            label = tabs->labelMmce;
            break;
        case _STR_HDD_GAMES:
            label = tabs->labelHdd;
            break;
        case _STR_NET_GAMES:
            label = tabs->labelEth;
            break;
        case _STR_APPS:
            label = tabs->labelApp;
            break;
        default: // _STR_BDM_GAMES, and any device type added later
            label = tabs->labelBdm;
            break;
    }

    return label ? label : menuItemGetText(item);
}

/// Width of a string in virtual units, for the given font slot.
static int menuTabTextWidth(int font, const char *text)
{
    return rmUnScaleX(fntCalcDimensions(font, text));
}

static int menuTabWidth(theme_element_t *elem, menu_tabs_t *tabs, struct menu_item *item)
{
    return menuTabTextWidth(elem->font, menuTabLabel(tabs, item)) + (2 * tabs->pad);
}

/**
 * Pill behind the active tab: a round cap at each end with a plain rect between
 * them, so it stays exact at any label width. The caps are drawn scaled, which
 * narrows them by 3/4 in anamorphic 16:9 exactly as the glyphs are narrowed, so
 * the pill keeps its shape and stays wrapped around the text in both aspects.
 */
static void drawMenuTabPill(theme_element_t *elem, menu_tabs_t *tabs, int x, int w)
{
    int capSrc = elem->height >> 1;
    int capW = (elem->scaled & SCALING_RATIO) ? rmWideScale(capSrc) : capSrc;
    int mid = w - (2 * capW);

    if (!tabs->capLeft || !tabs->capRight) {
        if (tabs->frameSize > 0)
            drawCellFrame(x, elem->posY, w, elem->height, tabs->frameSize, tabs->selColor);
        return;
    }

    rmDrawPixmap(&tabs->capLeft->source, x, elem->posY, ALIGN_NONE, capSrc, elem->height, elem->scaled, tabs->selTint);
    if (mid > 0)
        rmDrawRect(x + capW, elem->posY, mid, elem->height, tabs->selColor);
    rmDrawPixmap(&tabs->capRight->source, x + w - capW, elem->posY, ALIGN_NONE, capSrc, elem->height, elem->scaled, tabs->selTint);
}

static void drawMenuTabs(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    menu_tabs_t *tabs = (menu_tabs_t *)elem->extended;
    struct menu_list *head = menu, *m;
    int total = 0, count = 0, x, hintW;
    int mid = elem->posY + (elem->height >> 1);

    while (head->prev)
        head = head->prev;

    for (m = head; m; m = m->next) {
        if (m->item->visible == 0)
            continue;
        total += menuTabWidth(elem, tabs, m->item);
        count++;
    }

    if (count == 0)
        return;

    total += (count - 1) * elem->width; // elem->width is the gap between tabs

    if (tabs->prevText)
        total += menuTabTextWidth(tabs->hintFont, tabs->prevText) + elem->width;
    if (tabs->nextText)
        total += menuTabTextWidth(tabs->hintFont, tabs->nextText) + elem->width;

    x = elem->posX;
    if (elem->aligned)
        x -= total >> 1;

    if (tabs->prevText) {
        hintW = menuTabTextWidth(tabs->hintFont, tabs->prevText);
        fntRenderString(tabs->hintFont, x, mid, ALIGN_VCENTER, 0, 0, tabs->prevText, tabs->hintColor);
        x += hintW + elem->width;
    }

    for (m = head; m; m = m->next) {
        int w;

        if (m->item->visible == 0)
            continue;

        w = menuTabWidth(elem, tabs, m->item);

        if (m == menu) {
            drawMenuTabPill(elem, tabs, x, w);
            fntRenderString(elem->font, x + tabs->pad, mid, ALIGN_VCENTER, 0, 0, menuTabLabel(tabs, m->item), tabs->selTextColor);
        } else
            fntRenderString(elem->font, x + tabs->pad, mid, ALIGN_VCENTER, 0, 0, menuTabLabel(tabs, m->item), elem->color);

        x += w + elem->width;
    }

    if (tabs->nextText)
        fntRenderString(tabs->hintFont, x, mid, ALIGN_VCENTER, 0, 0, tabs->nextText, tabs->hintColor);
}

static char *themeStrDup(config_set_t *themeConfig, const char *name, const char *suffix)
{
    char elemProp[64];
    const char *value = NULL;

    snprintf(elemProp, sizeof(elemProp), "%s_%s", name, suffix);
    if (!configGetStr(themeConfig, elemProp, &value) || !value)
        return NULL;

    return strdup(value);
}

static void initMenuTabs(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name)
{
    menu_tabs_t *tabs = (menu_tabs_t *)malloc(sizeof(menu_tabs_t));
    unsigned char charColor[3];
    const char *value;
    char elemProp[64];
    int intValue;

    tabs->pad = 10;
    tabs->frameSize = 2;
    tabs->selColor = theme->selTextColor;
    tabs->selTint = gDefaultCol;
    tabs->selTextColor = GS_SETREG_RGBA(theme->bgColor[0], theme->bgColor[1], theme->bgColor[2], 0x80);
    tabs->hintFont = elem->font;
    tabs->hintColor = elem->color;
    tabs->prevText = NULL;
    tabs->nextText = NULL;
    tabs->capLeft = NULL;
    tabs->capRight = NULL;
    tabs->labelUsb = NULL;
    tabs->labelIlink = NULL;
    tabs->labelMx4sio = NULL;
    tabs->labelMmce = NULL;
    tabs->labelBdm = NULL;
    tabs->labelEth = NULL;
    tabs->labelHdd = NULL;
    tabs->labelApp = NULL;

    snprintf(elemProp, sizeof(elemProp), "%s_pad", name);
    configGetInt(themeConfig, elemProp, &tabs->pad);
    if (tabs->pad < 0)
        tabs->pad = 0;

    snprintf(elemProp, sizeof(elemProp), "%s_frame", name);
    configGetInt(themeConfig, elemProp, &tabs->frameSize);
    if (tabs->frameSize < 0)
        tabs->frameSize = 0;

    snprintf(elemProp, sizeof(elemProp), "%s_sel_color", name);
    if (configGetColor(themeConfig, elemProp, charColor)) {
        tabs->selColor = GS_SETREG_RGBA(charColor[0], charColor[1], charColor[2], 0x80);
        /* Textured draws multiply by the vertex colour with 0x80 as unity, so a
         * white cap tinted with the full value would come out doubled. Halve it
         * here and the cap lands on exactly the requested colour. */
        tabs->selTint = GS_SETREG_RGBA(charColor[0] >> 1, charColor[1] >> 1, charColor[2] >> 1, 0x80);
    }

    snprintf(elemProp, sizeof(elemProp), "%s_sel_text_color", name);
    if (configGetColor(themeConfig, elemProp, charColor))
        tabs->selTextColor = GS_SETREG_RGBA(charColor[0], charColor[1], charColor[2], 0x80);

    snprintf(elemProp, sizeof(elemProp), "%s_hint_font", name);
    if (configGetInt(themeConfig, elemProp, &intValue) && intValue > 0 && intValue < THM_MAX_FONTS)
        tabs->hintFont = theme->fonts[intValue];

    snprintf(elemProp, sizeof(elemProp), "%s_hint_color", name);
    if (configGetColor(themeConfig, elemProp, charColor))
        tabs->hintColor = GS_SETREG_RGBA(charColor[0], charColor[1], charColor[2], 0x80);

    tabs->prevText = themeStrDup(themeConfig, name, "prev_text");
    tabs->nextText = themeStrDup(themeConfig, name, "next_text");
    tabs->labelUsb = themeStrDup(themeConfig, name, "label_usb");
    tabs->labelIlink = themeStrDup(themeConfig, name, "label_ilink");
    tabs->labelMx4sio = themeStrDup(themeConfig, name, "label_mx4sio");
    tabs->labelMmce = themeStrDup(themeConfig, name, "label_mmce");
    tabs->labelBdm = themeStrDup(themeConfig, name, "label_bdm");
    tabs->labelEth = themeStrDup(themeConfig, name, "label_eth");
    tabs->labelHdd = themeStrDup(themeConfig, name, "label_hdd");
    tabs->labelApp = themeStrDup(themeConfig, name, "label_app");

    value = NULL;
    snprintf(elemProp, sizeof(elemProp), "%s_cap_left", name);
    if (configGetStr(themeConfig, elemProp, &value) && value)
        tabs->capLeft = initImageTexture(themePath, themeConfig, name, value, 0);

    value = NULL;
    snprintf(elemProp, sizeof(elemProp), "%s_cap_right", name);
    if (configGetStr(themeConfig, elemProp, &value) && value)
        tabs->capRight = initImageTexture(themePath, themeConfig, name, value, 0);

    elem->extended = tabs;
    elem->endElem = &endMenuTabs;
    elem->drawElem = &drawMenuTabs;
}

static void drawBDMIndex(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    item_list_t *itemList = menu->item->userdata;
    // Only render for bdm modes and if current mode is visible
    if (itemList->mode >= ETH_MODE || menu->item->visible == 0)
        return;

    // Only render if multiple mass devices are connected
    if (itemList->mode == 0 && menu->next->item->visible == 0)
        return;

    char imgName[32];
    snprintf(imgName, sizeof(imgName), "Index_%d", itemList->mode);

    GSTEXTURE *indexTex = thmGetTexture(texLookupInternalTexId(&imgName[0]));
    if (indexTex && indexTex->Mem)
        rmDrawPixmap(indexTex, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol);
}

/** Hard-edged selection frame: four filled rects, no gradient, no rounding. */
static void drawCellFrame(int x, int y, int w, int h, int t, u64 color)
{
    if (t <= 0 || w <= 0 || h <= 0)
        return;

    rmDrawRect(x, y, w, t, color);             // top
    rmDrawRect(x, y + h - t, w, t, color);     // bottom
    rmDrawRect(x, y + t, t, h - (2 * t), color);            // left
    rmDrawRect(x + w - t, y + t, t, h - (2 * t), color);    // right
}

static void drawItemsListGrid(struct menu_list *menu, struct submenu_list *item, struct theme_element *elem, items_list_t *itemsList, int posX, int posY)
{
    int labelH = itemsList->showText ? itemsList->labelHeight : 0;
    int artW = itemsList->cellWidth - itemsList->gap;
    int artH = itemsList->cellHeight - itemsList->gap - labelH;

    /* In anamorphic 16:9 rmDrawPixmap narrows a SCALING_RATIO image to 3/4 so it
     * survives the display stretch undistorted. The cell pitch has to shrink by
     * the same factor, otherwise the tiles keep their shape but the grid spreads
     * out. Text needs no correction: fntUpdateAspectRatio already rasterises
     * glyphs at 3/4 width, so only the clip box is converted here.
     */
    int wide = (elem->scaled & SCALING_RATIO);
    int pitchX = wide ? rmWideScale(itemsList->cellWidth) : itemsList->cellWidth;
    int drawnW = wide ? rmWideScale(artW) : artW;
    int gridW = pitchX * itemsList->columns;

    submenu_list_t *ps;
    int n = 0;

    if (artW < 1 || artH < 1)
        return;

    // Centre on the corrected width, so a centred grid stays centred in 16:9.
    if (elem->aligned) {
        posX -= gridW >> 1;
        posY -= elem->height >> 1;
    }

    ps = menu->item->pagestart;

    while (ps && (n < itemsList->displayedItems)) {
        int cx = posX + (n % itemsList->columns) * pitchX;
        int cy = posY + (n / itemsList->columns) * itemsList->cellHeight;
        int selected = (ps == item);
        u64 color = selected ? gTheme->selTextColor : elem->color;

        if (itemsList->decoratorImage) {
            GSTEXTURE *tex = getGameImageTexture(itemsList->decoratorImage->cache, menu->item->userdata, &ps->item);
            if ((!tex || !tex->Mem) && itemsList->decoratorImage->defaultTexture)
                tex = &itemsList->decoratorImage->defaultTexture->source;

            if (tex && tex->Mem)
                rmDrawPixmap(tex, cx, cy, ALIGN_NONE, artW, artH, elem->scaled, gDefaultCol);
        }

        if (selected && itemsList->frameSize > 0)
            drawCellFrame(cx, cy, drawnW, artH, itemsList->frameSize, gTheme->selTextColor);

        if (itemsList->showText)
            fntRenderString(elem->font, cx, cy + artH, ALIGN_NONE, drawnW, itemsList->labelHeight, submenuItemGetText(&ps->item), color);

        n++;
        ps = ps->next;
    }
}

/* RecentImage / RecentText //////////////////////////////////////////////////
 *
 * These render the most-recently-launched list, which is global and outlives
 * any one device. The entry is identified by its startup id, so the art is
 * looked up through the CURRENT device's ART folder -- a game last played from
 * another device shows its title but falls back to `default` for the picture.
 */

static void drawRecentImage(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    mutable_image_t *recentImage = (mutable_image_t *)elem->extended;
    const char *startup = oplRecentStartup(recentImage->recentIndex);
    GSTEXTURE *texture = NULL;

    if (startup != NULL && gEnableArt) {
        texture = cacheGetTexture(recentImage->cache, menu->item->userdata,
                                  &recentImage->recentCacheId, &recentImage->recentUid, (char *)startup);
    }

    if ((!texture || !texture->Mem) && recentImage->defaultTexture)
        texture = &recentImage->defaultTexture->source;

    if (texture && texture->Mem)
        rmDrawPixmap(texture, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, elem->scaled, gDefaultCol);
}

static void initRecentImage(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name)
{
    mutable_image_t *mutableImage = initMutableImage(themePath, themeConfig, theme, name, ELEM_TYPE_GAME_IMAGE, "COV", 1, NULL, NULL);
    char elemProp[64];

    elem->extended = mutableImage;
    elem->endElem = &endMutableImage;

    snprintf(elemProp, sizeof(elemProp), "%s_index", name);
    configGetInt(themeConfig, elemProp, &mutableImage->recentIndex);
    if (mutableImage->recentIndex < 0)
        mutableImage->recentIndex = 0;

    if (mutableImage->cache)
        elem->drawElem = &drawRecentImage;
    else
        LOG("THEMES RecentImage %s: NO pattern, elem disabled !!\n", name);
}

static void drawRecentText(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    mutable_text_t *mutableText = (mutable_text_t *)elem->extended;
    const char *title = oplRecentTitle(mutableText->displayMode);

    if (title == NULL || title[0] == '\0')
        return;

    if (mutableText->sizingMode == SIZING_NONE)
        fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, 0, 0, title, elem->color);
    else
        fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, elem->width, elem->height, title, elem->color);
}

static void initRecentText(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name)
{
    char elemProp[64];
    int index = 0;

    // Reuses mutable_text_t. `displayMode` carries the recent index, because a
    // RecentText has no use for the label/value display rules.
    elem->extended = initMutableText(themePath, themeConfig, theme, name, ELEM_TYPE_STATIC_TEXT, elem, "", NULL, DISPLAY_ALWAYS, SIZING_NONE);
    elem->endElem = &endMutableText;

    snprintf(elemProp, sizeof(elemProp), "%s_index", name);
    configGetInt(themeConfig, elemProp, &index);
    if (index < 0)
        index = 0;
    ((mutable_text_t *)elem->extended)->displayMode = index;

    elem->drawElem = &drawRecentText;
}

static void drawItemsList(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    if (item) {
        items_list_t *itemsList = (items_list_t *)elem->extended;

        int posX, posY;

        if (itemsList->columns > 1) {
            // Grid mode does its own alignment: it has to centre on the
            // widescreen-corrected width, not the declared one.
            drawItemsListGrid(menu, item, elem, itemsList, elem->posX, elem->posY);
            return;
        }

        posX = elem->posX;
        posY = elem->posY;
        if (elem->aligned) {
            posX -= elem->width >> 1;
            posY -= elem->height >> 1;
        }

        submenu_list_t *ps = menu->item->pagestart;
        int others = 0;
        u64 color;
        while (ps && (others++ < itemsList->displayedItems)) {
            if (ps == item)
                color = gTheme->selTextColor;
            else
                color = elem->color;

            if (itemsList->decoratorImage) {
                GSTEXTURE *itemIconTex = getGameImageTexture(itemsList->decoratorImage->cache, menu->item->userdata, &ps->item);
                if (itemIconTex && itemIconTex->Mem)
                    rmDrawPixmap(itemIconTex, posX, posY, elem->aligned, DECORATOR_SIZE, DECORATOR_SIZE, elem->scaled, gDefaultCol);
                else {
                    if (itemsList->decoratorImage->defaultTexture)
                        rmDrawPixmap(&itemsList->decoratorImage->defaultTexture->source, posX, posY, elem->aligned, DECORATOR_SIZE, DECORATOR_SIZE, elem->scaled, gDefaultCol);
                }
                fntRenderString(elem->font, elem->posX + DECORATOR_SIZE, posY, elem->aligned, elem->width, elem->height, submenuItemGetText(&ps->item), color);
            } else
                fntRenderString(elem->font, elem->posX, posY, elem->aligned, elem->width, elem->height, submenuItemGetText(&ps->item), color);

            posY += MENU_ITEM_HEIGHT;
            ps = ps->next;
        }
    }
}

static void initItemsList(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *elem, const char *name, const char *decorator)
{
    char elemProp[64];

    items_list_t *itemsList = (items_list_t *)malloc(sizeof(items_list_t));

    if (elem->width == DIM_UNDEF)
        elem->width = screenWidth;

    if (elem->height == DIM_UNDEF)
        elem->height = theme->usedHeight - (MENU_POS_V + HINT_HEIGHT);

    itemsList->columns = 1;
    snprintf(elemProp, sizeof(elemProp), "%s_columns", name);
    configGetInt(themeConfig, elemProp, &itemsList->columns);
    if (itemsList->columns < 1)
        itemsList->columns = 1;

    itemsList->gap = 4;
    snprintf(elemProp, sizeof(elemProp), "%s_gap", name);
    configGetInt(themeConfig, elemProp, &itemsList->gap);
    if (itemsList->gap < 0)
        itemsList->gap = 0;

    itemsList->showText = 1;
    snprintf(elemProp, sizeof(elemProp), "%s_text", name);
    configGetInt(themeConfig, elemProp, &itemsList->showText);

    itemsList->labelHeight = MENU_ITEM_HEIGHT;
    snprintf(elemProp, sizeof(elemProp), "%s_label_height", name);
    configGetInt(themeConfig, elemProp, &itemsList->labelHeight);
    if (itemsList->labelHeight < 1)
        itemsList->labelHeight = MENU_ITEM_HEIGHT;

    itemsList->frameSize = (itemsList->columns > 1) ? 2 : 0;
    snprintf(elemProp, sizeof(elemProp), "%s_frame", name);
    configGetInt(themeConfig, elemProp, &itemsList->frameSize);
    if (itemsList->frameSize < 0)
        itemsList->frameSize = 0;

    if (itemsList->columns > 1) {
        // Cell pitch defaults to an even division of the element box.
        itemsList->cellWidth = elem->width / itemsList->columns;
        snprintf(elemProp, sizeof(elemProp), "%s_cell_width", name);
        configGetInt(themeConfig, elemProp, &itemsList->cellWidth);

        itemsList->cellHeight = itemsList->cellWidth;
        snprintf(elemProp, sizeof(elemProp), "%s_cell_height", name);
        configGetInt(themeConfig, elemProp, &itemsList->cellHeight);

        if (itemsList->cellWidth < 1)
            itemsList->cellWidth = 1;
        if (itemsList->cellHeight < 1)
            itemsList->cellHeight = 1;

        int rows = elem->height / itemsList->cellHeight;
        if (rows < 1)
            rows = 1;
        itemsList->displayedItems = rows * itemsList->columns;
    } else {
        itemsList->cellWidth = elem->width;
        itemsList->cellHeight = MENU_ITEM_HEIGHT;
        itemsList->displayedItems = elem->height / MENU_ITEM_HEIGHT;
    }

    LOG("THEMES ItemsList %s: displaying %d elems in %d column(s), cell: %dx%d\n", name, itemsList->displayedItems, itemsList->columns, itemsList->cellWidth, itemsList->cellHeight);

    itemsList->decorator = NULL;
    snprintf(elemProp, sizeof(elemProp), "%s_decorator", name);
    configGetStr(themeConfig, elemProp, &decorator);
    if (decorator)
        itemsList->decorator = decorator; // Will be used later (thmValidate)

    itemsList->decoratorImage = NULL;

    elem->extended = itemsList;
    // elem->endElem = &endBasic; does the job

    elem->drawElem = &drawItemsList;
}

static void drawItemText(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    if (item) {
        item_list_t *support = menu->item->userdata;
        fntRenderString(elem->font, elem->posX, elem->posY, elem->aligned, 0, 0, support->itemGetStartup(support, item->item.id), elem->color);
    }
}

static void drawHintText(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    menu_hint_item_t *hint = menu->item->hints;
    if (hint) {
        int x = elem->posX;

        if (elem->aligned)
            x = guiAlignMenuHints(hint, elem->font, elem->width);

        for (; hint; hint = hint->next) {
            x = guiDrawIconAndText(hint->icon_id, hint->text_id, elem->font, x, elem->posY, elem->color);
            x += elem->width;
        }
    }
}

static void drawInfoHintText(struct menu_list *menu, struct submenu_list *item, config_set_t *config, struct theme_element *elem)
{
    int infoHints[2] = {_STR_RUN, _STR_BACK};
    int infoIcons[2] = {CIRCLE_ICON, CROSS_ICON};
    int x = elem->posX;

    /* A theme that draws its own launch affordance does not want OPL saying
     * "Run" a second time. Back is never hidden -- it is the only way off this
     * screen, and a theme cannot be allowed to strand the user. */
    if (!elem->showRun) {
        int backOnly[1] = {_STR_BACK};
        int backIcon[1] = {CROSS_ICON};

        if (elem->aligned)
            x = guiAlignSubMenuHints(1, backOnly, backIcon, elem->font, elem->width, 1);

        guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? infoIcons[1] : infoIcons[0], _STR_BACK, elem->font, x, elem->posY, elem->color);
        return;
    }

    if (elem->aligned)
        x = guiAlignSubMenuHints(2, infoHints, infoIcons, elem->font, elem->width, 1);

    x = guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? infoIcons[0] : infoIcons[1], infoHints[0], elem->font, x, elem->posY, elem->color);
    x += elem->width;
    x = guiDrawIconAndText(gSelectButton == KEY_CIRCLE ? infoIcons[1] : infoIcons[0], infoHints[1], elem->font, x, elem->posY, elem->color);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void validateBackgroundElems(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_elems_t *mainElems, theme_elems_t *infoElems)
{
    if (!mainElems->first || (mainElems->first->type != ELEM_TYPE_BACKGROUND)) {
        LOG("THEMES No valid background found for main, add default BG_ART\n");
        theme_element_t *backgroundElem = initBasic(themePath, themeConfig, theme, "bg", ELEM_TYPE_BACKGROUND, 0, 0, ALIGN_NONE, screenWidth, screenHeight, SCALING_NONE, gDefaultCol, theme->fonts[0]);
        initBackground(themePath, themeConfig, theme, backgroundElem, "bg", "BG", 1, NULL);
        backgroundElem->next = mainElems->first;
        mainElems->first = backgroundElem;
    }

    if (infoElems->first) {
        if (infoElems->first->type != ELEM_TYPE_BACKGROUND) {
            LOG("THEMES No valid background found for info, add default BG_ART\n");
            theme_element_t *backgroundElem = initBasic(themePath, themeConfig, theme, "bg", ELEM_TYPE_BACKGROUND, 0, 0, ALIGN_NONE, screenWidth, screenHeight, SCALING_NONE, gDefaultCol, theme->fonts[0]);
            initBackground(themePath, themeConfig, theme, backgroundElem, "bg", "BG", 1, NULL);
            backgroundElem->next = infoElems->first;
            infoElems->first = backgroundElem;
        }
    }
}

static void validateItemsList(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_element_t *list, theme_elems_t *mainElems)
{
    if (list) {
        items_list_t *itemsList = (items_list_t *)list->extended;
        if (itemsList->decorator) {
            // Second pass to find the decorator
            theme_element_t *decoratorElem = mainElems->first;
            while (decoratorElem) {
                if (decoratorElem->type == ELEM_TYPE_GAME_IMAGE) {
                    mutable_image_t *gameImage = (mutable_image_t *)decoratorElem->extended;
                    if (!strcmp(itemsList->decorator, gameImage->cache->suffix)) {
                        // if user want to cache less than displayed items, then disable itemslist icons, if not would load constantly
                        if (gameImage->cache->count >= itemsList->displayedItems)
                            itemsList->decoratorImage = gameImage;
                        break;
                    }
                }

                decoratorElem = decoratorElem->next;
            }
            itemsList->decorator = NULL;
        }
    } else {
        LOG("THEMES No itemsList found, adding a default one\n");
        list = initBasic(themePath, themeConfig, theme, "il", ELEM_TYPE_ITEMS_LIST, 42, 42, ALIGN_NONE, 373, 316, SCALING_RATIO, theme->textColor, theme->fonts[0]);
        initItemsList(themePath, themeConfig, theme, list, "il", NULL);
        list->next = mainElems->first->next; // Position the itemsList as second element (right after the Background)
        mainElems->first->next = list;
    }
}

static void validateGUIElems(const char *themePath, config_set_t *themeConfig, theme_t *theme)
{
    // 1. check we have a valid Background elements
    validateBackgroundElems(themePath, themeConfig, theme, &theme->mainElems, &theme->infoElems);
    validateBackgroundElems(themePath, themeConfig, theme, &theme->appsMainElems, &theme->appsInfoElems);

    // 2. check we have a valid ItemsList element, and link its decorator to the target element
    validateItemsList(themePath, themeConfig, theme, theme->gamesItemsList, &theme->mainElems);
    validateItemsList(themePath, themeConfig, theme, theme->appsItemsList, &theme->appsMainElems);
}

static int addGUIElem(const char *themePath, config_set_t *themeConfig, theme_t *theme, theme_elems_t *elems, const char *type, const char *name)
{
    int enabled = 1;
    char elemProp[64];
    theme_element_t *elem = NULL;

    snprintf(elemProp, sizeof(elemProp), "%s_enabled", name);
    configGetInt(themeConfig, elemProp, &enabled);

    if (enabled) {
        snprintf(elemProp, sizeof(elemProp), "%s_type", name);
        configGetStr(themeConfig, elemProp, &type);
        if (type) {
            if (!strcmp(elementsType[ELEM_TYPE_ATTRIBUTE_LIST], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_ATTRIBUTE_LIST, 0, 0, ALIGN_NONE, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, theme->textColor, theme->fonts[0]);
                initAttributeList(themePath, themeConfig, theme, elem, name);
            } else if (!strcmp(elementsType[ELEM_TYPE_ATTRIBUTE_TEXT], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_ATTRIBUTE_TEXT, 0, 0, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, theme->textColor, theme->fonts[0]);
                initAttributeText(themePath, themeConfig, theme, elem, name);
            } else if (!strcmp(elementsType[ELEM_TYPE_STATIC_TEXT], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_STATIC_TEXT, 0, 0, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, theme->textColor, theme->fonts[0]);
                initStaticText(themePath, themeConfig, theme, elem, name);
            } else if (!strcmp(elementsType[ELEM_TYPE_GAME_COUNT_TEXT], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_STATIC_TEXT, 0, 0, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, theme->textColor, theme->fonts[0]);
                initGameCountText(themePath, themeConfig, theme, elem, name);
            } else if (!strcmp(elementsType[ELEM_TYPE_ATTRIBUTE_IMAGE], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_ATTRIBUTE_IMAGE, 0, 0, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
                initAttributeImage(themePath, themeConfig, theme, elem, name);
            } else if (!strcmp(elementsType[ELEM_TYPE_GAME_IMAGE], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_GAME_IMAGE, 0, 0, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
                initGameImage(themePath, themeConfig, theme, elem, name, NULL, 1, NULL, NULL);
            } else if (!strcmp(elementsType[ELEM_TYPE_STATIC_IMAGE], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_STATIC_IMAGE, 0, 0, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
                initStaticImage(themePath, themeConfig, theme, elem, name, NULL);
            } else if (!strcmp(elementsType[ELEM_TYPE_BACKGROUND], type)) {
                if (!elems->first) { // Background elem can only be the first one
                    elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_BACKGROUND, 0, 0, ALIGN_NONE, screenWidth, screenHeight, SCALING_NONE, gDefaultCol, theme->fonts[0]);
                    initBackground(themePath, themeConfig, theme, elem, name, NULL, 1, NULL);
                }
            } else if (!strcmp(elementsType[ELEM_TYPE_MENU_ICON], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_MENU_ICON, screenWidth >> 1, 400, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
                elem->drawElem = &drawMenuIcon;
            } else if (!strcmp(elementsType[ELEM_TYPE_MENU_TEXT], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_MENU_TEXT, screenWidth >> 1, 20, ALIGN_CENTER, 200, 20, SCALING_RATIO, theme->textColor, theme->fonts[0]);
                elem->drawElem = &drawMenuText;
            } else if (!strcmp(elementsType[ELEM_TYPE_ITEMS_LIST], type)) {
                if (!theme->gamesItemsList) {
                    elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_ITEMS_LIST, 0, 0, ALIGN_NONE, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, theme->textColor, theme->fonts[0]);
                    initItemsList(themePath, themeConfig, theme, elem, name, NULL);
                    theme->gamesItemsList = elem;
                } else if (!theme->appsItemsList) {
                    elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_ITEMS_LIST, 42, 42, ALIGN_NONE, 400, 360, SCALING_RATIO, theme->textColor, theme->fonts[0]);
                    initItemsList(themePath, themeConfig, theme, elem, name, NULL);
                    theme->appsItemsList = elem;
                }
            } else if (!strcmp(elementsType[ELEM_TYPE_ITEM_ICON], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_GAME_IMAGE, 0, 0, ALIGN_CENTER, 64, 64, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
                initGameImage(themePath, themeConfig, theme, elem, name, "ICO", 20, NULL, NULL);
            } else if (!strcmp(elementsType[ELEM_TYPE_ITEM_COVER], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_GAME_IMAGE, 0, 0, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
                initGameImage(themePath, themeConfig, theme, elem, name, "COV", 10, NULL, NULL);
            } else if (!strcmp(elementsType[ELEM_TYPE_ITEM_TEXT], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_ITEM_TEXT, 0, 0, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, theme->textColor, theme->fonts[0]);
                elem->drawElem = &drawItemText;
            } else if (!strcmp(elementsType[ELEM_TYPE_HINT_TEXT], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_HINT_TEXT, 16, -HINT_HEIGHT, ALIGN_NONE, 12, 20, SCALING_RATIO, theme->textColor, theme->fonts[0]);
                elem->drawElem = &drawHintText;
            } else if (!strcmp(elementsType[ELEM_TYPE_INFO_HINT_TEXT], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_INFO_HINT_TEXT, 16, -HINT_HEIGHT, ALIGN_NONE, 12, 20, SCALING_RATIO, theme->textColor, theme->fonts[0]);
                int showRun = 1;
                snprintf(elemProp, sizeof(elemProp), "%s_show_run", name);
                configGetInt(themeConfig, elemProp, &showRun);
                elem->showRun = showRun != 0;
                elem->drawElem = &drawInfoHintText;
            } else if (!strcmp(elementsType[ELEM_TYPE_LOADING_ICON], type)) {
                if (!theme->loadingIcon)
                    theme->loadingIcon = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_LOADING_ICON, -40, -60, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
            } else if (!strcmp(elementsType[ELEM_TYPE_MENU_TABS], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_MENU_TABS, screenWidth >> 1, 20, ALIGN_CENTER, 16, 24, SCALING_RATIO, theme->textColor, theme->fonts[0]);
                initMenuTabs(themePath, themeConfig, theme, elem, name);
            } else if (!strcmp(elementsType[ELEM_TYPE_RECENT_IMAGE], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_GAME_IMAGE, 0, 0, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
                initRecentImage(themePath, themeConfig, theme, elem, name);
            } else if (!strcmp(elementsType[ELEM_TYPE_RECENT_TEXT], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_STATIC_TEXT, 0, 0, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, theme->textColor, theme->fonts[0]);
                initRecentText(themePath, themeConfig, theme, elem, name);
            } else if (!strcmp(elementsType[ELEM_TYPE_BDM_INDEX], type)) {
                elem = initBasic(themePath, themeConfig, theme, name, ELEM_TYPE_BDM_INDEX, screenWidth >> 1, 355, ALIGN_CENTER, DIM_UNDEF, DIM_UNDEF, SCALING_RATIO, gDefaultCol, theme->fonts[0]);
                elem->drawElem = &drawBDMIndex;
            }

            if (elem) {
                if (!elems->first)
                    elems->first = elem;

                if (!elems->last)
                    elems->last = elem;
                else {
                    elems->last->next = elem;
                    elems->last = elem;
                }
            }
        } else
            return 0; // ends the reading of elements
    }

    return 1;
}

static void freeGUIElems(theme_elems_t *elems)
{
    theme_element_t *elem = elems->first;
    while (elem) {
        elems->first = elem->next;
        elem->endElem(elem);
        elem = elems->first;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

GSTEXTURE *thmGetTexture(unsigned int id)
{
    if (id >= TEXTURES_COUNT)
        return NULL;
    else {
        // see if the texture is valid
        GSTEXTURE *txt = &gTheme->textures[id];

        if (txt->Mem)
            return txt;
        else
            return NULL;
    }
}

static void thmFree(theme_t *theme)
{
    if (theme) {
        // free elements
        freeGUIElems(&theme->mainElems);
        freeGUIElems(&theme->infoElems);
        freeGUIElems(&theme->appsMainElems);
        freeGUIElems(&theme->appsInfoElems);

        // free textures
        GSTEXTURE *texture;
        int id = 0;
        for (; id < TEXTURES_COUNT; id++) {
            texture = &theme->textures[id];
            if (texture->Mem != NULL) {
                rmUnloadTexture(texture);
                texFree(texture);
            }
        }

        // free fonts
        for (id = 0; id < THM_MAX_FONTS; ++id)
            fntRelease(theme->fonts[id]);

        free(theme);
    }
}

static int thmReadEntry(int index, const char *path, const char *separator, const char *name, unsigned char d_type)
{
    if (d_type == DT_DIR && strstr(name, "thm_")) {
        theme_file_t *currTheme = &themes[nThemes + index];

        int length = strlen(name) - 4 + 1;
        currTheme->name = (char *)malloc(length * sizeof(char));
        memcpy(currTheme->name, name + 4, length);
        currTheme->name[length - 1] = '\0';

        length = strlen(path) + 1 + strlen(name) + 1 + 1;
        currTheme->filePath = (char *)malloc(length * sizeof(char));
        sprintf(currTheme->filePath, "%s%s%s%s", path, separator, name, separator);

        LOG("THEMES Theme found: %s\n", currTheme->filePath);

        index++;
    }
    return index;
}

/* themePath must contains the leading separator (as it is dependent of the device, we can't know here) */
static int thmLoadResource(GSTEXTURE *texture, int texId, const char *themePath, short psm, int useDefault)
{
    int success = -1;

    if (themePath != NULL)
        success = texDiscoverLoad(texture, themePath, texId); // only set success here

    if ((success < 0) && useDefault)
        texLoadInternal(texture, texId); // we don't mind the result of "default"

    return success;
}

static void thmSetColors(theme_t *theme)
{
    memcpy(theme->bgColor, gDefaultBgColor, 3);
    theme->textColor = GS_SETREG_RGBA(gDefaultTextColor[0], gDefaultTextColor[1], gDefaultTextColor[2], 0x80);
    theme->uiTextColor = GS_SETREG_RGBA(gDefaultUITextColor[0], gDefaultUITextColor[1], gDefaultUITextColor[2], 0x80);
    theme->selTextColor = GS_SETREG_RGBA(gDefaultSelTextColor[0], gDefaultSelTextColor[1], gDefaultSelTextColor[2], 0x80);

    theme_element_t *elem = theme->mainElems.first;
    while (elem) {
        elem->color = theme->textColor;
        elem = elem->next;
    }
}

static void thmLoadFonts(config_set_t *themeConfig, const char *themePath, theme_t *theme)
{
    int fntID; // theme side font id, not the fntSys handle
    for (fntID = 0; fntID < THM_MAX_FONTS; ++fntID) {
        // does the font by the key exist?
        char fntKey[16];

        if (fntID == 0) {
            snprintf(fntKey, sizeof(fntKey), "default_font");
            theme->fonts[0] = FNT_DEFAULT;
        } else {
            snprintf(fntKey, sizeof(fntKey), "font%d", fntID);
            theme->fonts[fntID] = theme->fonts[0];
        }

        char fullPath[128];
        const char *fntFile;
        if (configGetStr(themeConfig, fntKey, &fntFile)) {
            snprintf(fullPath, sizeof(fullPath), "%s%s", themePath, fntFile);

            int fontSize;
            char sizeKey[64];
            if (fntID == 0)
                snprintf(sizeKey, sizeof(sizeKey), "default_font_size");
            else
                snprintf(sizeKey, sizeof(sizeKey), "font%d_size", fntID);

            if (!configGetInt(themeConfig, sizeKey, &fontSize) || fontSize <= 0)
                fontSize = FNTSYS_DEFAULT_SIZE;

            int fntHandle = fntLoadFile(fullPath, fontSize);
            // Do we have a valid font? Assign the font handle to the theme font slot
            if (fntHandle != FNT_ERROR)
                theme->fonts[fntID] = fntHandle;
        }
    }
}

static void thmLoad(const char *themePath)
{
    LOG("THEMES Load theme path=%s\n", themePath);
    char path[256];
    theme_t *curT = gTheme;
    theme_t *newT = (theme_t *)malloc(sizeof(theme_t));
    memset(newT, 0, sizeof(theme_t));

    newT->useDefault = 1;
    newT->usedHeight = 480;
    thmSetColors(newT);
    newT->mainElems.first = NULL;
    newT->mainElems.last = NULL;
    newT->infoElems.first = NULL;
    newT->infoElems.last = NULL;
    newT->appsMainElems.first = NULL;
    newT->appsMainElems.last = NULL;
    newT->appsInfoElems.first = NULL;
    newT->appsInfoElems.last = NULL;
    newT->gameCacheCount = 0;
    newT->itemsList = NULL;
    newT->gamesItemsList = NULL;
    newT->appsItemsList = NULL;
    newT->loadingIcon = NULL;
    newT->loadingIconCount = LOAD7_ICON - LOAD0_ICON + 1;

    config_set_t *themeConfig = NULL;
    if (!themePath) {
        // No theme specified. Prepare and load the default theme.
        themeConfig = configAlloc(0, NULL, NULL);
        configReadBuffer(themeConfig, &conf_theme_OPL_cfg, size_conf_theme_OPL_cfg);
    } else {
        snprintf(path, sizeof(path), "%sconf_theme.cfg", themePath);
        themeConfig = configAlloc(0, NULL, path);
        configRead(themeConfig); // try to load the theme config file. If it does not exist, defaults will be used.
    }

    int intValue;
    if (configGetInt(themeConfig, "use_default", &intValue))
        newT->useDefault = intValue;

    if (configGetInt(themeConfig, "use_real_height", &intValue)) {
        if (intValue)
            newT->usedHeight = screenHeight;
    }

    configGetColor(themeConfig, "bg_color", newT->bgColor);

    unsigned char color[3];
    if (configGetColor(themeConfig, "text_color", color))
        newT->textColor = GS_SETREG_RGBA(color[0], color[1], color[2], 0x80);

    if (configGetColor(themeConfig, "ui_text_color", color))
        newT->uiTextColor = GS_SETREG_RGBA(color[0], color[1], color[2], 0x80);

    if (configGetColor(themeConfig, "sel_text_color", color))
        newT->selTextColor = GS_SETREG_RGBA(color[0], color[1], color[2], 0x80);

    // before loading the element definitions, we have to have the fonts prepared
    // for that, we load the fonts and a translation table
    if (themePath)
        thmLoadFonts(themeConfig, themePath, newT);

    int i = 1, j;
    snprintf(path, sizeof(path), "main0");
    while (addGUIElem(themePath, themeConfig, newT, &newT->mainElems, NULL, path))
        snprintf(path, sizeof(path), "main%d", i++);

    for (j = 0; j < i; j++) {
        snprintf(path, sizeof(path), "appsMain%d", j);

        if (addGUIElem(themePath, themeConfig, newT, &newT->appsMainElems, NULL, path))
            continue;
        else {
            snprintf(path, sizeof(path), "main%d", j);
            addGUIElem(themePath, themeConfig, newT, &newT->appsMainElems, NULL, path);
        }
    }

    i = 1;
    snprintf(path, sizeof(path), "info0");
    while (addGUIElem(themePath, themeConfig, newT, &newT->infoElems, NULL, path))
        snprintf(path, sizeof(path), "info%d", i++);

    for (j = 0; j < i; j++) {
        snprintf(path, sizeof(path), "appsInfo%d", j);

        if (addGUIElem(themePath, themeConfig, newT, &newT->appsInfoElems, NULL, path))
            continue;
        else {
            snprintf(path, sizeof(path), "info%d", j);
            addGUIElem(themePath, themeConfig, newT, &newT->appsInfoElems, NULL, path);
        }
    }

    if (themePath)
        validateGUIElems(themePath, themeConfig, newT);

    newT->itemsList = newT->gamesItemsList;

    configFree(themeConfig);

    LOG("THEMES Number of cache: %d\n", newT->gameCacheCount);
    LOG("THEMES Used height: %d\n", newT->usedHeight);

    // default all to not loaded...
    for (i = 0; i < TEXTURES_COUNT; i++)
        newT->textures[i].Mem = NULL;

    // LOGO, loaded here to avoid flickering during startup with device in AUTO + theme set
    texLoadInternal(&newT->textures[LOGO_PICTURE], LOGO_PICTURE);

    // First start with busy icon
    const char *themePath_temp = themePath;
    int customBusy = 0;
    for (i = LOAD0_ICON; i <= LOAD7_ICON; i++) {
        if (thmLoadResource(&newT->textures[i], i, themePath_temp, GS_PSM_CT32, newT->useDefault) >= 0)
            customBusy = 1;
        else {
            if (customBusy)
                break;
            else
                themePath_temp = NULL;
        }
    }
    newT->loadingIconCount = i;

    // Customizable icons
    for (i = BDM_ICON; i <= START_ICON; i++)
        thmLoadResource(&newT->textures[i], i, themePath, GS_PSM_CT32, newT->useDefault);

    /* Not customizable icons - currently unused.
    for (i = L1_ICON; i <= R3_ICON; i++)
        thmLoadResource(&newT->textures[i], i, NULL, GS_PSM_CT32, 1); */

    if (!themePath)
        for (i = ELF_FORMAT; i <= VMODE_PAL; i++)
            thmLoadResource(&newT->textures[i], i, NULL, GS_PSM_CT32, 1);

    gTheme = newT;
    thmFree(curT);
}

static void thmRebuildGuiNames(void)
{
    if (guiThemesNames)
        free(guiThemesNames);

    // build the themes name list
    guiThemesNames = (const char **)malloc((nThemes + 2) * sizeof(char **));

    // add default internal
    guiThemesNames[0] = "<OPL>";

    int i = 0;
    for (; i < nThemes; i++) {
        guiThemesNames[i + 1] = themes[i].name;
    }

    guiThemesNames[nThemes + 1] = NULL;
}

int thmAddElements(char *path, const char *separator, int forceRefresh)
{
    int result, i;

    result = listDir(path, separator, THM_MAX_FILES - nThemes, &thmReadEntry);
    nThemes += result;
    thmRebuildGuiNames();

    const char *temp;
    if (configGetStr(configGetByType(CONFIG_OPL), "theme", &temp)) {
        LOG("THEMES Trying to set again theme: %s\n", temp);
        if (thmSetGuiValue(thmFindGuiID(temp), 0) && forceRefresh) {
            for (i = 0; i < MODE_COUNT; i++)
                moduleUpdateMenu(i, 1, 0);
        }
    }

    return result;
}

void thmInit(void)
{
    LOG("THEMES Init\n");
    gTheme = NULL;

    thmReloadScreenExtents();

    // initialize default internal
    thmLoad(NULL);

    thmAddElements(gBaseMCDir, "/", 0);
}

void thmReinit(const char *path)
{
    thmLoad(NULL);
    guiThemeID = 0;

    int i = 0;
    while (i < nThemes) {
        if (strncmp(themes[i].filePath, path, strlen(path)) == 0) {
            LOG("THEMES Remove theme: %s\n", themes[i].filePath);
            nThemes--;
            free(themes[i].name);
            themes[i].name = themes[nThemes].name;
            themes[nThemes].name = NULL;
            free(themes[i].filePath);
            themes[i].filePath = themes[nThemes].filePath;
            themes[nThemes].filePath = NULL;
        } else
            i++;
    }

    thmRebuildGuiNames();
}

void thmReloadScreenExtents(void)
{
    rmGetScreenExtents(&screenWidth, &screenHeight);
}

const char *thmGetValue(void)
{
    return guiThemesNames[guiThemeID];
}

int thmSetGuiValue(int themeID, int reload)
{
    if (themeID != -1) {
        if (guiThemeID != themeID || reload) {
            thmLoad(themeID != 0 ? themes[themeID - 1].filePath : NULL);

            guiThemeID = themeID;
            return 1;
        } else if (guiThemeID == 0)
            thmSetColors(gTheme);
    }
    return 0;
}

int thmGetGuiValue(void)
{
    return guiThemeID;
}

int thmFindGuiID(const char *theme)
{
    if (theme) {
        int i = 0;
        for (; i < nThemes; i++) {
            if (strcasecmp(themes[i].name, theme) == 0)
                return i + 1;
        }
    }
    return 0;
}

const char **thmGetGuiList(void)
{
    return guiThemesNames;
}

char *thmGetFilePath(int themeID)
{
    theme_file_t *currTheme = &themes[themeID - 1];
    char *path = currTheme->filePath;

    return path;
}

void thmEnd(void)
{
    thmFree(gTheme);

    int i = 0;
    for (; i < nThemes; i++) {
        free(themes[i].name);
        free(themes[i].filePath);
    }

    free(guiThemesNames);
}
