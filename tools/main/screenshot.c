/*
 * LVGL screen capture helper.
 *
 * Forces a full repaint of the active screen, collects every flushed band in
 * a full-screen mirror in PSRAM (so the capture shows exactly what the panel
 * received) and streams it out as PPM6 (RGB888) over UART, framed by
 * SCREENSHOT_BEGIN / SCREENSHOT_END markers.
 *
 * The capture is requested with screenshot_request() (safe from any task,
 * e.g. the MQTT command handler) and performed by the LVGL task through
 * screenshot_check_and_do() after lv_timer_handler().
 */

#include "screenshot.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart_vfs.h"
#include "driver/usb_serial_jtag_vfs.h"

static const char *TAG = "screenshot";

/* Mirror of the last flushed frame (exactly what the panel got). */
#define SHOT_MIRROR_W 320
#define SHOT_MIRROR_H 240

static uint16_t *s_mirror;
static bool s_mirror_on;

void screenshot_mirror_begin(void)
{
    if (s_mirror == NULL) {
        s_mirror = heap_caps_malloc(SHOT_MIRROR_W * SHOT_MIRROR_H * sizeof(uint16_t),
                                    MALLOC_CAP_SPIRAM);
        if (s_mirror == NULL) {
            ESP_LOGE(TAG, "mirror: no PSRAM");
            return;
        }
    }
    memset(s_mirror, 0, SHOT_MIRROR_W * SHOT_MIRROR_H * sizeof(uint16_t));
    s_mirror_on = true;
}

/* px_map is RGB565_SWAPPED (big-endian bytes), one band of the frame. */
void screenshot_mirror_area(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                            const void *px_map, size_t stride)
{
    if (!s_mirror_on || s_mirror == NULL) {
        return;
    }

    if (x1 < 0) {
        x1 = 0;
    }
    if (y1 < 0) {
        y1 = 0;
    }
    if (x2 >= SHOT_MIRROR_W) {
        x2 = SHOT_MIRROR_W - 1;
    }
    if (y2 >= SHOT_MIRROR_H) {
        y2 = SHOT_MIRROR_H - 1;
    }

    for (int32_t y = y1; y <= y2; y++) {
        const uint8_t *src = (const uint8_t *)px_map + (size_t)(y - y1) * stride;
        uint16_t *dst = s_mirror + (size_t)y * SHOT_MIRROR_W + x1;

        for (int32_t x = x1; x <= x2; x++) {
            const uint8_t *p = src + (size_t)(x - x1) * 2;
            dst[x - x1] = (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
        }
    }
}

static int g_screenshot_count = 0;
static bool g_screenshot_requested = false;

void screenshot_request(void)
{
    g_screenshot_requested = true;
}

void screenshot_check_and_do(lv_display_t *disp)
{
    if (!g_screenshot_requested) {
        return;
    }
    g_screenshot_requested = false;

    if (disp == NULL) {
        disp = lv_display_get_default();
    }
    if (disp == NULL) {
        ESP_LOGE(TAG, "no display");
        return;
    }

    /* Capture exactly what the panel receives: force a full repaint, collect
     * every flushed band in the mirror, then stream it. */
    screenshot_mirror_begin();
    if (!s_mirror_on) {
        return;
    }
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(disp);
    s_mirror_on = false;

    const uint8_t *src = (const uint8_t *)s_mirror;
    const int32_t w = SHOT_MIRROR_W;
    const int32_t h = SHOT_MIRROR_H;
    const size_t stride = (size_t)SHOT_MIRROR_W * 2;
    const uint8_t cf = LV_COLOR_FORMAT_RGB565;

    /* One RGB888 row: 3 bytes per pixel. */
    uint8_t *row = malloc((size_t)w * 3);
    if (row == NULL) {
        ESP_LOGE(TAG, "snapshot: no memory for the row buffer");
        return;
    }

    g_screenshot_count++;
    ESP_LOGI(TAG, "screenshot #%d streaming %ldx%ld",
             g_screenshot_count, (long)w, (long)h);

    /*
     * Silence every log tag while the binary image is on the wire, otherwise
     * a background ESP_LOG line would corrupt the byte stream.
     */
    esp_log_level_set("*", ESP_LOG_NONE);

    /* The console defaults to CRLF: every 0x0A byte of the binary image would
     * get a 0x0D inserted in front of it, silently shifting the rest of the
     * stream. Switch both consoles to LF for the duration of the transfer. */
    uart_vfs_dev_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM,
                                          ESP_LINE_ENDINGS_LF);
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_LF);
#endif

    printf("SCREENSHOT_BEGIN #%d %ldx%ld P6\n",
           g_screenshot_count, (long)w, (long)h);
    fflush(stdout);

    const bool argb = (cf == LV_COLOR_FORMAT_ARGB8888 ||
                       cf == LV_COLOR_FORMAT_XRGB8888);
    /* RGB565 (0x12) stores little-endian words; RGB565_SWAPPED (0x1B) is the
     * byte-swapped variant used for panel DMA. Handle both. */
    const bool swapped = (cf == LV_COLOR_FORMAT_RGB565_SWAPPED);

    for (int32_t y = 0; y < h; y++) {
        const uint8_t *src_row = src + (size_t)y * stride;
        uint8_t *dst = row;

        for (int32_t x = 0; x < w; x++) {
            if (argb) {
                /* 4 bytes/pixel: R G B A or X R G B */
                dst[0] = src_row[x * 4 + 0];
                dst[1] = src_row[x * 4 + 1];
                dst[2] = src_row[x * 4 + 2];
            } else {
                uint16_t px = swapped
                    ? (uint16_t)(((uint16_t)src_row[x * 2] << 8) |
                                 src_row[x * 2 + 1])
                    : (uint16_t)(((uint16_t)src_row[x * 2 + 1] << 8) |
                                 src_row[x * 2]);
                dst[0] = (uint8_t)((px >> 11) << 3);
                dst[1] = (uint8_t)(((px >> 5) & 0x3F) << 2);
                dst[2] = (uint8_t)((px & 0x1F) << 3);
            }

            dst += 3;
        }

        fwrite(row, 1, (size_t)w * 3, stdout);
        /* Keep IDLE0 (and the task watchdog) alive while the slow UART drains:
         * a watchdog backtrace printed mid-stream would corrupt the image. */
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    fflush(stdout);
    printf("SCREENSHOT_END #%d\n", g_screenshot_count);
    fflush(stdout);

    uart_vfs_dev_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM,
                                          ESP_LINE_ENDINGS_CRLF);
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
#endif

    /* Restore the default log level. */
    esp_log_level_set("*", ESP_LOG_INFO);

    free(row);

    ESP_LOGI(TAG, "screenshot #%d sent (%ldx%ld)",
             g_screenshot_count, (long)w, (long)h);
}
