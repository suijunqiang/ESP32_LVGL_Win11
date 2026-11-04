#ifndef BOARD_DISPLAY_H
#define BOARD_DISPLAY_H

/*
 * Display + touch bring-up for this ESP32-S3 board.
 *
 * The pin map below was confirmed with a raw-SPI probe; the tutorial pin
 * map of the vendor example does not respond on this PCB.
 *
 *   LCD : MOSI=47 SCLK=21 MISO=48 CS=42 DC=41, no reset, no backlight
 *   Touch: XPT2046 on CS=46 IRQ=2, sharing the LCD SPI bus
 *
 * board_display_init() initialises the SPI bus, the ILI9341 panel, LVGL,
 * the two draw buffers, the 1 ms tick timer and the touch input device.
 * After it returns the caller may start the LVGL task.
 */

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialise the panel and LVGL. On success `*out_display` receives the
 * LVGL display that the UI task must drive with lv_timer_handler().
 */
esp_err_t board_display_init(lv_display_t **out_display);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_DISPLAY_H */
