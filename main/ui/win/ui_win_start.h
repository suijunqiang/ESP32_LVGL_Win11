#ifndef UI_WIN_START_H
#define UI_WIN_START_H

/*
 * The Windows 11 start menu.
 *
 * The menu is an overlay panel that lives on top of the desktop: a search
 * field, the "Pinned" grid of applications, a "Recommended" list and the
 * user footer. It is created by the desktop view, which decides where the
 * panel is placed and what happens when an application is picked.
 */

#include "lvgl.h"
#include "ui_win_icons.h"

/* Called when the user picks an application in the start menu. */
typedef void (*ui_win_start_cb_t)(win_app_id_t app, void *user_data);

/*
 * Create the start menu. The panel is created hidden, use
 * ui_win_start_show() / ui_win_start_hide() to toggle it.
 */
lv_obj_t *ui_win_start_create(lv_obj_t *parent,
                              ui_win_start_cb_t on_launch,
                              void *user_data);

/* Show / hide the start menu. */
void ui_win_start_show(lv_obj_t *start);
void ui_win_start_hide(lv_obj_t *start);
bool ui_win_start_is_visible(lv_obj_t *start);

#endif /* UI_WIN_START_H */
