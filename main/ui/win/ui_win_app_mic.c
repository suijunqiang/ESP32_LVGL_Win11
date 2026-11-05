#include "ui_win_app_mic.h"
#include "ui_win_theme.h"
#include "lv_i18n.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"

/*
 * Tab 5: the microphone monitor (see mic.txt / mic.jpg).
 *
 * The reference builds the effect from dozens of LVGL objects with transforms.
 * Here everything is rasterised into two RGB565 canvases instead: a circular
 * scope (rings, radial spectrum bars, sonar arc, ripples, the microphone icon)
 * and a waveform strip. That keeps the object tree small and gives full
 * control over the anti-aliased neon look at 320 x 240.
 *
 * The level is simulated with stacked sines, exactly like the reference demo
 * mode, because this board has no audio input wired to the UI task.
 */

#define MIC_PI          3.14159265f
#define MIC_BARS_IN     44
#define MIC_BARS_OUT    52
#define MT_SAMPLES      200
#define MIC_FPS_MS      33

#define MIC_RGB(r, g, b) \
    ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

#define MIC_C_BG        MIC_RGB(0x08, 0x0C, 0x16)
#define MIC_C_WAVE_BG   MIC_RGB(0x0A, 0x10, 0x1E)
#define MIC_C_CYAN      MIC_RGB(0x2A, 0xE6, 0xFF)
#define MIC_C_CYAN_DIM  MIC_RGB(0x18, 0x6A, 0x86)
#define MIC_C_PURPLE    MIC_RGB(0x8A, 0x5C, 0xFF)
#define MIC_C_BLUE      MIC_RGB(0x1E, 0x7B, 0xFF)
#define MIC_C_WHITE     MIC_RGB(0xDD, 0xEE, 0xFF)

typedef struct {
    lv_obj_t  *page;
    lv_obj_t  *title;
    lv_obj_t  *hud;
    lv_obj_t  *cap;
    lv_obj_t  *lvl_name;
    lv_obj_t  *lvl_val;

    lv_obj_t  *scope;
    uint16_t  *scope_buf;
    int32_t    scope_w;
    int32_t    scope_h;

    lv_obj_t  *wave;
    uint16_t  *wave_buf;
    int32_t    wave_w;
    int32_t    wave_h;

    float      bars_in[MIC_BARS_IN];
    float      bars_out[MIC_BARS_OUT];
    float      samples[MT_SAMPLES];

    float      level;      /* smoothed 0..100 */
    float      t;          /* seconds */
    int        shown_pct;

    lv_timer_t *tmr;
    int32_t    laid_w;
    int32_t    laid_h;
} mic_ctx_t;

static mic_ctx_t s_mic;

/* ------------------------------------------------------------------ */
/* Pixel helpers                                                       */
/* ------------------------------------------------------------------ */

static uint16_t mic_max565(uint16_t a, uint16_t b)
{
    uint32_t ar = a >> 11, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    uint32_t br = b >> 11, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    if (ar < br) {
        ar = br;
    }
    if (ag < bg) {
        ag = bg;
    }
    if (ab < bb) {
        ab = bb;
    }
    return (uint16_t)((ar << 11) | (ag << 5) | ab);
}

static uint16_t mic_scale(uint16_t c, float a)
{
    if (a <= 0.0f) {
        return 0;
    }
    if (a > 1.0f) {
        a = 1.0f;
    }
    uint32_t r = (c >> 11) & 0x1F;
    uint32_t g = (c >> 5) & 0x3F;
    uint32_t b = c & 0x1F;
    r = (uint32_t)((float)r * a);
    g = (uint32_t)((float)g * a);
    b = (uint32_t)((float)b * a);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void mic_px(uint16_t *buf, int32_t w, int32_t h,
                   int32_t x, int32_t y, uint16_t c, float a)
{
    if (x < 0 || y < 0 || x >= w || y >= h) {
        return;
    }
    uint16_t *p = &buf[(size_t)y * (size_t)w + (size_t)x];
    *p = mic_max565(*p, mic_scale(c, a));
}

/* Anti-aliased dot, same coverage band as the dot array of tab 4. */
static void mic_dot(uint16_t *buf, int32_t w, int32_t h,
                    float x, float y, float r, uint16_t c, float a)
{
    float e = r + 0.75f;
    float e2 = e * e;
    int32_t x0 = (int32_t)floorf(x - e);
    int32_t x1 = (int32_t)ceilf(x + e);
    int32_t y0 = (int32_t)floorf(y - e);
    int32_t y1 = (int32_t)ceilf(y + e);
    for (int32_t py = y0; py <= y1; py++) {
        float dy = (float)py + 0.5f - y;
        float dy2 = dy * dy;
        for (int32_t px = x0; px <= x1; px++) {
            float dx = (float)px + 0.5f - x;
            float d2 = dx * dx + dy2;
            if (d2 >= e2) {
                continue;
            }
            float cov = (e - sqrtf(d2)) * (1.0f / 1.5f);
            if (cov > 1.0f) {
                cov = 1.0f;
            }
            mic_px(buf, w, h, px, py, c, a * cov);
        }
    }
}

/* Line drawn as overlapping anti-aliased dots. */
static void mic_line(uint16_t *buf, int32_t w, int32_t h,
                     float x0, float y0, float x1, float y1,
                     float r, uint16_t c, float a)
{
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    int32_t steps = (int32_t)(len * 1.4f) + 1;
    for (int32_t i = 0; i <= steps; i++) {
        float u = (float)i / (float)steps;
        mic_dot(buf, w, h, x0 + dx * u, y0 + dy * u, r, c, a);
    }
}

static void mic_circle(uint16_t *buf, int32_t w, int32_t h,
                       float cx, float cy, float rad,
                       uint16_t c, float a, float thick)
{
    int32_t steps = (int32_t)(rad * 6.2832f * 1.2f) + 24;
    for (int32_t i = 0; i < steps; i++) {
        float ang = (float)i / (float)steps * 6.2832f;
        mic_dot(buf, w, h, cx + cosf(ang) * rad, cy + sinf(ang) * rad,
                thick, c, a);
    }
}

/* ------------------------------------------------------------------ */
/* Scope                                                               */
/* ------------------------------------------------------------------ */

static void mic_draw_mic_icon(uint16_t *buf, int32_t w, int32_t h,
                              float cx, float cy, float rad)
{
    float half_w = rad * 0.11f;
    float half_h = rad * 0.21f;
    float corner = half_w;
    float top = cy - rad * 0.06f - half_h;
    float bottom = cy - rad * 0.06f + half_h;
    float straight = half_h - corner;

    /* soft neon halo, the canvas equivalent of the reference shadow */
    mic_dot(buf, w, h, cx, cy - rad * 0.06f, rad * 0.28f, MIC_C_CYAN, 0.09f);

    for (int32_t y = (int32_t)floorf(top); y <= (int32_t)ceilf(bottom); y++) {
        float dy = (float)y + 0.5f - (cy - rad * 0.06f);
        float ady = fabsf(dy);
        float hw;
        if (ady <= straight) {
            hw = half_w;
        } else {
            float k = ady - straight;
            if (k >= corner) {
                continue;
            }
            hw = sqrtf(corner * corner - k * k);
        }
        /* vertical cyan -> blue gradient, like the reference capsule */
        float u = ((float)y - top) / (bottom - top);
        uint16_t col = mic_max565(
            mic_scale(MIC_C_CYAN, 1.0f - u),
            mic_scale(MIC_C_BLUE, u));
        for (int32_t x = (int32_t)(cx - hw); x <= (int32_t)(cx + hw); x++) {
            mic_px(buf, w, h, x, y, col, 1.0f);
        }
    }

    /* U shaped bracket plus the base bar */
    float y_mid = cy - rad * 0.06f;
    float y_end = cy + rad * 0.26f;
    float x_off = rad * 0.16f;
    mic_line(buf, w, h, cx - x_off, y_mid, cx - x_off, y_end, 0.9f,
             MIC_C_WHITE, 0.95f);
    mic_line(buf, w, h, cx + x_off, y_mid, cx + x_off, y_end, 0.9f,
             MIC_C_WHITE, 0.95f);
    mic_line(buf, w, h, cx - x_off, y_end, cx + x_off, y_end, 0.9f,
             MIC_C_WHITE, 0.95f);
    mic_line(buf, w, h, cx - rad * 0.20f, y_end + 2.0f,
             cx + rad * 0.20f, y_end + 2.0f, 1.1f, MIC_C_WHITE, 0.95f);
}

static void mic_scope_render(void)
{
    if (s_mic.scope_buf == NULL) {
        return;
    }
    int32_t w = s_mic.scope_w;
    int32_t h = s_mic.scope_h;
    uint16_t *buf = s_mic.scope_buf;

    for (int32_t i = 0; i < w * h; i++) {
        buf[i] = MIC_C_BG;
    }

    float cx = (float)w * 0.5f;
    float cy = (float)h * 0.5f;
    float rad = (float)((w < h) ? w : h) * 0.5f - 2.0f;

    /* the three guide circles */
    mic_circle(buf, w, h, cx, cy, rad * 0.95f, MIC_C_CYAN_DIM, 0.55f, 0.9f);
    mic_circle(buf, w, h, cx, cy, rad * 0.60f, MIC_C_CYAN_DIM, 0.40f, 0.8f);
    mic_circle(buf, w, h, cx, cy, rad * 0.30f, MIC_C_CYAN_DIM, 0.32f, 0.8f);

    /* inner spectrum: cyan, growing outwards with the level */
    float r_in = rad * 0.44f;
    for (int i = 0; i < MIC_BARS_IN; i++) {
        float ang = (float)i / (float)MIC_BARS_IN * 6.2832f;
        float len = rad * 0.05f + rad * 0.10f * (s_mic.bars_in[i] / 100.0f);
        float ca = cosf(ang), sa = sinf(ang);
        mic_line(buf, w, h,
                 cx + ca * r_in, cy + sa * r_in,
                 cx + ca * (r_in + len), cy + sa * (r_in + len),
                 0.8f, MIC_C_CYAN, 0.75f + 0.25f * (s_mic.bars_in[i] / 100.0f));
    }

    /* outer spectrum: purple, inverse mapping so both layers breathe */
    float r_out = rad * 0.72f;
    for (int i = 0; i < MIC_BARS_OUT; i++) {
        float ang = (float)i / (float)MIC_BARS_OUT * 6.2832f;
        float len = rad * 0.04f + rad * 0.055f * (s_mic.bars_out[i] / 100.0f);
        float ca = cosf(ang), sa = sinf(ang);
        mic_line(buf, w, h,
                 cx + ca * r_out, cy + sa * r_out,
                 cx + ca * (r_out + len), cy + sa * (r_out + len),
                 0.8f, MIC_C_PURPLE, 0.70f + 0.30f * (s_mic.bars_out[i] / 100.0f));
    }

    /* expanding ripples */
    for (int k = 0; k < 3; k++) {
        float ph = fmodf(s_mic.t * 0.33f + (float)k * 0.3333f, 1.0f);
        float rr = rad * 0.30f + ph * rad * 0.62f;
        mic_circle(buf, w, h, cx, cy, rr, MIC_C_CYAN, 0.50f * (1.0f - ph),
                   0.9f);
    }

    /* sonar arc on the outer circle */
    float a0 = fmodf(s_mic.t * 1.5f, 6.2832f);
    for (float a = 0.0f; a < 1.05f; a += 0.06f) {
        mic_dot(buf, w, h, cx + cosf(a0 + a) * rad * 0.95f,
                cy + sinf(a0 + a) * rad * 0.95f, 1.2f, MIC_C_CYAN, 0.95f);
    }

    mic_draw_mic_icon(buf, w, h, cx, cy, rad);

    lv_obj_invalidate(s_mic.scope);
}

/* ------------------------------------------------------------------ */
/* Waveform strip                                                      */
/* ------------------------------------------------------------------ */

static void mic_wave_render(void)
{
    if (s_mic.wave_buf == NULL) {
        return;
    }
    int32_t w = s_mic.wave_w;
    int32_t h = s_mic.wave_h;
    uint16_t *buf = s_mic.wave_buf;

    for (int32_t i = 0; i < w * h; i++) {
        buf[i] = MIC_C_WAVE_BG;
    }

    /* thin rounded frame, like the reference waveform panel */
    for (int32_t x = 0; x < w; x++) {
        mic_px(buf, w, h, x, 0, MIC_C_CYAN_DIM, 0.55f);
        mic_px(buf, w, h, x, h - 1, MIC_C_CYAN_DIM, 0.55f);
    }
    for (int32_t y = 0; y < h; y++) {
        mic_px(buf, w, h, 0, y, MIC_C_CYAN_DIM, 0.55f);
        mic_px(buf, w, h, w - 1, y, MIC_C_CYAN_DIM, 0.55f);
    }

    float mid = (float)h * 0.5f;
    float amp = mid - 3.0f;
    float step = (float)(w - 4) / (float)(MT_SAMPLES - 1);

    float px = 2.0f;
    float py = mid - s_mic.samples[0] * amp;
    for (int i = 1; i < MT_SAMPLES; i++) {
        float nx = 2.0f + step * (float)i;
        float ny = mid - s_mic.samples[i] * amp;
        /* glow pass then core pass: two series stacked, as in the reference */
        mic_line(buf, w, h, px, py, nx, ny, 1.7f, MIC_C_BLUE, 0.30f);
        mic_line(buf, w, h, px, py, nx, ny, 0.8f, MIC_C_CYAN, 0.95f);
        px = nx;
        py = ny;
    }

    lv_obj_invalidate(s_mic.wave);
}

/* ------------------------------------------------------------------ */
/* Animation                                                           */
/* ------------------------------------------------------------------ */

static void mic_tick(lv_timer_t *tmr)
{
    (void)tmr;
    s_mic.t += (float)MIC_FPS_MS / 1000.0f;

    float t = s_mic.t;
    float lvl = 40.0f + 26.0f * sinf(t * 1.7f)
              + 16.0f * sinf(t * 5.1f + 1.2f)
              + 9.0f * sinf(t * 11.3f + 2.5f)
              + 5.0f * sinf(t * 23.0f + 0.6f);
    lvl += (float)((rand() % 21) - 10) * 0.8f;
    if (lvl < 0.0f) {
        lvl = 0.0f;
    } else if (lvl > 100.0f) {
        lvl = 100.0f;
    }
    s_mic.level = s_mic.level * 0.7f + lvl * 0.3f;

    for (int i = 0; i < MIC_BARS_IN; i++) {
        float target = s_mic.level * (0.5f + 0.5f * sinf(t * 2.3f + i * 0.9f));
        target += 14.0f * sinf(t * 8.0f + i * 1.35f);
        if (target < 0.0f) {
            target = 0.0f;
        } else if (target > 100.0f) {
            target = 100.0f;
        }
        s_mic.bars_in[i] = s_mic.bars_in[i] * 0.6f + target * 0.4f;
    }
    for (int i = 0; i < MIC_BARS_OUT; i++) {
        float target = s_mic.level * (0.5f + 0.5f * sinf(t * 2.3f + i * 0.9f));
        target += 14.0f * sinf(t * 8.0f + i * 1.35f);
        if (target < 0.0f) {
            target = 0.0f;
        } else if (target > 100.0f) {
            target = 100.0f;
        }
        /* inverse mapping: the outer ring moves against the inner one */
        target = 100.0f - target;
        s_mic.bars_out[i] = s_mic.bars_out[i] * 0.6f + target * 0.4f;
    }

    memmove(s_mic.samples, &s_mic.samples[1],
            sizeof(float) * (MT_SAMPLES - 1));
    float gain = 0.25f + 0.75f * (s_mic.level / 100.0f);
    float s = 0.55f * sinf(t * 9.0f) + 0.28f * sinf(t * 21.0f + 0.7f)
            + 0.14f * sinf(t * 47.0f + 1.9f);
    s += (float)((rand() % 201) - 100) / 100.0f * 0.12f;
    s_mic.samples[MT_SAMPLES - 1] = s * gain;

    mic_scope_render();
    mic_wave_render();

    int pct = (int)(s_mic.level + 0.5f);
    if (pct != s_mic.shown_pct) {
        s_mic.shown_pct = pct;
        lv_label_set_text_fmt(s_mic.lvl_val, "%d%%", pct);
    }
}

/* ------------------------------------------------------------------ */
/* Build / layout                                                      */
/* ------------------------------------------------------------------ */

static void *mic_malloc(size_t bytes)
{
    void *p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (p == NULL) {
        p = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    }
    return p;
}

static void mic_scope_alloc(int32_t w, int32_t h)
{
    if (s_mic.scope_buf != NULL && s_mic.scope_w == w && s_mic.scope_h == h) {
        return;
    }
    if (s_mic.scope_buf != NULL) {
        heap_caps_free(s_mic.scope_buf);
        s_mic.scope_buf = NULL;
    }
    s_mic.scope_buf = mic_malloc((size_t)w * (size_t)h * sizeof(uint16_t));
    if (s_mic.scope_buf == NULL) {
        return;
    }
    s_mic.scope_w = w;
    s_mic.scope_h = h;
    lv_canvas_set_buffer(s_mic.scope, s_mic.scope_buf, w, h,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(s_mic.scope, w, h);
}

static void mic_wave_alloc(int32_t w, int32_t h)
{
    if (s_mic.wave_buf != NULL && s_mic.wave_w == w && s_mic.wave_h == h) {
        return;
    }
    if (s_mic.wave_buf != NULL) {
        heap_caps_free(s_mic.wave_buf);
        s_mic.wave_buf = NULL;
    }
    s_mic.wave_buf = mic_malloc((size_t)w * (size_t)h * sizeof(uint16_t));
    if (s_mic.wave_buf == NULL) {
        return;
    }
    s_mic.wave_w = w;
    s_mic.wave_h = h;
    lv_canvas_set_buffer(s_mic.wave, s_mic.wave_buf, w, h,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(s_mic.wave, w, h);
}

static void mic_size_cb(lv_event_t *event)
{
    (void)event;
    ui_mic_relayout();
}

void ui_mic_relayout(void)
{
    if (s_mic.page == NULL) {
        return;
    }
    int32_t w = lv_obj_get_width(s_mic.page);
    int32_t h = lv_obj_get_height(s_mic.page);
    if (w < 20 || h < 20) {
        return;
    }
    if (w == s_mic.laid_w && h == s_mic.laid_h) {
        return;
    }
    s_mic.laid_w = w;
    s_mic.laid_h = h;

    const int32_t margin = WIN_PX(6);
    const int32_t head_h = 16;
    const int32_t wave_h = 34;
    const int32_t lvl_h = 14;
    int32_t pw = w - 2 * margin;

    /* The scope is square and centred; everything else stacks below it. */
    int32_t scope = h - head_h - wave_h - lvl_h - 9;
    if (scope < 40) {
        scope = h / 2;
    }
    if (scope > pw) {
        scope = pw;
    }
    int32_t scope_x = (w - scope) / 2;

    lv_obj_set_pos(s_mic.title, margin, 1);
    lv_obj_set_size(s_mic.title, pw / 2, 14);
    lv_obj_set_pos(s_mic.hud, margin, 2);
    lv_obj_set_size(s_mic.hud, pw, 12);

    lv_obj_set_pos(s_mic.scope, scope_x, head_h + 2);
    mic_scope_alloc(scope, scope);

    int32_t card_y = head_h + 2 + scope + 3;
    lv_obj_set_pos(s_mic.cap, margin, card_y);
    lv_obj_set_size(s_mic.cap, pw, wave_h);
    lv_obj_set_pos(s_mic.wave, 2, 2);
    mic_wave_alloc(pw - 4, wave_h - 4);

    /* "INPUT LEVEL" on the left, the live percentage on the right. */
    int32_t lvl_y = card_y + wave_h + 1;
    lv_obj_set_pos(s_mic.lvl_name, margin, lvl_y);
    lv_obj_set_size(s_mic.lvl_name, pw / 2, lvl_h);
    lv_obj_set_pos(s_mic.lvl_val, margin + pw / 2, lvl_y);
    lv_obj_set_size(s_mic.lvl_val, pw / 2, lvl_h);

    mic_scope_render();
    mic_wave_render();
}

static lv_obj_t *mic_label(lv_obj_t *parent, const lv_font_t *font,
                           lv_color_t color, lv_text_align_t align)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_align(l, align, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    return l;
}

lv_obj_t *ui_mic_create(lv_obj_t *page)
{
    memset(&s_mic, 0, sizeof(s_mic));
    s_mic.page = page;
    s_mic.level = 30.0f;
    s_mic.shown_pct = -1;

    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_set_style_bg_color(page, lv_color_hex(0x080C16), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);

    s_mic.title = mic_label(page, WIN_FONT_SMALL, WIN_COLOR_TEXT,
                            LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(s_mic.title, _("mic_title"));

    s_mic.hud = mic_label(page, WIN_FONT_TINY, WIN_COLOR_TEXT_MUTED,
                          LV_TEXT_ALIGN_RIGHT);
    lv_label_set_text(s_mic.hud, _("mic_meta"));

    s_mic.scope = lv_canvas_create(page);
    lv_obj_remove_flag(s_mic.scope, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_mic.scope, 0, 0);
    lv_obj_set_style_border_width(s_mic.scope, 0, 0);

    s_mic.cap = lv_obj_create(page);
    lv_obj_remove_flag(s_mic.cap, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(s_mic.cap, 0, 0);
    lv_obj_set_style_radius(s_mic.cap, WIN_PX(6), 0);
    lv_obj_set_style_bg_color(s_mic.cap, lv_color_hex(0x0A101E), 0);
    lv_obj_set_style_bg_opa(s_mic.cap, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_mic.cap, 1, 0);
    lv_obj_set_style_border_color(s_mic.cap, lv_color_hex(0x186A86), 0);
    lv_obj_set_style_border_opa(s_mic.cap, LV_OPA_50, 0);

    s_mic.wave = lv_canvas_create(s_mic.cap);
    lv_obj_remove_flag(s_mic.wave, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_mic.wave, 0, 0);
    lv_obj_set_style_border_width(s_mic.wave, 0, 0);
    lv_obj_set_style_radius(s_mic.wave, 0, 0);

    s_mic.lvl_name = mic_label(page, WIN_FONT_TINY, WIN_COLOR_TEXT_MUTED,
                               LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(s_mic.lvl_name, _("mic_input"));

    s_mic.lvl_val = mic_label(page, WIN_FONT_SMALL, lv_color_hex(0x2AE6FF),
                              LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(s_mic.lvl_val, "0%");

    /* Pre-fill the waveform so the first frame is a live trace, not a flat
     * line (the sample history fills in only after a few seconds). */
    for (int i = 0; i < MT_SAMPLES; i++) {
        s_mic.samples[i] = 0.45f * sinf((float)i * 0.33f)
                         + 0.24f * sinf((float)i * 0.87f + 1.2f)
                         + 0.09f * ((float)((rand() % 101) - 50) / 50.0f);
    }

    lv_obj_add_event_cb(page, mic_size_cb, LV_EVENT_SIZE_CHANGED, NULL);

    ui_mic_relayout();
    return page;
}

void ui_mic_start(void)
{
    if (s_mic.page == NULL || s_mic.tmr != NULL) {
        return;
    }
    s_mic.tmr = lv_timer_create(mic_tick, MIC_FPS_MS, NULL);
    mic_scope_render();
    mic_wave_render();
}

void ui_mic_stop(void)
{
    if (s_mic.tmr != NULL) {
        lv_timer_delete(s_mic.tmr);
        s_mic.tmr = NULL;
    }
}
