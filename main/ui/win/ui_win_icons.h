#ifndef UI_WIN_ICONS_H
#define UI_WIN_ICONS_H

/*
 * The application catalogue of the Windows 11 demo.
 *
 * One table drives every place an app is shown: the desktop shortcuts, the
 * pinned grid of the start menu and the taskbar. Adding an app means adding
 * one line here - the widgets pick up the icon, the label and the colour
 * automatically.
 *
 * The icons are the built-in LVGL symbol font, so the demo needs no image
 * assets at all.
 */

#include "lvgl.h"

/* Every application known by the demo. */
typedef enum {
    WIN_APP_EDGE = 0,
    WIN_APP_EXPLORER,
    WIN_APP_SETTINGS,
    WIN_APP_STORE,
    WIN_APP_NOTEPAD,
    WIN_APP_MAIL,
    WIN_APP_MUSIC,
    WIN_APP_PHOTOS,
    WIN_APP_TERMINAL,
    WIN_APP_WIFI,
    WIN_APP_MQTT,
    WIN_APP_BT,
    WIN_APP_CAM,
    WIN_APP_UI,
    WIN_APP_COUNT
} win_app_id_t;

/* Description of one application. */
typedef struct {
    win_app_id_t id;
    const char *symbol;  /* LV_SYMBOL_* drawn on the tile          */
    const char *label;   /* caption under the tile / window title */
    lv_color_t color;    /* colour of the tile                    */
} win_app_t;

/* The full catalogue. */
extern const win_app_t win_apps[WIN_APP_COUNT];

/* Look up an application by id, never returns NULL. */
const win_app_t *win_app_get(win_app_id_t id);

/* The applications pinned to the taskbar (in taskbar order). */
extern const win_app_id_t win_taskbar_apps[];
extern const unsigned win_taskbar_app_count;

/* The applications pinned to the "Pinned" grid of the start menu. */
extern const win_app_id_t win_start_pinned[];
extern const unsigned win_start_pinned_count;

/* Desktop shortcuts (not applications, but files and system objects). */
typedef struct {
    const char *symbol;
    const char *label;
    lv_color_t color;
    win_app_id_t app;   /* the window opened when the icon is clicked */
} win_shortcut_t;

extern const win_shortcut_t win_desktop_shortcuts[];
extern const unsigned win_desktop_shortcut_count;

#endif /* UI_WIN_ICONS_H */
