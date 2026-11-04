#ifndef APP_CAMERA_H
#define APP_CAMERA_H

/*
 * Camera application (OV2640-class sensor on the ESP32-S3).
 *
 * The module wraps the `espressif/esp32-camera` driver and exposes the
 * operations the Camera panel needs:
 *
 *   - initialise the sensor (once, from app_main)
 *   - capture one frame into an internal RGB565 buffer
 *   - change the frame size (QQVGA .. QVGA)
 *   - toggle an RGB565 byte-swap (some modules output big-endian pixels)
 *   - reinitialise after a wiring change
 *   - a log ring buffer, like the MQTT / Bluetooth apps
 *
 * Because the sensor may be missing or miswired, every capture falls back to
 * a generated colour-bar test pattern when the real capture fails, so the
 * UI remains usable while the hardware is being debugged.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAM_LOG_LINE_MAX   128
#define CAM_LOG_LINES      24

typedef struct {
    bool inited;                /* esp_camera_init succeeded          */
    int  last_error;            /* esp_err_t from the last init/capture */
    int  width;                 /* current frame width, pixels        */
    int  height;                /* current frame height, pixels       */
    int  frame_size;            /* framesize_t value                  */
    int  frame_count;           /* successful captures since boot     */
    bool byte_swap;             /* swap RGB565 bytes before display   */
    bool test_pattern;          /* true when a generated pattern is   */
                                /* shown instead of a real frame     */
    unsigned log_lines;
} cam_status_t;

/* Initialise the sensor. Safe to call once from app_main(); never crashes
 * even when the camera is disconnected — the error is recorded in status. */
void app_camera_init(void);

/* Snapshot of the driver state. */
void app_camera_get_status(cam_status_t *out);

/* Capture one frame. On success the internal RGB565 buffer is refreshed;
 * on failure a test pattern is generated and `test_pattern` is set true. */
esp_err_t app_camera_capture(void);

/* Read the most recently captured frame. `out` points to RGB565 pixels in
 * row-major order; the buffer is owned by this module and stays valid until
 * the next capture. */
void app_camera_get_last_frame(const uint16_t **out, int *w, int *h);

/* Free the internal display buffer (app window closed). The next capture
 * allocates it again. The sensor driver stays initialised. */
void app_camera_release(void);

/* Settings */
int          app_camera_frame_size_count(void);
const char  *app_camera_frame_size_name(int idx);
int          app_camera_frame_size_value(int idx);
esp_err_t    app_camera_set_frame_size(int framesize);
esp_err_t    app_camera_set_byte_swap(bool on);
esp_err_t    app_camera_set_test_pattern(bool on);   /* force on/off */
esp_err_t    app_camera_reinit(void);

void app_camera_get_log(char *out, size_t size);
void app_camera_log(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* APP_CAMERA_H */
