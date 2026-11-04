#ifndef APP_UI_H
#define APP_UI_H

/*
 * The LVGL task.
 *
 * All LVGL calls happen in this one task, protected by a mutex. The task
 * drives lv_timer_handler(), polls the network state (WiFi/MQTT) and
 * refreshes the taskbar clock once per minute.
 */

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the Win11 desktop and start the LVGL task. */
void app_ui_start(lv_display_t *display);

#ifdef __cplusplus
}
#endif

#endif /* APP_UI_H */
