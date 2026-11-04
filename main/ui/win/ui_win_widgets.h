#ifndef UI_WIN_WIDGETS_H
#define UI_WIN_WIDGETS_H

/*
 * Reusable Windows 11 widgets.
 *
 * The demo draws a few things that LVGL has no ready made version of:
 *
 *   - the Windows logo (four squares),
 *   - the search magnifier,
 *   - the task view and the widgets glyphs,
 *   - the app tiles and the taskbar buttons,
 *   - the app window frame with its caption buttons and title bar drag.
 *
 * All of them are built from plain LVGL objects, so they scale with the
 * display and need no image assets.
 */

#include "lvgl.h"
#include "ui_win_icons.h"
#include "ui_win_theme.h"

/* ------------------------------------------------------------------ */
/* Glyphs                                                             */
/* ------------------------------------------------------------------ */

/* The Windows logo: four squares in a 2 x 2 grid. */
lv_obj_t *win_logo_create(lv_obj_t *parent, lv_coord_t size, lv_color_t color);

/* A magnifier: a ring plus a tilted handle. */
lv_obj_t *win_search_glyph_create(lv_obj_t *parent,
                                  lv_coord_t size,
                                  lv_color_t color);

/* Two overlapping rectangles: the "task view" glyph. */
lv_obj_t *win_taskview_glyph_create(lv_obj_t *parent,
                                    lv_coord_t size,
                                    lv_color_t color);

/* Four coloured squares: the "widgets" glyph. */
lv_obj_t *win_widgets_glyph_create(lv_obj_t *parent,
                                   lv_coord_t size,
                                   lv_color_t color);

/* ------------------------------------------------------------------ */
/* App tiles and taskbar buttons                                       */
/* ------------------------------------------------------------------ */

/* A rounded app tile with the app symbol on it. */
lv_obj_t *win_app_icon_create(lv_obj_t *parent,
                              const win_app_t *app,
                              lv_coord_t size);

/*
 * A taskbar button: a transparent square that holds an app icon and the
 * small running indicator underneath. `indicator` is the object that shows
 * the indicator, it is returned so the view can toggle it.
 */
lv_obj_t *win_taskbar_button_create(lv_obj_t *parent,
                                    const win_app_t *app,
                                    lv_obj_t **indicator);

/* ------------------------------------------------------------------ */
/* App windows                                                        */
/* ------------------------------------------------------------------ */

/* Callbacks of the caption buttons of an app window. */
typedef void (*win_window_action_cb_t)(lv_obj_t *window, void *user_data);

typedef struct {
    win_window_action_cb_t on_minimize;
    win_window_action_cb_t on_maximize;
    win_window_action_cb_t on_close;
} win_window_actions_t;

/*
 * Create an app window. The returned object is the frame; its child with
 * the "content" name is the area below the title bar where the view puts
 * the window content.
 */
lv_obj_t *win_window_create(lv_obj_t *parent,
                             const win_app_t *app,
                             const win_window_actions_t *actions);

/* The content area of an app window. */
lv_obj_t *win_window_get_content(lv_obj_t *window);

/* Maximize a window to the desktop area, or restore its previous geometry. */
void win_window_toggle_maximize(lv_obj_t *window);

/* Raise a window above its siblings. */
void win_window_bring_to_front(lv_obj_t *window);

/* Delete an app window and release its private data. */
void win_window_destroy(lv_obj_t *window);

#endif /* UI_WIN_WIDGETS_H */
