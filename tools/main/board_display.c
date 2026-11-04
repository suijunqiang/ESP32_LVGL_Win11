/*
 * Display + touch bring-up for the ESP32-S3 board.
 *
 * This is the driver combination that was verified on the real hardware:
 * ILI9341 over SPI2 at 60 MHz, landscape 320x240 with mirror(false, true),
 * and an XPT2046 resistive touch panel sharing the same SPI bus.
 */

#include "board_display.h"

#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "screenshot.h"

#define LCD_HOST            SPI2_HOST
#define PIN_MOSI            47
#define PIN_SCLK            21
#define PIN_MISO            48    /* XPT2046 SDO, shared with the display bus */
#define PIN_CS              42
#define PIN_DC              41
#define PIN_RST             (-1)  /* no reset line: panel resets with the chip */

#define PIN_TOUCH_CS        46
#define PIN_TOUCH_IRQ       2

#define LCD_H_RES           320
#define LCD_V_RES           240
#define LCD_PIXEL_CLOCK_HZ  (60 * 1000 * 1000)
/* RGB565 (2 bytes/pixel), 40 lines per buffer: 320 * 40 * 2 = 25 KiB each. */
#define LVGL_BUF_LINES      40

/* Raw 12-bit XPT2046 range, least-squares fit of a 5-point tap calibration
 * (centre + four corners). Residuals are within about +/-13 px. */
#define TOUCH_X_MIN         164
#define TOUCH_X_MAX         1796
#define TOUCH_Y_MIN         206
#define TOUCH_Y_MAX         1794

static const char *TAG = "board";

/* Bring-up aid, off by default: paint a known test card before LVGL starts so
 * the panel's black level, primaries and grey ramp can be judged. */
#define PANEL_TEST_CARD 0

/*
 * Power / VCOM / gamma sequence of this module.
 *
 * The generic sequence shipped with the managed esp_lcd_ili9341 component
 * uses a different gamma and VCOM set.  On this panel that renders black and
 * the primaries correctly but lifts every mid tone, so the whole UI looks
 * pale and washed out.  These values come from the known-good vendor driver
 * of the reference example (lvgl_esp32_drivers/lvgl_tft/ili9341.c).
 *
 * The driver still sends SLPOUT, MADCTL and COLMOD itself, so those are not
 * repeated here.
 */
static const ili9341_lcd_init_cmd_t s_vendor_init_cmds[] = {
    {0xCF, (uint8_t[]){0x00, 0x83, 0x30}, 3, 0},          /* power control B */
    {0xED, (uint8_t[]){0x64, 0x03, 0x12, 0x81}, 4, 0},    /* power on seq    */
    {0xE8, (uint8_t[]){0x85, 0x01, 0x79}, 3, 0},          /* driver timing A */
    {0xCB, (uint8_t[]){0x39, 0x2C, 0x00, 0x34, 0x02}, 5, 0},
    {0xF7, (uint8_t[]){0x20}, 1, 0},                      /* pump ratio      */
    {0xEA, (uint8_t[]){0x00, 0x00}, 2, 0},                /* driver timing B */
    {0xC0, (uint8_t[]){0x26}, 1, 0},                      /* GVDD            */
    {0xC1, (uint8_t[]){0x11}, 1, 0},                      /* DDVDH/VGH/VGL   */
    {0xC5, (uint8_t[]){0x35, 0x3E}, 2, 0},                /* VCOM            */
    {0xC7, (uint8_t[]){0xBE}, 1, 0},                      /* VCOM offset     */
    {0xB1, (uint8_t[]){0x00, 0x1B}, 2, 0},                /* frame rate      */
    {0xF2, (uint8_t[]){0x08}, 1, 0},
    {0x26, (uint8_t[]){0x01}, 1, 0},                      /* gamma curve 1   */
    {0xE0, (uint8_t[]){0x1F, 0x1A, 0x18, 0x0A, 0x0F, 0x06, 0x45, 0x87,
                       0x32, 0x0A, 0x07, 0x02, 0x07, 0x05, 0x00}, 15, 0},
    {0xE1, (uint8_t[]){0x00, 0x25, 0x27, 0x05, 0x10, 0x09, 0x3A, 0x78,
                       0x4D, 0x05, 0x18, 0x0D, 0x38, 0x3A, 0x1F}, 15, 0},
    {0xB7, (uint8_t[]){0x07}, 1, 0},                      /* entry mode      */
    {0xB6, (uint8_t[]){0x0A, 0x82, 0x27, 0x00}, 4, 0},    /* display function */
};

static esp_lcd_panel_handle_t s_panel;
static lv_display_t *s_display;

#if PANEL_TEST_CARD
static void panel_test_card(void)
{
    const size_t band_rows = 20;
    uint16_t *buf = heap_caps_malloc(LCD_H_RES * band_rows * sizeof(uint16_t),
                                     MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (buf == NULL) {
        return;
    }

    /* black, red, green, blue, white, 50% grey */
    static const uint16_t bars[6] = {0x0000, 0xF800, 0x07E0,
                                     0x001F, 0xFFFF, 0x8410};

    for (int y0 = 0; y0 < LCD_V_RES; y0 += (int)band_rows) {
        for (size_t r = 0; r < band_rows; r++) {
            int y = y0 + (int)r;

            for (int x = 0; x < LCD_H_RES; x++) {
                uint16_t c;

                if (y < 180) {
                    int bar = x / 54;
                    if (bar > 5) {
                        bar = 5;
                    }
                    c = bars[bar];
                } else {
                    int step = x / 40;
                    if (step > 7) {
                        step = 7;
                    }
                    uint16_t v = (uint16_t)(step * 255 / 7);
                    c = (uint16_t)(((v >> 3) << 11) | ((v >> 2) << 5) | (v >> 3));
                }

                buf[r * LCD_H_RES + x] = __builtin_bswap16(c);
            }
        }

        esp_lcd_panel_draw_bitmap(s_panel, 0, y0, LCD_H_RES,
                                  y0 + (int)band_rows, buf);
    }

    heap_caps_free(buf);
    ESP_LOGI(TAG, "test card: black red green blue white grey + grey ramp");
    vTaskDelay(pdMS_TO_TICKS(10000));
}
#endif

/* ------------------------------------------------------------------ */
/* Display                                                            */
/* ------------------------------------------------------------------ */

static bool on_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                esp_lcd_panel_io_event_data_t *edata,
                                void *user_ctx)
{
    (void)panel_io;
    (void)edata;
    (void)user_ctx;
    lv_display_flush_ready(s_display);
    return false;
}

/*
 * LVGL is configured with RGB565_SWAPPED, so it renders directly in
 * big-endian RGB565 matching the ILI9341 panel's native byte order.
 * No manual conversion or byte swap needed — px_map belongs to LVGL's
 * double buffer pool and must NOT be modified in-place.
 */
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)disp;

    /* Keep a copy of what the panel receives (screenshot mirror). */
    screenshot_mirror_area(area->x1, area->y1, area->x2, area->y2, px_map,
                           LCD_H_RES * sizeof(uint16_t));

    esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, px_map);
    /* lv_display_flush_ready() is called from on_color_trans_done(). */
}

static void tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(1);
}

static esp_err_t panel_init(esp_lcd_panel_io_handle_t *out_io)
{
    /* No backlight GPIO on this board: the panel LED is hard-wired on. */

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LVGL_BUF_LINES * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO),
                        TAG, "spi bus");

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_DC,
        .cs_gpio_num = PIN_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(
                            (esp_lcd_spi_bus_handle_t)LCD_HOST,
                            &io_cfg, out_io),
                        TAG, "panel io");

    /* Identify the real controller. An ST7789/ILI9342 clone driven with the
     * ILI9341 init sequence shows exactly the symptom we have: a strongly
     * lifted black level and a washed out, desaturated picture. */
    {
        uint8_t id[4] = {0};
        esp_err_t err = esp_lcd_panel_io_rx_param(*out_io, 0x04, id, sizeof(id));
        ESP_LOGI(TAG, "RDDID(04h) err=%d -> %02X %02X %02X %02X",
                 (int)err, id[0], id[1], id[2], id[3]);

        uint8_t did[3] = {0};
        err = esp_lcd_panel_io_rx_param(*out_io, 0xDA, did, sizeof(did));
        ESP_LOGI(TAG, "RDID(DAh) err=%d -> %02X %02X %02X",
                 (int)err, did[0], did[1], did[2]);

        uint8_t st[5] = {0};
        err = esp_lcd_panel_io_rx_param(*out_io, 0x09, st, sizeof(st));
        ESP_LOGI(TAG, "RDDST(09h) err=%d -> %02X %02X %02X %02X %02X",
                 (int)err, st[0], st[1], st[2], st[3], st[4]);
    }

    /* Identify the real controller. An ST7789/ILI9342 clone driven with the
     * ILI9341 init sequence shows exactly the symptom we have: a strongly
     * lifted black level and a washed out, desaturated picture. */
    {
        uint8_t id[4] = {0};
        esp_err_t err = esp_lcd_panel_io_rx_param(*out_io, 0x04, id, sizeof(id));
        ESP_LOGI(TAG, "RDDID(04h) err=%d -> %02X %02X %02X %02X",
                 (int)err, id[0], id[1], id[2], id[3]);

        uint8_t did[3] = {0};
        err = esp_lcd_panel_io_rx_param(*out_io, 0xDA, did, sizeof(did));
        ESP_LOGI(TAG, "RDID(DAh) err=%d -> %02X %02X %02X",
                 (int)err, did[0], did[1], did[2]);

        uint8_t st[5] = {0};
        err = esp_lcd_panel_io_rx_param(*out_io, 0x09, st, sizeof(st));
        ESP_LOGI(TAG, "RDDST(09h) err=%d -> %02X %02X %02X %02X %02X",
                 (int)err, st[0], st[1], st[2], st[3], st[4]);
    }

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,   /* RGB565: the module does not do RGB666 */
        .vendor_config = &(ili9341_vendor_config_t){
            .init_cmds = s_vendor_init_cmds,
            .init_cmds_size = sizeof(s_vendor_init_cmds) / sizeof(s_vendor_init_cmds[0]),
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(*out_io, &panel_cfg, &s_panel),
                        TAG, "ili9341");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    /* Wake the controller before running the init sequence. */
    esp_lcd_panel_io_tx_param(*out_io, 0x11, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(150));
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, false), TAG, "invert");
    /* The panel is a native 320x240 landscape module: no axis swap needed. */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, false), TAG, "swap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, false, false), TAG, "mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "disp on");

    /* The panel has no reset line, so its GRAM comes up with random noise.
     * Paint it black once, otherwise the "snow" stays visible until LVGL
     * has produced its first full frame. */
    static uint16_t black_row[LCD_H_RES];
    for (int y = 0; y < LCD_V_RES; y++) {
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_H_RES, y + 1, black_row);
    }

#if PANEL_TEST_CARD
    panel_test_card();
#endif

    esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = on_color_trans_done,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(*out_io, &cbs,
                                                                 NULL),
                        TAG, "callbacks");

    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Touch                                                              */
/* ------------------------------------------------------------------ */

static spi_device_handle_t s_touch_spi;

/* One 12-bit XPT2046 conversion. 0x90 reads the panel's X channel and 0xD0
 * its Y channel (the datasheet labels them the other way round). */
static uint16_t touch_raw(uint8_t cmd)
{
    uint8_t tx[3] = {cmd, 0x00, 0x00};
    uint8_t rx[3] = {0, 0, 0};
    spi_transaction_t t = {
        .length = 24,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    spi_device_polling_transmit(s_touch_spi, &t);
    return (uint16_t)((((uint16_t)rx[1] << 8) | rx[2]) >> 4);
}

static int32_t touch_scale(uint16_t raw, int32_t min, int32_t max, int32_t res)
{
    int32_t v = ((int32_t)raw - min) * res / (max - min);
    if (v < 0) {
        v = 0;
    }
    if (v > res - 1) {
        v = res - 1;
    }
    return v;
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    static lv_point_t last = {0, 0};
    (void)indev;

    if (gpio_get_level(PIN_TOUCH_IRQ) == 0) {
        uint16_t rx = touch_raw(0x90);
        uint16_t ry = touch_raw(0xD0);
        /* Display mirror(false,false): touch Y runs opposite to the screen. */
        int32_t x = touch_scale(rx, TOUCH_X_MIN, TOUCH_X_MAX, LCD_H_RES);
        int32_t y = LCD_V_RES - 1 -
                    touch_scale(ry, TOUCH_Y_MIN, TOUCH_Y_MAX, LCD_V_RES);
        last.x = (lv_coord_t)x;
        last.y = (lv_coord_t)y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }

    data->point = last;
}

static esp_err_t touch_init(void)
{
    gpio_config_t irq_cfg = {
        .pin_bit_mask = 1ULL << PIN_TOUCH_IRQ,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&irq_cfg), TAG, "touch irq gpio");

    spi_device_interface_config_t dev = {
        .clock_speed_hz = 2 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = PIN_TOUCH_CS,
        .queue_size = 1,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(LCD_HOST, &dev, &s_touch_spi),
                        TAG, "touch spi device");

    lv_indev_t *indev = lv_indev_create();
    if (indev == NULL) {
        return ESP_ERR_NO_MEM;
    }
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);

    ESP_LOGI(TAG, "touch: XPT2046 on cs=%d irq=%d", PIN_TOUCH_CS, PIN_TOUCH_IRQ);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Public entry point                                                 */
/* ------------------------------------------------------------------ */

esp_err_t board_display_init(lv_display_t **out_display)
{
    if (out_display == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "init panel");
    esp_lcd_panel_io_handle_t io = NULL;
    ESP_RETURN_ON_ERROR(panel_init(&io), TAG, "panel");

    lv_init();

    s_display = lv_display_create(LCD_H_RES, LCD_V_RES);
    if (s_display == NULL) {
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_flush_cb(s_display, flush_cb);

    size_t buf_bytes = LCD_H_RES * LVGL_BUF_LINES * sizeof(uint16_t);
    void *buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (buf1 == NULL || buf2 == NULL) {
        ESP_LOGE(TAG, "no memory for the LVGL draw buffers");
        heap_caps_free(buf1);
        heap_caps_free(buf2);
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_buffers(s_display, buf1, buf2, buf_bytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    const esp_timer_create_args_t timer_args = {
        .callback = tick_cb,
        .name = "lv_tick",
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &tick_timer), TAG, "tick");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(tick_timer, 1000), TAG, "tick start");

    /* Touch shares the LCD SPI bus, so it must be added after panel_init(). */
    ESP_RETURN_ON_ERROR(touch_init(), TAG, "touch");

    *out_display = s_display;
    ESP_LOGI(TAG, "LVGL %d.%d.%d up, panel %dx%d",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
             LCD_H_RES, LCD_V_RES);
    return ESP_OK;
}
