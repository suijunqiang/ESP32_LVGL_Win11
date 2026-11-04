#ifndef UI_WIN_APP_UI_H
#define UI_WIN_APP_UI_H

/*
 * Content of the "UI" demo application: a left tab bar with three demo
 * pages - the honeycomb app menu, the void (black hole) menu and the silk
 * (coverflow) menu.
 *
 * Only the active page animates. Pause stops all of them (the window was
 * hidden); resume starts the active page again.
 */

#include "lvgl.h"

void win_content_ui(lv_obj_t *content);

/* Stop every animation of the app while its window is hidden. */
void win_app_ui_pause(void);

/* Restart the animation of the active tab (window shown again). */
void win_app_ui_resume(void);

#endif /* UI_WIN_APP_UI_H */
