/*
 * ESP32-S3 + LVGL 9.6 Windows 11 desktop demo.
 *
 * Boot order:
 *   1. NVS (Wi-Fi driver needs it)
 *   2. Wi-Fi station + SNTP (network runs in the background)
 *   3. MQTT task on mqtt://suijunqiang.top:1883
 *   4. NimBLE host for the Bluetooth app
 *   5. Display, touch and LVGL driver bring-up
 *   6. The LVGL task that owns every LVGL call
 *
 * The display/touch pins were verified on the real board; see
 * board_display.c for the details.
 */

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "app_bt.h"
#include "app_camera.h"
#include "app_mqtt.h"
#include "app_ui.h"
#include "app_wifi.h"
#include "board_display.h"

static const char *TAG = "main";

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "starting Wi-Fi");
    app_wifi_start();

    ESP_LOGI(TAG, "starting MQTT task");
    app_mqtt_start();

    ESP_LOGI(TAG, "starting Bluetooth LE");
    app_bt_start();

    ESP_LOGI(TAG, "starting camera");
    app_camera_init();

    ESP_LOGI(TAG, "starting display and touch");
    lv_display_t *display = NULL;
    ESP_ERROR_CHECK(board_display_init(&display));

    /* Everything below this line is owned by the LVGL task. */
    app_ui_start(display);

    /* Screen captures are requested on demand (MQTT cmd "screenshot").
     * app_main may return; the FreeRTOS scheduler keeps running. */
}
