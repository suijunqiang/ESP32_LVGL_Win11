/*
 * The LVGL task.
 *
 * Everything that touches LVGL runs here. The network tasks only set a
 * flag (app_wifi_is_connected / app_mqtt_is_connected) and this task
 * mirrors that state into the Win11 system tray.
 */

#include "app_ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app_mqtt.h"
#include "app_wifi.h"
#include "app_i18n.h"
#include "screenshot.h"
#include "ui_manager.h"
#include "ui_views.h"
#include "ui/win/ui_win_desktop.h"

/* LVGL 9 renders transformed, clipped objects through a draw layer; the
 * drawing call chain is deep, so 8 KiB overflows when an app uses
 * transform_scale (the honeycomb menu of the UI demo). */
#define UI_TASK_STACK   16384
#define UI_TASK_PRIO    3

static const char *TAG = "ui";

static SemaphoreHandle_t s_gui_lock;

/* ------------------------------------------------------------------ */
/* Task                                                               */
/* ------------------------------------------------------------------ */

static void ui_task(void *arg)
{
    (void)arg;

    char clock_text[8] = "--:--";
    bool wifi_ok = false;
    bool mqtt_ok = false;

    for (;;) {
        if (xSemaphoreTake(s_gui_lock, portMAX_DELAY) == pdTRUE) {
            /* Refresh the taskbar clock once a minute (SNTP keeps time()). */
            time_t now = time(NULL);
            if (now > 1700000000) {
                struct tm local;
                localtime_r(&now, &local);
                char next[8];
                strftime(next, sizeof(next), "%H:%M", &local);
                if (strcmp(next, clock_text) != 0) {
                    strcpy(clock_text, next);
                    ui_win_desktop_set_clock(clock_text);
                }
            }

            bool wifi = app_wifi_is_connected();
            bool mqtt = app_mqtt_is_connected();
            if (wifi != wifi_ok || mqtt != mqtt_ok) {
                wifi_ok = wifi;
                mqtt_ok = mqtt;
                ui_win_desktop_set_status(wifi_ok, mqtt_ok);
                ESP_LOGI(TAG, "tray: wifi=%d mqtt=%d", wifi_ok, mqtt_ok);
            }

            uint32_t wait_ms = lv_timer_handler();

            /* Check for pending screenshot request (must run on LVGL task). */
            screenshot_check_and_do(NULL);  /* NULL OK — uses lv_display_get_default() internally */

            xSemaphoreGive(s_gui_lock);

            if (wait_ms == 0 || wait_ms > 20) {
                wait_ms = 10;
            }
            vTaskDelay(pdMS_TO_TICKS(wait_ms));
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public entry point                                                 */
/* ------------------------------------------------------------------ */

void app_ui_start(lv_display_t *display)
{
    (void)display;

    s_gui_lock = xSemaphoreCreateMutex();
    if (s_gui_lock == NULL) {
        ESP_LOGE(TAG, "no memory for the LVGL mutex");
        return;
    }

    /*
     * Build the desktop before the task exists: still single threaded.
     * The manager drives the view lifecycle (init -> open) and later
     * pause/resume/close/deinit when navigation happens.
     */
    app_i18n_init();
    ui_views_register_all();
    if (!ui_manager_init_with(VIEW_WIN_DESKTOP)) {
        ESP_LOGE(TAG, "cannot start the view manager");
        return;
    }

    /* The internal RAM is tight once Wi-Fi, BLE and the app statics are
     * loaded; the 16 KiB stack does not fit there any more. Task stacks
     * may live in PSRAM on this chip, so allocate it there. */
    BaseType_t ok = xTaskCreateWithCaps(ui_task, "lvgl_task", UI_TASK_STACK,
                                        NULL, UI_TASK_PRIO, NULL,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "cannot create the LVGL task");
        return;
    }

    ESP_LOGI(TAG, "LVGL task started");
}
