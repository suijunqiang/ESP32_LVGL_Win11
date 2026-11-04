/*
 * Camera application built on the espressif/esp32-camera component.
 *
 * The sensor wiring (ESP32-S3, 8-bit DVP) is defined at the top of this
 * file and intentionally avoids the GPIOs used by the ILI9341 panel and the
 * XPT2046 touch controller.  The default is the OV2640 wiring used by the
 * ESP32-S3-EYE module.
 *
 * All operations are safe when the sensor is absent: esp_camera_init()'s
 * return code is stored in cam_status_t.last_error, and every capture that
 * cannot get a real frame buffer falls back to a colour-bar test pattern so
 * the panel can be exercised without working hardware.
 */

#include "app_camera.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define CAM_TAG "cam"

/* ------------------------------------------------------------------ */
/* Sensor wiring                                                      */
/* ------------------------------------------------------------------ */

#define CAM_PIN_PWDN    (-1)
#define CAM_PIN_RESET   (-1)
#define CAM_PIN_XCLK    15
#define CAM_PIN_SIOD    5
#define CAM_PIN_SIOC    6
#define CAM_PIN_Y9      12
#define CAM_PIN_Y8      13
#define CAM_PIN_Y7      7
#define CAM_PIN_Y6      4
#define CAM_PIN_Y5      10
#define CAM_PIN_Y4      8
#define CAM_PIN_Y3      9
#define CAM_PIN_Y2      11
#define CAM_PIN_VSYNC   3
#define CAM_PIN_HREF    14
#define CAM_PIN_PCLK    16

/* ------------------------------------------------------------------ */
/* State                                                              */
/* ------------------------------------------------------------------ */

static SemaphoreHandle_t s_lock;

static volatile bool s_inited;
static volatile int  s_last_error;
static volatile int  s_frame_size = FRAMESIZE_QQVGA;   /* 160 x 120     */
static volatile bool s_byte_swap;
static volatile bool s_test_pattern;       /* true while we show a pattern  */
static volatile int  s_frame_count;

/* Internal display buffer (RGB565), reallocated when the frame size changes. */
static uint16_t *s_frame_buf;
static int s_frame_w;
static int s_frame_h;

static char s_log_lines[CAM_LOG_LINES][CAM_LOG_LINE_MAX];
static unsigned s_log_head;
static unsigned s_log_count;

/* The frame sizes the panel offers. framesize_t values from esp_camera.h. */
static const struct {
    framesize_t size;
    const char *name;
    int w, h;
} s_frame_sizes[] = {
    { FRAMESIZE_QQVGA,  "QQVGA 160x120",   160, 120 },
    { FRAMESIZE_128X128,"128x128",         128, 128 },
    { FRAMESIZE_QCIF,   "QCIF 176x144",    176, 144 },
    { FRAMESIZE_HQVGA,  "HQVGA 240x176",   240, 176 },
    { FRAMESIZE_240X240,"240x240",         240, 240 },
    { FRAMESIZE_QVGA,   "QVGA 320x240",    320, 240 },
};
#define FRAME_SIZE_COUNT (sizeof(s_frame_sizes) / sizeof(s_frame_sizes[0]))

static int size_index_by_value(framesize_t v)
{
    for (size_t i = 0; i < FRAME_SIZE_COUNT; i++) {
        if (s_frame_sizes[i].size == v) {
            return (int)i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static void cam_copy_str(char *dst, size_t cap, const char *src)
{
    if (dst == NULL || cap == 0) {
        return;
    }
    if (src == NULL) {
        src = "";
    }
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void cam_lock(void)
{
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void cam_unlock(void)
{
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
}

/* ------------------------------------------------------------------ */
/* Log ring buffer                                                    */
/* ------------------------------------------------------------------ */

static void cam_log_v(const char *fmt, va_list ap)
{
    char msg[CAM_LOG_LINE_MAX];
    int n = vsnprintf(msg, sizeof(msg), fmt, ap);
    if (n < 0) {
        n = 0;
    }
    if ((unsigned)n >= sizeof(msg)) {
        msg[sizeof(msg) - 1] = '\0';
    }

    char line[CAM_LOG_LINE_MAX];
    int prefix = snprintf(line, sizeof(line), "[%llu] ",
                          (unsigned long long)(esp_timer_get_time()
                                               / 1000000ULL));
    if (prefix < 0) {
        prefix = 0;
    }
    if ((unsigned)prefix >= sizeof(line)) {
        prefix = (int)sizeof(line) - 1;
        line[prefix] = '\0';
    }
    unsigned avail = sizeof(line) - (unsigned)prefix - 1;
    unsigned blen = (unsigned)n < avail ? (unsigned)n : avail;
    memcpy(line + prefix, msg, blen);
    line[prefix + blen] = '\0';

    cam_lock();
    unsigned i = s_log_head;
    s_log_head = (s_log_head + 1) % CAM_LOG_LINES;
    if (s_log_count < CAM_LOG_LINES) {
        s_log_count++;
    }
    cam_copy_str(s_log_lines[i], sizeof(s_log_lines[i]), line);
    cam_unlock();
}

void app_camera_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    cam_log_v(fmt, ap);
    va_end(ap);
}

void app_camera_get_log(char *out, size_t size)
{
    if (out == NULL || size == 0) {
        return;
    }
    out[0] = '\0';

    cam_lock();
    if (s_log_count == 0) {
        cam_unlock();
        return;
    }

    unsigned start = (s_log_head + CAM_LOG_LINES - s_log_count) % CAM_LOG_LINES;
    size_t written = 0;
    for (unsigned i = 0; i < s_log_count && written + 1 < size; i++) {
        const char *l = s_log_lines[(start + i) % CAM_LOG_LINES];
        int n = snprintf(out + written, size - written, "%s\n", l);
        if (n < 0) {
            break;
        }
        if ((size_t)n >= size - written) {
            written = size - 1;
            break;
        }
        written += (size_t)n;
    }
    out[written] = '\0';
    cam_unlock();
}

/* ------------------------------------------------------------------ */
/* Frame buffer management                                            */
/* ------------------------------------------------------------------ */

static bool cam_alloc_frame(int w, int h)
{
    if (w <= 0 || h <= 0) {
        return false;
    }

    cam_lock();
    if (s_frame_buf != NULL && s_frame_w == w && s_frame_h == h) {
        cam_unlock();
        return true;
    }

    if (s_frame_buf != NULL) {
        heap_caps_free(s_frame_buf);
        s_frame_buf = NULL;
    }

    size_t bytes = (size_t)w * (size_t)h * sizeof(uint16_t);
    s_frame_buf = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (s_frame_buf == NULL) {
        s_frame_buf = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL);
    }
    if (s_frame_buf == NULL) {
        cam_unlock();
        return false;
    }

    s_frame_w = w;
    s_frame_h = h;
    cam_unlock();
    return true;
}

void app_camera_release(void)
{
    cam_lock();
    heap_caps_free(s_frame_buf);
    s_frame_buf = NULL;
    s_frame_w = 0;
    s_frame_h = 0;
    cam_unlock();
}

static void cam_swap_bytes(uint16_t *buf, int w, int h)
{
    if (buf == NULL) {
        return;
    }
    size_t count = (size_t)w * (size_t)h;
    for (size_t i = 0; i < count; i++) {
        uint16_t p = buf[i];
        buf[i] = (uint16_t)((p << 8) | (p >> 8));
    }
}

/* Generated colour-bar + moving gradient used when the real sensor is
 * unavailable. It gives the LVGL image widget something to draw so the
 * whole UI can be verified without a working camera. */
static void cam_render_test_pattern(uint16_t *buf, int w, int h, int frame)
{
    static const uint16_t bars[8] = {
        0x0000, 0xF800, 0x07E0, 0x001F,
        0xFFFF, 0xFFE0, 0xF81F, 0x07FF,
    };
    int band = w / 8;
    if (band < 1) {
        band = 1;
    }

    for (int y = 0; y < h; y++) {
        int shade = (y * 31) / h;   /* 0..31 vertical gradient */
        for (int x = 0; x < w; x++) {
            uint16_t c = bars[(x / band) & 7];
            /* Mix the band colour with a dim grey that follows the frame
             * counter so the image visibly updates on every capture. */
            uint16_t r = (c >> 11) & 0x1F;
            uint16_t g = (c >> 5) & 0x3F;
            uint16_t b = c & 0x1F;
            r = (uint16_t)((r * shade) / 31);
            g = (uint16_t)((g * shade) / 31);
            b = (uint16_t)((b * shade) / 31);
            c = (uint16_t)((r << 11) | (g << 5) | b);

            /* A moving vertical stripe highlights new frames. */
            int stripe = ((frame * 8) + x) % w;
            if (stripe < 12) {
                c = (uint16_t)(c ^ 0xFFFF);
            }
            buf[y * w + x] = c;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Sensor init / capture                                              */
/* ------------------------------------------------------------------ */

static camera_config_t cam_make_config(framesize_t size)
{
    camera_config_t c = {
        .pin_pwdn       = CAM_PIN_PWDN,
        .pin_reset      = CAM_PIN_RESET,
        .pin_xclk       = CAM_PIN_XCLK,
        .pin_sccb_sda   = CAM_PIN_SIOD,
        .pin_sccb_scl   = CAM_PIN_SIOC,
        .pin_d7         = CAM_PIN_Y9,
        .pin_d6         = CAM_PIN_Y8,
        .pin_d5         = CAM_PIN_Y7,
        .pin_d4         = CAM_PIN_Y6,
        .pin_d3         = CAM_PIN_Y5,
        .pin_d2         = CAM_PIN_Y4,
        .pin_d1         = CAM_PIN_Y3,
        .pin_d0         = CAM_PIN_Y2,
        .pin_vsync      = CAM_PIN_VSYNC,
        .pin_href       = CAM_PIN_HREF,
        .pin_pclk       = CAM_PIN_PCLK,
        .xclk_freq_hz   = 20000000,
        .ledc_timer     = LEDC_TIMER_0,
        .ledc_channel   = LEDC_CHANNEL_0,
        .pixel_format   = PIXFORMAT_RGB565,
        .frame_size     = size,
        .jpeg_quality   = 12,
        .fb_count       = 1,
        .fb_location    = CAMERA_FB_IN_PSRAM,
        .grab_mode      = CAMERA_GRAB_LATEST,
    };
    return c;
}

static esp_err_t cam_init_sensor(framesize_t size)
{
    camera_config_t cfg = cam_make_config(size);

    esp_err_t err = esp_camera_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(CAM_TAG, "esp_camera_init failed: %s", esp_err_to_name(err));
        app_camera_log("init failed: %s", esp_err_to_name(err));
        s_last_error = err;
        s_inited = false;
        return err;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL) {
        s->set_brightness(s, 0);
        s->set_contrast(s, 0);
        s->set_saturation(s, 0);
        s->set_vflip(s, 0);
        s->set_hmirror(s, 0);
        app_camera_log("sensor ready: framesize=%d pid=0x%x",
                       s->status.framesize, s->id.PID);
        ESP_LOGI(CAM_TAG, "sensor framesize %d pid 0x%x",
                 s->status.framesize, s->id.PID);
    } else {
        app_camera_log("init ok but no sensor descriptor");
    }

    s_last_error = ESP_OK;
    s_inited = true;
    return ESP_OK;
}

void app_camera_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }

    cam_lock();
    s_log_head = 0;
    s_log_count = 0;
    s_frame_count = 0;
    s_test_pattern = false;
    cam_unlock();

    framesize_t size = (framesize_t)s_frame_size;
    esp_err_t err = cam_init_sensor(size);

    if (err != ESP_OK) {
        /* Hardware absent / miswired: fall back to the test pattern so the
         * UI is still usable. The panel can attempt app_camera_reinit()
         * once the wiring is fixed. */
        int idx = size_index_by_value(size);
        int w = (idx >= 0) ? s_frame_sizes[idx].w : 160;
        int h = (idx >= 0) ? s_frame_sizes[idx].h : 120;
        if (cam_alloc_frame(w, h)) {
            s_test_pattern = true;
            cam_render_test_pattern(s_frame_buf, w, h, 0);
            app_camera_log("using test pattern until camera is fixed");
        } else {
            app_camera_log("no frame buffer");
        }
    }
}

void app_camera_get_status(cam_status_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->inited = s_inited;
    out->last_error = s_last_error;
    out->frame_size = s_frame_size;
    out->frame_count = s_frame_count;
    out->byte_swap = s_byte_swap;
    out->test_pattern = s_test_pattern;

    cam_lock();
    out->width = s_frame_w;
    out->height = s_frame_h;
    out->log_lines = s_log_count;
    cam_unlock();
}

esp_err_t app_camera_capture(void)
{
    if (!s_inited || s_test_pattern) {
        /* Generate a fresh pattern so the UI visibly updates. */
        cam_lock();
        int w = s_frame_w;
        int h = s_frame_h;
        cam_unlock();

        if (w <= 0 || h <= 0 || !cam_alloc_frame(w, h)) {
            if (!cam_alloc_frame(160, 120)) {
                s_last_error = ESP_ERR_NO_MEM;
                return ESP_ERR_NO_MEM;
            }
            w = 160;
            h = 120;
        }
        cam_render_test_pattern(s_frame_buf, w, h, ++s_frame_count);
        s_test_pattern = true;
        return ESP_OK;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (fb == NULL) {
        s_last_error = ESP_FAIL;
        app_camera_log("capture: no frame buffer");
        /* Fall back to the pattern for this frame only; the sensor stays
         * initialised for the next attempt. */
        cam_lock();
        int w = s_frame_w;
        int h = s_frame_h;
        cam_unlock();
        if (w > 0 && h > 0 && cam_alloc_frame(w, h)) {
            cam_render_test_pattern(s_frame_buf, w, h, ++s_frame_count);
        }
        return ESP_FAIL;
    }

    int w = fb->width;
    int h = fb->height;
    bool swap = s_byte_swap;

    if (fb->format == PIXFORMAT_RGB565 && cam_alloc_frame(w, h)) {
        size_t bytes = (size_t)w * (size_t)h * sizeof(uint16_t);
        if (fb->len >= bytes) {
            memcpy(s_frame_buf, fb->buf, bytes);
            if (swap) {
                cam_swap_bytes(s_frame_buf, w, h);
            }
            s_frame_count++;
            s_test_pattern = false;
            s_last_error = ESP_OK;
        } else {
            app_camera_log("capture: short frame %u bytes", (unsigned)fb->len);
        }
    } else {
        app_camera_log("capture: format %d unsupported", (int)fb->format);
        s_last_error = ESP_ERR_NOT_SUPPORTED;
    }

    esp_camera_fb_return(fb);
    return s_last_error;
}

void app_camera_get_last_frame(const uint16_t **out, int *w, int *h)
{
    if (out != NULL) {
        *out = s_frame_buf;
    }
    if (w != NULL) {
        *w = s_frame_w;
    }
    if (h != NULL) {
        *h = s_frame_h;
    }
}

/* ------------------------------------------------------------------ */
/* Settings                                                           */
/* ------------------------------------------------------------------ */

int app_camera_frame_size_count(void)
{
    return (int)FRAME_SIZE_COUNT;
}

const char *app_camera_frame_size_name(int idx)
{
    if (idx < 0 || idx >= (int)FRAME_SIZE_COUNT) {
        return "";
    }
    return s_frame_sizes[idx].name;
}

int app_camera_frame_size_value(int idx)
{
    if (idx < 0 || idx >= (int)FRAME_SIZE_COUNT) {
        return -1;
    }
    return (int)s_frame_sizes[idx].size;
}

esp_err_t app_camera_set_frame_size(int framesize)
{
    int idx = size_index_by_value((framesize_t)framesize);
    if (idx < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    s_frame_size = framesize;

    if (s_inited) {
        sensor_t *s = esp_camera_sensor_get();
        if (s != NULL) {
            int rc = s->set_framesize(s, (framesize_t)framesize);
            if (rc != 0) {
                app_camera_log("set_framesize failed rc=%d", rc);
                return ESP_FAIL;
            }
        }
        /* Pre-allocate the display buffer at the new size. */
        cam_alloc_frame(s_frame_sizes[idx].w, s_frame_sizes[idx].h);
        /* Render a placeholder so the image widget never points at a
         * stale-sized buffer. */
        if (s_frame_buf != NULL) {
            cam_render_test_pattern(s_frame_buf, s_frame_w, s_frame_h,
                                    ++s_frame_count);
        }
    } else {
        /* Not initialised: just remember it for the next init. */
        cam_alloc_frame(s_frame_sizes[idx].w, s_frame_sizes[idx].h);
        if (s_frame_buf != NULL) {
            cam_render_test_pattern(s_frame_buf, s_frame_w, s_frame_h,
                                    ++s_frame_count);
        }
        s_test_pattern = true;
    }

    app_camera_log("frame size -> %s", s_frame_sizes[idx].name);
    return ESP_OK;
}

esp_err_t app_camera_set_byte_swap(bool on)
{
    s_byte_swap = on;
    app_camera_log("byte swap %s", on ? "on" : "off");
    return ESP_OK;
}

esp_err_t app_camera_set_test_pattern(bool on)
{
    s_test_pattern = on;
    app_camera_log("test pattern %s", on ? "on" : "off");
    if (on) {
        /* Render immediately so the widget has valid pixels. */
        cam_lock();
        int w = s_frame_w;
        int h = s_frame_h;
        cam_unlock();
        if (w > 0 && h > 0 && cam_alloc_frame(w, h)) {
            cam_render_test_pattern(s_frame_buf, w, h, ++s_frame_count);
        }
    }
    return ESP_OK;
}

esp_err_t app_camera_reinit(void)
{
    if (s_inited) {
        esp_camera_deinit();
        s_inited = false;
    }
    app_camera_log("reinitialising...");
    return cam_init_sensor((framesize_t)s_frame_size);
}
