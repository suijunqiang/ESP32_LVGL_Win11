#ifndef UI_WIN_DESKTOP_H
#define UI_WIN_DESKTOP_H

/*
 * The Windows 11 desktop view.
 *
 * A full desktop simulation:
 *
 *   ┌──────────────────────────────────────┐
 *   │ [This PC]                            │
 *   │ [Recycle Bin]        desktop icons   │
 *   │ [Documents]                           │
 *   │ [Readme.txt]                         │
 *   │                                      │
 *   │              (app windows)            │  windows can be dragged
 *   │  ╭──────────────────────────╮        │
 *   │  │ start menu               │        │
 *   │  ╰──────────────────────────╯        │
 *   │  [⊞][🔍][▣][▦][🌐][📁][🛍]  ▂▄▆ 10:24 │  taskbar
 *   └──────────────────────────────────────┘
 *
 * Everything is drawn with plain LVGL objects, so the demo needs no image
 * assets. The view follows the same rules as the other views of the
 * application: it is registered in ui_views.c and its widgets are built
 * once, in the init phase.
 */

#include "lvgl.h"
#include <stdbool.h>

/* The six lifecycle functions of the view. */
void ui_win_desktop_init(void);
void ui_win_desktop_open(void);
void ui_win_desktop_pause(void);
void ui_win_desktop_resume(void);
void ui_win_desktop_close(void);
void ui_win_desktop_deinit(void);

/*
 * Live status shown in the system tray. Both functions must be called from
 * the LVGL task (the desktop view owns its widgets), so the network tasks
 * only publish their state and the UI task applies it.
 */
void ui_win_desktop_set_clock(const char *text);
void ui_win_desktop_set_status(bool wifi_ok, bool mqtt_ok);

#endif /* UI_WIN_DESKTOP_H */
