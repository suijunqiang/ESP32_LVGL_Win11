#include "ui_win_app_dots.h"

#include "ui_win_theme.h"
#include "lv_i18n.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "esp_heap_caps.h"

/*
 * Tab 4: the dot array.
 *
 * Port of the reference design (see arrayDot.txt): one 320-point style preview
 * above a 3 x 3 grid of small previews, nine generator functions, max-blend
 * compositing (so draw order never matters), fibonacci sphere seeds and a
 * 0.38 s screen-space migration when the shape changes.
 *
 * The reference runs at 2560 x 1600 in ARGB8888 with 1490 points. Here the
 * canvases are RGB565 and sized to the tab page, so the point counts and dot
 * radii are scaled down; everything else follows the same pipeline.
 */

#define DT_SHAPES       9
#define DT_COLS         3
#define DT_PREVIEW_PTS  170
#define DT_THUMB_PTS    64

#define DT_TRANS_SEC    0.38f
#define DT_SPIN         0.80f
#define DT_PI           3.14159265f

/* Colours from the reference layout. */
#define DT_C_PANEL      0x0A0C11
#define DT_C_CARD       0x0B0E13
#define DT_C_CARD_ON    0x151A24
#define DT_BG565        ((uint16_t)(((0x0A >> 3) << 11) | ((0x0C >> 2) << 5) | (0x11 >> 3)))

typedef struct {
    float dx, dy, dz;   /* unit direction                    */
    float phi;          /* polar angle, 0..pi                */
    float th;           /* azimuth, i * golden angle         */
    float u;            /* 0..1, groups shapes by index      */
    float half;         /* 1 for odd indexes, 0 for even     */
} dt_seed_t;

typedef struct {
    float x, y, r, l;   /* screen position, radius, luma 0..1 */
} dt_dot_t;

typedef struct {
    lv_obj_t *card;
    lv_obj_t *canvas;
    uint16_t *buf;
    int32_t   w;
    int32_t   h;
    int       n;
    dt_dot_t *from;     /* transition start                  */
    dt_dot_t *shown;    /* last displayed state              */
} dt_tile_t;

static struct {
    lv_obj_t  *page;
    lv_obj_t  *head;
    lv_obj_t  *sub;
    dt_tile_t  preview;
    dt_tile_t  tile[DT_SHAPES];
    dt_seed_t  seed[DT_PREVIEW_PTS];
    uint16_t   ramp[256];
    int        shape;
    float      t;
    float      trans;
    lv_timer_t *tmr;
    int32_t    laid_w;
    int32_t    laid_h;
} s_dot;

/* Scratch for one frame: one tile at a time. */
static dt_dot_t s_cur[DT_PREVIEW_PTS];

static const char *const dt_name_key[DT_SHAPES] = {
    "dot_breathe", "dot_scan", "dot_spin", "dot_swirl", "dot_lattice",
    "dot_shells", "dot_orbit", "dot_weave", "dot_shape",
};

static const char *const dt_sub_key[DT_SHAPES] = {
    "dot_breathe_sub", "dot_scan_sub", "dot_spin_sub", "dot_swirl_sub",
    "dot_lattice_sub", "dot_shells_sub", "dot_orbit_sub", "dot_weave_sub",
    "dot_shape_sub",
};

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static float dt_smooth(float x)
{
    if (x < 0.0f) {
        x = 0.0f;
    } else if (x > 1.0f) {
        x = 1.0f;
    }
    return x * x * (3.0f - 2.0f * x);
}

static void dt_rot_y(float *x, float *z, float a)
{
    float c = cosf(a), s = sinf(a);
    float nx = *x * c + *z * s;
    *z = -(*x) * s + *z * c;
    *x = nx;
}

static void dt_to_cube(float *x, float *y, float *z)
{
    float m = fabsf(*x);
    if (fabsf(*y) > m) {
        m = fabsf(*y);
    }
    if (fabsf(*z) > m) {
        m = fabsf(*z);
    }
    float k = 1.0f / (m + 1e-6f);
    *x *= k;
    *y *= k;
    *z *= k;
}

/* ------------------------------------------------------------------ */
/* Shape generators                                                    */
/* ------------------------------------------------------------------ */

/* Model space position plus size (sm) and luma (lm) modulation. */
static void dt_shape_eval(const dt_seed_t *ps, int sh, float t,
                          float *ox, float *oy, float *oz,
                          float *osm, float *olm)
{
    float x = ps->dx;
    float y = ps->dy;
    float z = ps->dz;
    float sm = 1.0f;
    float lm = 1.0f;

    switch (sh) {
    case 0: { /* BREATHE */
        float waves = sinf(ps->phi * 5.0f - t * 3.0f);
        float rr = 1.0f + 0.08f * waves;
        x *= rr;
        y *= rr;
        z *= rr;
        sm = 1.0f + 0.30f * fabsf(waves);
        break;
    }
    case 1: { /* SCAN: azimuth band */
        float az = atan2f(z, x);
        float d = az - t * 1.4f;
        while (d > DT_PI) {
            d -= 2.0f * DT_PI;
        }
        while (d < -DT_PI) {
            d += 2.0f * DT_PI;
        }
        float k = 1.0f - fabsf(d) / 0.9f;
        if (k < 0.0f) {
            k = 0.0f;
        }
        sm = 0.60f + 1.30f * k;
        lm = 0.35f + 1.35f * k;
        break;
    }
    case 2: { /* SPIN: extra rotation is added by the render loop */
        sm = 1.0f;
        break;
    }
    case 3: { /* SWIRL: each azimuth band spins at its own speed */
        float az = fabsf(atan2f(z, x));
        int band = (int)(az * 9.0f / DT_PI);
        if (band < 0) {
            band = 0;
        } else if (band > 8) {
            band = 8;
        }
        dt_rot_y(&x, &z, t * (2.60f - 0.22f * (float)band));
        if (band & 1) {
            sm = 1.35f;
            lm = 1.12f;
        }
        break;
    }
    case 4: { /* LATTICE: a 7 x 7 grid on each of the six cube faces */
        int face = (int)(ps->u * 6.0f);
        if (face > 5) {
            face = 5;
        }
        float a = ps->u * 6.0f - (float)face;
        float b = fmodf(ps->th, 2.0f * DT_PI) * 0.15915494f;
        int gi = (int)(a * 7.0f);
        int gj = (int)(b * 7.0f);
        if (gi > 6) {
            gi = 6;
        }
        if (gj > 6) {
            gj = 6;
        }
        float p = (((float)gi + 0.5f) / 7.0f) * 2.0f - 1.0f;
        float q = (((float)gj + 0.5f) / 7.0f) * 2.0f - 1.0f;
        switch (face) {
        case 0:  x =  1.0f; y = p;    z = q;    break;
        case 1:  x = -1.0f; y = p;    z = q;    break;
        case 2:  x = p;     y =  1.0f; z = q;   break;
        case 3:  x = p;     y = -1.0f; z = q;   break;
        case 4:  x = p;     y = q;    z =  1.0f; break;
        default: x = p;     y = q;    z = -1.0f; break;
        }
        float k = 0.62f;
        x *= k;
        y *= k;
        z *= k;
        sm = 0.85f;
        break;
    }
    case 5: { /* SHELLS: outer shell and a counter rotating core */
        if (ps->half > 0.5f) {
            dt_rot_y(&x, &z, -t * 1.55f);
            x *= 0.55f;
            y *= 0.55f;
            z *= 0.55f;
            sm = 0.70f;
            lm = 0.68f;
        } else {
            dt_rot_y(&x, &z, t * 0.70f);
        }
        break;
    }
    case 6: { /* ORBIT: four tilted rings */
        int g = (int)(ps->u * 4.0f);
        if (g > 3) {
            g = 3;
        }
        float a = ps->th + t * (0.90f + 0.25f * (float)g);
        x = cosf(a);
        y = 0.16f * (float)g - 0.24f;
        z = sinf(a);
        float tilt = 0.45f + 0.62f * (float)g;
        float c = cosf(tilt), s = sinf(tilt);
        float ny = y * c - z * s;
        float nz = y * s + z * c;
        y = ny;
        z = nz;
        break;
    }
    case 7: { /* WEAVE: meridians and quantised parallels */
        if (ps->half > 0.5f) {
            dt_rot_y(&x, &z, -t * 0.80f);
            sm = 0.80f;
        } else {
            dt_rot_y(&x, &z, t * 0.80f);
            sm = 1.15f;
            lm = 1.05f;
            float q = floorf(y * 5.0f + 0.5f) / 5.0f;
            y = y * 0.22f + q * 0.78f;
        }
        break;
    }
    case 8: { /* SHAPE: sphere, cube and torus, smoothstepped */
        float ax = ps->dx, ay = ps->dy, az = ps->dz;
        float bx, by, bz;
        dt_to_cube(&x, &y, &z);
        x *= 0.62f;
        y *= 0.62f;
        z *= 0.62f;

        bx = (0.62f + 0.28f * cosf(ps->phi)) * cosf(ps->th);
        by = 0.28f * sinf(ps->phi);
        bz = (0.62f + 0.28f * cosf(ps->phi)) * sinf(ps->th);

        float ph = fmodf(t * 0.18f, 3.0f);
        if (ph < 1.0f) {
            float e = dt_smooth(ph);
            x = ax + (x - ax) * e;
            y = ay + (y - ay) * e;
            z = az + (z - az) * e;
        } else if (ph < 2.0f) {
            float e = dt_smooth(ph - 1.0f);
            x = x + (bx - x) * e;
            y = y + (by - y) * e;
            z = z + (bz - z) * e;
        } else {
            float e = dt_smooth(ph - 2.0f);
            x = x + (ax - x) * e;
            y = y + (by - y) * e;
            z = z + (az - z) * e;
        }
        break;
    }
    default:
        break;
    }

    *ox = x;
    *oy = y;
    *oz = z;
    *osm = sm;
    *olm = lm;
}

/* ------------------------------------------------------------------ */
/* Renderer                                                            */
/* ------------------------------------------------------------------ */

static uint16_t dt_max565(uint16_t a, uint16_t b)
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

static void dt_tile_free(dt_tile_t *tile)
{
    if (tile->buf != NULL) {
        heap_caps_free(tile->buf);
        tile->buf = NULL;
    }
    if (tile->from != NULL) {
        heap_caps_free(tile->from);
        tile->from = NULL;
    }
    if (tile->shown != NULL) {
        heap_caps_free(tile->shown);
        tile->shown = NULL;
    }
    tile->w = 0;
    tile->h = 0;
}

static void *dt_malloc(size_t bytes)
{
    void *p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (p == NULL) {
        p = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    }
    return p;
}

static void dt_tile_alloc(dt_tile_t *tile, int32_t w, int32_t h, int n)
{
    if (w < 4) {
        w = 4;
    }
    if (h < 4) {
        h = 4;
    }
    if (tile->buf != NULL && tile->w == w && tile->h == h && tile->n == n) {
        return;
    }

    dt_tile_free(tile);
    tile->buf = dt_malloc((size_t)w * h * sizeof(uint16_t));
    tile->from = dt_malloc((size_t)n * sizeof(dt_dot_t));
    tile->shown = dt_malloc((size_t)n * sizeof(dt_dot_t));
    if (tile->buf == NULL || tile->from == NULL || tile->shown == NULL) {
        dt_tile_free(tile);
        lv_obj_set_size(tile->canvas, 0, 0);
        return;
    }
    tile->w = w;
    tile->h = h;
    tile->n = n;
    memset(tile->shown, 0, (size_t)n * sizeof(dt_dot_t));
    memset(tile->from, 0, (size_t)n * sizeof(dt_dot_t));

    lv_canvas_set_buffer(tile->canvas, tile->buf, w, h, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_size(tile->canvas, w, h);
}

static void dt_tile_calc(const dt_tile_t *tile, int shape, float t)
{
    float r_min = (float)((tile->w < tile->h) ? tile->w : tile->h);
    float rad = r_min * 0.5f - r_min * 0.08f;      /* keeps the worst phase inside */
    float cx = (float)tile->w * 0.5f;
    float cy = (float)tile->h * 0.5f;
    float base_r = rad * 0.055f;
    if (base_r < 0.95f) {
        base_r = 0.95f;      /* keep the small thumbs legible */
    }

    float ay = t * DT_SPIN + ((shape == 2) ? t * 0.90f : 0.0f);
    float ax = 0.30f + 0.34f * sinf(t * 0.41f);
    float cay = cosf(ay), say = sinf(ay);
    float cax = cosf(ax), sax = sinf(ax);

    for (int i = 0; i < tile->n; i++) {
        int si = (tile->n >= DT_PREVIEW_PTS) ? i
                                             : (i * DT_PREVIEW_PTS) / tile->n;
        if (si >= DT_PREVIEW_PTS) {
            si = DT_PREVIEW_PTS - 1;
        }
        float x, y, z, sm, lm;
        dt_shape_eval(&s_dot.seed[si], shape, t, &x, &y, &z, &sm, &lm);

        float nx = cay * x + say * z;
        float nz = -say * x + cay * z;
        x = nx;
        z = nz;

        float ny = cax * y - sax * z;
        float nz2 = sax * y + cax * z;
        y = ny;
        z = nz2;

        float p = 1.0f / (1.0f + z * 0.22f);
        dt_dot_t *d = &s_cur[i];
        d->x = cx + x * rad * p;
        d->y = cy - y * rad * p;
        d->r = base_r * p * sm;
        d->l = (0.40f + 0.60f * (z * 0.5f + 0.5f)) * lm;
        if (d->l > 1.2f) {
            d->l = 1.2f;
        }
        if (d->r > 9.0f) {
            d->r = 9.0f;
        }
    }
}

static void dt_tile_render(dt_tile_t *tile, int shape, float t, float u)
{
    if (tile->buf == NULL || tile->n <= 0) {
        return;
    }

    int32_t w = tile->w;
    int32_t h = tile->h;
    uint16_t *buf = tile->buf;

    for (int32_t i = 0; i < w * h; i++) {
        buf[i] = DT_BG565;
    }

    dt_tile_calc(tile, shape, t);

    for (int i = 0; i < tile->n; i++) {
        dt_dot_t *c = &s_cur[i];
        dt_dot_t *s = &tile->shown[i];
        if (u < 1.0f) {
            const dt_dot_t *f = &tile->from[i];
            s->x = f->x + (c->x - f->x) * u;
            s->y = f->y + (c->y - f->y) * u;
            s->r = f->r + (c->r - f->r) * u;
            s->l = f->l + (c->l - f->l) * u;
        } else {
            *s = *c;
        }
    }

    for (int i = 0; i < tile->n; i++) {
        const dt_dot_t *d = &tile->shown[i];
        if (d->r < 0.30f || d->l <= 0.005f) {
            continue;
        }
        float e = d->r + 0.75f;
        float e2 = e * e;
        int32_t x0 = (int32_t)floorf(d->x - e);
        int32_t x1 = (int32_t)ceilf(d->x + e);
        int32_t y0 = (int32_t)floorf(d->y - e);
        int32_t y1 = (int32_t)ceilf(d->y + e);
        if (x0 < 0) {
            x0 = 0;
        }
        if (y0 < 0) {
            y0 = 0;
        }
        if (x1 > w - 1) {
            x1 = w - 1;
        }
        if (y1 > h - 1) {
            y1 = h - 1;
        }
        float lm = d->l * 255.0f;
        for (int32_t y = y0; y <= y1; y++) {
            float dy = (float)y + 0.5f - d->y;
            float dy2 = dy * dy;
            uint16_t *row = buf + (size_t)y * w;
            for (int32_t x = x0; x <= x1; x++) {
                float dx = (float)x + 0.5f - d->x;
                float d2 = dx * dx + dy2;
                if (d2 >= e2) {
                    continue;
                }
                float cov = (e - sqrtf(d2)) * (1.0f / 1.5f);
                if (cov > 1.0f) {
                    cov = 1.0f;
                }
                int v = (int)(lm * cov);
                if (v > 255) {
                    v = 255;
                }
                if (v > 0) {
                    row[x] = dt_max565(row[x], s_dot.ramp[v]);
                }
            }
        }
    }

    lv_obj_invalidate(tile->canvas);
}

static void dt_render_all(float u)
{
    dt_tile_render(&s_dot.preview, s_dot.shape, s_dot.t, u);
    for (int i = 0; i < DT_SHAPES; i++) {
        dt_tile_render(&s_dot.tile[i], i, s_dot.t, u);
    }
}

/* Thumbnails refresh every other frame to keep the flush cost down. */
static int s_thumb_phase;

static void dt_tick(lv_timer_t *tmr)
{
    (void)tmr;
    s_dot.t += 0.033f;

    if (s_dot.trans < 1.0f) {
        s_dot.trans += 0.033f / DT_TRANS_SEC;
        if (s_dot.trans > 1.0f) {
            s_dot.trans = 1.0f;
        }
    }

    float u = dt_smooth(s_dot.trans);
    dt_tile_render(&s_dot.preview, s_dot.shape, s_dot.t, u);

    s_thumb_phase ^= 1;
    for (int i = 0; i < DT_SHAPES; i++) {
        int phase = (i + s_thumb_phase) & 1;
        if (phase != 0 && i != s_dot.shape) {
            continue;
        }
        dt_tile_render(&s_dot.tile[i], i, s_dot.t, u);
    }
}

/* ------------------------------------------------------------------ */
/* Style                                                               */
/* ------------------------------------------------------------------ */

static void dt_style_tiles(void)
{
    for (int i = 0; i < DT_SHAPES; i++) {
        bool on = (i == s_dot.shape);
        lv_obj_t *card = s_dot.tile[i].card;
        lv_obj_set_style_bg_color(card, lv_color_hex(on ? DT_C_CARD_ON : DT_C_CARD), 0);
        lv_obj_set_style_border_width(card, on ? 1 : 0, 0);
        lv_obj_set_style_border_color(card, WIN_COLOR_ACCENT_SOFT, 0);
    }
}

static void dt_select(int idx, bool animate)
{
    if (idx < 0) {
        idx = DT_SHAPES - 1;
    } else if (idx >= DT_SHAPES) {
        idx = 0;
    }
    if (idx == s_dot.shape && animate) {
        return;
    }

    /* Freeze the currently displayed positions as the transition start. */
    dt_tile_t *all = &s_dot.preview;
    for (int i = -1; i < DT_SHAPES; i++) {
        dt_tile_t *tile = (i < 0) ? all : &s_dot.tile[i];
        if (tile->from != NULL && tile->shown != NULL) {
            memcpy(tile->from, tile->shown, (size_t)tile->n * sizeof(dt_dot_t));
        }
    }

    s_dot.shape = idx;
    s_dot.trans = animate ? 0.0f : 1.0f;

    lv_label_set_text(s_dot.head, _(dt_name_key[idx]));
    lv_label_set_text(s_dot.sub, _(dt_sub_key[idx]));
    dt_style_tiles();
    dt_render_all(dt_smooth(s_dot.trans));
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

static void dt_tile_cb(lv_event_t *e)
{
    dt_select((int)(intptr_t)lv_event_get_user_data(e), true);
}

static void dt_preview_cb(lv_event_t *e)
{
    (void)e;
    dt_select(s_dot.shape + 1, true);
}

static void dt_size_cb(lv_event_t *e)
{
    (void)e;
    ui_dots_relayout();
}

static void dt_delete_cb(lv_event_t *e)
{
    (void)e;
    ui_dots_stop();
    dt_tile_free(&s_dot.preview);
    for (int i = 0; i < DT_SHAPES; i++) {
        dt_tile_free(&s_dot.tile[i]);
    }
    s_dot.page = NULL;
    s_dot.laid_w = 0;
    s_dot.laid_h = 0;
}

/* ------------------------------------------------------------------ */
/* Build                                                               */
/* ------------------------------------------------------------------ */

static lv_obj_t *dt_card_create(lv_obj_t *parent, uint32_t radius, uint32_t rgb)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(card, (lv_coord_t)radius, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(rgb), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    return card;
}

static lv_obj_t *dt_canvas_create(lv_obj_t *card)
{
    lv_obj_t *c = lv_canvas_create(card);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, 0, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    return c;
}

void ui_dots_relayout(void)
{
    if (s_dot.page == NULL) {
        return;
    }
    int32_t w = lv_obj_get_width(s_dot.page);
    int32_t h = lv_obj_get_height(s_dot.page);
    if (w < 20 || h < 20) {
        return;
    }
    if (w == s_dot.laid_w && h == s_dot.laid_h) {
        return;
    }
    s_dot.laid_w = w;
    s_dot.laid_h = h;

    const int32_t margin = WIN_PX(6);
    const int32_t gap = WIN_PX(4);
    const int32_t head_h = WIN_PX(6) + 24;  /* title + subtitle, both 14 px */
    int32_t pw = w - 2 * margin;
    int32_t avail = h - head_h;
    int32_t ph = (avail * 56) / 100;
    if (ph < 36) {
        ph = avail / 2;
    }

    lv_obj_set_pos(s_dot.head, margin, 1);
    lv_obj_set_size(s_dot.head, pw, 14);
    lv_obj_set_pos(s_dot.sub, margin, 15);
    lv_obj_set_size(s_dot.sub, pw, 14);

    int32_t inset = WIN_PX(3);
    lv_obj_set_pos(s_dot.preview.card, margin, head_h);
    lv_obj_set_size(s_dot.preview.card, pw, ph);
    lv_obj_set_pos(s_dot.preview.canvas, inset, inset);
    dt_tile_alloc(&s_dot.preview, pw - 2 * inset, ph - 2 * inset, DT_PREVIEW_PTS);

    int32_t rest_h = h - head_h - ph - gap;
    int32_t cell_w = (pw - (DT_COLS - 1) * gap) / DT_COLS;
    int32_t cell_h = (rest_h - 2 * gap) / DT_COLS;
    if (cell_w < 12) {
        cell_w = 12;
    }
    if (cell_h < 8) {
        cell_h = 8;
    }

    for (int i = 0; i < DT_SHAPES; i++) {
        int col = i % DT_COLS;
        int row = i / DT_COLS;
        int32_t x = margin + col * (cell_w + gap);
        int32_t y = head_h + ph + gap + row * (cell_h + gap);
        lv_obj_set_pos(s_dot.tile[i].card, x, y);
        lv_obj_set_size(s_dot.tile[i].card, cell_w, cell_h);
        lv_obj_set_pos(s_dot.tile[i].canvas, 1, 1);
        dt_tile_alloc(&s_dot.tile[i], cell_w - 2, cell_h - 2, DT_THUMB_PTS);
    }

    dt_render_all(dt_smooth(s_dot.trans));
}

lv_obj_t *ui_dots_create(lv_obj_t *page)
{
    memset(&s_dot, 0, sizeof(s_dot));
    s_dot.page = page;
    s_dot.shape = 0;
    s_dot.trans = 1.0f;
    s_dot.laid_w = 0;
    s_dot.laid_h = 0;

    /* Fibonacci sphere seeds, cached once. */
    for (int i = 0; i < DT_PREVIEW_PTS; i++) {
        dt_seed_t *s = &s_dot.seed[i];
        s->phi = acosf(1.0f - 2.0f * ((float)i + 0.5f) / (float)DT_PREVIEW_PTS);
        s->th = (float)i * 2.39996323f;
        s->dx = sinf(s->phi) * cosf(s->th);
        s->dy = cosf(s->phi);
        s->dz = sinf(s->phi) * sinf(s->th);
        s->u = (float)i / (float)(DT_PREVIEW_PTS - 1);
        s->half = (i & 1) ? 1.0f : 0.0f;
    }

    /* Cool white ramp: R/G slightly below B, like the reference. */
    for (int i = 0; i < 256; i++) {
        int rr = (i * 0x90) / 255;
        int gg = (i * 0x95) / 255;
        s_dot.ramp[i] = (uint16_t)(((rr >> 3) << 11) | ((gg >> 2) << 5) | (i >> 3));
    }

    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_set_style_bg_color(page, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);

    s_dot.head = lv_label_create(page);
    lv_obj_set_style_text_font(s_dot.head, WIN_FONT_SMALL, 0);
    lv_obj_set_style_text_color(s_dot.head, WIN_COLOR_TEXT, 0);
    lv_obj_set_style_text_align(s_dot.head, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_dot.head, LV_LABEL_LONG_CLIP);

    s_dot.sub = lv_label_create(page);
    lv_obj_set_style_text_font(s_dot.sub, WIN_FONT_TINY, 0);
    lv_obj_set_style_text_color(s_dot.sub, WIN_COLOR_TEXT_MUTED, 0);
    lv_obj_set_style_text_align(s_dot.sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_dot.sub, LV_LABEL_LONG_CLIP);

    s_dot.preview.card = dt_card_create(page, WIN_PX(8), DT_C_PANEL);
    s_dot.preview.canvas = dt_canvas_create(s_dot.preview.card);
    lv_obj_add_event_cb(s_dot.preview.card, dt_preview_cb, LV_EVENT_CLICKED, NULL);

    for (int i = 0; i < DT_SHAPES; i++) {
        s_dot.tile[i].card = dt_card_create(page, WIN_PX(5), DT_C_CARD);
        s_dot.tile[i].canvas = dt_canvas_create(s_dot.tile[i].card);
        lv_obj_add_event_cb(s_dot.tile[i].card, dt_tile_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }

    lv_obj_add_event_cb(page, dt_size_cb, LV_EVENT_SIZE_CHANGED, NULL);
    lv_obj_add_event_cb(page, dt_delete_cb, LV_EVENT_DELETE, NULL);

    dt_select(0, false);
    ui_dots_relayout();
    return page;
}

void ui_dots_start(void)
{
    if (s_dot.page == NULL || s_dot.tmr != NULL) {
        return;
    }
    s_dot.tmr = lv_timer_create(dt_tick, 33, NULL);
    dt_render_all(dt_smooth(s_dot.trans));
}

void ui_dots_stop(void)
{
    if (s_dot.tmr != NULL) {
        lv_timer_delete(s_dot.tmr);
        s_dot.tmr = NULL;
    }
}
