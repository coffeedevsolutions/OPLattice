#ifndef __APP_SUPPORT_H
#define __APP_SUPPORT_H

#include "include/iosupport.h"

#define APP_MODE_UPDATE_DELAY 240

#define APP_TITLE_MAX 128
#define APP_PATH_MAX  128
#define APP_BOOT_MAX  64
#define APP_ARGV1_MAX 128
#define APP_SUBTITLE_MAX 64

#define APP_CONFIG_TITLE "title"
#define APP_CONFIG_BOOT  "boot"
#define APP_CONFIG_ARGV1 "argv1"
/* Optional. title.cfg is already parsed into a config_set_t, and configWrite
   preserves keys it does not recognise, so this survives anything OPL writes
   and an older OPL ignores it. */
#define APP_CONFIG_SUBTITLE "subtitle"

#define APP_TITLE_CONFIG_FILE "title.cfg"

typedef struct
{
    char title[APP_TITLE_MAX + 1];
    char path[APP_PATH_MAX + 1];
    char boot[APP_BOOT_MAX + 1];
    char argv1[APP_ARGV1_MAX + 1];
    char subtitle[APP_SUBTITLE_MAX + 1];
    u8 legacy;
} app_info_t;

void appInit(item_list_t *itemList);
item_list_t *appGetObject(int initOnly);
void appPostUpdateCallback(int mode);

/** Borrowed pointer to the apps list, or NULL. Count comes from the vtable's
 *  itemGetCount. Exposed so the SHELF pages can render the same list the
 *  classic screen shows without maintaining a second copy of it. */
const app_info_t *appGetList(void);

#endif
