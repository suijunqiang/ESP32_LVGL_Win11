/*
 * The "UI" demo application.
 *
 * The window content is a Tabview with the tab bar on the left and three
 * pages:
 *
 *   1. honeycomb   the Apple Watch honeycomb app menu (37 icons, fisheye,
 *                  free 2D panning with momentum)
 *   2. void menu   a particle galaxy: eight planets orbit a black hole, with
 *                  a detail column; drag to roll the ring, tap a planet to
 *                  choose its destination
 *   3. silk menu   a coverflow of eight app cards on an arc, with drag and
 *                  snap physics
 *
 * Only the active page runs a timer. The pages are built once when the window
 * opens and are destroyed with it (the desktop destroys the whole window tree
 * on close), so no page has to be rebuilt by hand.
 *
 * The reference demos are designed for a large desktop display; every metric
 * here is derived from the actual page size, so the menus also fit the small
 * window before it is maximized.
 *
 * Transforms (rotation / scale) are deliberately not used: every transformed
 * object is drawn through an off-screen layer, and a handful of those per
 * frame is already too expensive on this MCU. The "zoom" is done by resizing
 * objects and by mixing their colours towards the background, the same
 * technique the honeycomb already uses.
 */

#include "lv_i18n.h"
#include "ui_win_app_ui.h"
#include "ui_win_theme.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Honeycomb layout constants                                         */
/* ------------------------------------------------------------------ */

#define HC_RADIUS       3               /* 1 + 6 + 12 + 18 = 37 cells  */
#define HC_COUNT        37
#define HC_ICON_MIN     WIN_PX(12)      /* icon diameter at the rim    */
#define HC_ICON_MAX     WIN_PX(36)      /* icon diameter at the centre */
#define HC_DX           WIN_PX(24)      /* horizontal cell pitch       */
#define HC_DY           WIN_PX(28)      /* vertical cell pitch         */
#define HC_DIAL_SIZE    WIN_PX(172)
#define HC_CASE_SIZE    WIN_PX(182)
/* Keep the panning range small: the comb must always cover the dial, so the
 * finger can never fling every icon out of the circle. */
#define HC_PAN          WIN_PX(48)
#define HC_CANVAS_SIZE  (HC_DIAL_SIZE + 2 * HC_PAN)

/* Radius at which an icon is fully shrunk and dimmed. The outermost cells sit
 * at about 0.8 of it, so the rim cells keep a little of their size. */
#define HC_RIM          ((float)(HC_DIAL_SIZE / 2) * 1.15f)

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

/* A cheap deterministic random generator, so the star fields are stable
 * between boots without pulling in rand(). */
static uint32_t s_rng = 0x12345678u;

static float ui_rand01(void)
{
    s_rng = s_rng * 1103515245u + 12345u;
    return (float)((s_rng >> 8) & 0xFFFFu) / 65535.0f;
}

/* Absolute colours are mixed towards the background instead of using an
 * opacity style: an opacity below 100% would force LVGL to draw the object
 * through a layer, which is exactly what this UI cannot afford. */
static lv_color_t ui_fade(lv_color_t color, lv_color_t bg, float amount)
{
    if (amount < 0.0f) {
        amount = 0.0f;
    }
    if (amount > 1.0f) {
        amount = 1.0f;
    }

    return lv_color_mix(bg, color, (lv_opa_t)(amount * 255.0f));
}

/* Shortest signed distance between two positions on a ring of n steps. This
 * makes both carousels wrap around and always take the short way round. */
static float ui_ring_delta(float d, int32_t n)
{
    d = fmodf(d, (float)n);
    if (d >= (float)n / 2.0f) {
        d -= (float)n;
    }
    if (d < -(float)n / 2.0f) {
        d += (float)n;
    }
    return d;
}

/* ------------------------------------------------------------------ */
/* Partial state                                                      */
/* ------------------------------------------------------------------ */

static lv_obj_t *s_tv;              /* the tabview                        */
static uint32_t s_tab;              /* active tab index                   */
static bool s_paused;               /* the window is hidden               */

static lv_timer_t *s_hc_fling;
static float s_hc_fl_vx;
static float s_hc_fl_vy;

static lv_timer_t *s_vd_tmr;
static lv_timer_t *s_sk_tmr;

static void vd_start(void);
static void vd_stop(void);
static void sk_start(void);
static void sk_stop(void);
static void hc_fling_stop(void);
static void vd_layout(void);
static void vd_size_cb(lv_event_t *event);
static void sk_layout(void);
static void sk_size_cb(lv_event_t *event);

/* ================================================================== */
/* Page 1: honeycomb                                                  */
/* ================================================================== */

typedef struct {
    lv_obj_t *icon;
    lv_obj_t *glyph;
    lv_color_t color;
    int32_t cx;                         /* centre inside the canvas    */
    int32_t cy;
    int32_t size;                       /* last applied diameter       */
    lv_opa_t mix;                       /* last applied brightness     */
} hc_cell_t;

typedef struct {
    lv_obj_t *dial;
    lv_obj_t *grid;                     /* scrollable container        */
    lv_obj_t *canvas;                   /* larger than the dial        */
    hc_cell_t cells[HC_COUNT];
} hc_ctx_t;

static hc_ctx_t s_hc;

static const char *const hc_glyphs[] = {
    LV_SYMBOL_CALL, LV_SYMBOL_ENVELOPE, LV_SYMBOL_AUDIO, LV_SYMBOL_IMAGE,
    LV_SYMBOL_SETTINGS, LV_SYMBOL_GPS, LV_SYMBOL_WIFI, LV_SYMBOL_BLUETOOTH,
    LV_SYMBOL_EDIT, LV_SYMBOL_FILE, LV_SYMBOL_DIRECTORY, LV_SYMBOL_VIDEO,
    LV_SYMBOL_BELL, LV_SYMBOL_TINT, LV_SYMBOL_LOOP, LV_SYMBOL_BARS,
    LV_SYMBOL_POWER, LV_SYMBOL_LIST, LV_SYMBOL_HOME, LV_SYMBOL_OK,
    LV_SYMBOL_DRIVE, LV_SYMBOL_PLAY, LV_SYMBOL_SHUFFLE, LV_SYMBOL_CHARGE,
    LV_SYMBOL_REFRESH, LV_SYMBOL_EYE_OPEN, LV_SYMBOL_SAVE, LV_SYMBOL_COPY,
    LV_SYMBOL_CUT, LV_SYMBOL_PASTE, LV_SYMBOL_DOWNLOAD, LV_SYMBOL_UPLOAD,
    LV_SYMBOL_SD_CARD, LV_SYMBOL_USB, LV_SYMBOL_KEYBOARD, LV_SYMBOL_WARNING,
    LV_SYMBOL_PLUS,
};

static lv_color_t hc_colour(unsigned i)
{
    static const uint32_t rgb[] = {
        0xE8455A, 0xE8A33C, 0x38BDF8, 0x1BA37B, 0x8C5CF7, 0xD946EF,
        0x0EA5E9, 0x22C55E, 0xF59E0B, 0xEF4444, 0x14B8A6, 0x6366F1,
    };

    return lv_color_hex(rgb[i % ARRAY_LEN(rgb)]);
}

static void hc_apply_cell(hc_cell_t *cell, float dist)
{
    float ratio = dist / HC_RIM;
    if (ratio > 1.0f) {
        ratio = 1.0f;
    }

    const float fall = (1.0f - ratio) * (1.0f - ratio);

    const int32_t size = HC_ICON_MIN +
                         (int32_t)((HC_ICON_MAX - HC_ICON_MIN) * fall);
    if (size != cell->size) {
        cell->size = size;
        lv_obj_set_size(cell->icon, size, size);
        lv_obj_set_pos(cell->icon, cell->cx - size / 2, cell->cy - size / 2);
    }

    /* 16 brightness steps: the eye cannot see more, and every step that is
     * skipped is a style change (and a redraw) that is skipped as well. */
    const int32_t dim = 255 - (int32_t)(175.0f * fall);
    const lv_opa_t mix = (lv_opa_t)((dim / 16) * 16);
    if (mix != cell->mix) {
        cell->mix = mix;
        lv_obj_set_style_bg_color(cell->icon,
                                  lv_color_mix(cell->color, lv_color_black(),
                                               mix),
                                  LV_PART_MAIN);
        lv_obj_set_style_text_color(cell->glyph,
                                    lv_color_mix(lv_color_white(),
                                                 lv_color_black(), mix),
                                    LV_PART_MAIN);
    }
}

static void hc_apply_fisheye(void)
{
    if (s_hc.canvas == NULL) {
        return;
    }

    /* Read the STYLE position: the resolved coords are still (0,0) before the
     * first layout pass, which would offset the zoom towards the left-top. */
    const int32_t sx = -(lv_obj_get_style_x(s_hc.canvas, LV_PART_MAIN) + HC_PAN);
    const int32_t sy = -(lv_obj_get_style_y(s_hc.canvas, LV_PART_MAIN) + HC_PAN);
    const int32_t cx = HC_DIAL_SIZE / 2;
    const int32_t cy = HC_DIAL_SIZE / 2;

    for (unsigned i = 0; i < HC_COUNT; i++) {
        hc_cell_t *cell = &s_hc.cells[i];
        if (cell->icon == NULL) {
            continue;
        }

        const int32_t dx = cell->cx - HC_PAN - sx - cx;
        const int32_t dy = cell->cy - HC_PAN - sy - cy;

        hc_apply_cell(cell, sqrtf((float)(dx * dx + dy * dy)));
    }
}

static void hc_pan_by(int32_t dx, int32_t dy)
{
    lv_obj_t *canvas = s_hc.canvas;
    if (canvas == NULL) {
        return;
    }

    int32_t nx = lv_obj_get_style_x(canvas, LV_PART_MAIN) + dx;
    int32_t ny = lv_obj_get_style_y(canvas, LV_PART_MAIN) + dy;
    /* The canvas is 2 * HC_PAN bigger than the dial: keep it covering it. */
    if (nx < -2 * HC_PAN) nx = -2 * HC_PAN;
    if (nx > 0) nx = 0;
    if (ny < -2 * HC_PAN) ny = -2 * HC_PAN;
    if (ny > 0) ny = 0;
    if (nx == lv_obj_get_style_x(canvas, LV_PART_MAIN) &&
        ny == lv_obj_get_style_y(canvas, LV_PART_MAIN)) {
        return;
    }

    lv_obj_set_pos(canvas, nx, ny);
    hc_apply_fisheye();
}

static void hc_fling_cb(lv_timer_t *timer)
{
    s_hc_fl_vx *= 0.88f;
    s_hc_fl_vy *= 0.88f;
    if (fabsf(s_hc_fl_vx) < 0.8f && fabsf(s_hc_fl_vy) < 0.8f) {
        lv_timer_delete(timer);
        s_hc_fling = NULL;
        return;
    }
    hc_pan_by((int32_t)s_hc_fl_vx, (int32_t)s_hc_fl_vy);
}

static void hc_fling_stop(void)
{
    if (s_hc_fling != NULL) {
        lv_timer_delete(s_hc_fling);
        s_hc_fling = NULL;
    }
    s_hc_fl_vx = 0.0f;
    s_hc_fl_vy = 0.0f;
}

static void hc_delete_cb(lv_event_t *event)
{
    (void)event;
    hc_fling_stop();
    memset(&s_hc, 0, sizeof(s_hc));
}

static void hc_press_cb(lv_event_t *event)
{
    switch (lv_event_get_code(event)) {
        case LV_EVENT_PRESSED:
            hc_fling_stop();
            break;

        case LV_EVENT_PRESSING: {
            lv_indev_t *indev = lv_indev_active();
            if (indev == NULL) {
                break;
            }
            lv_point_t vect;
            lv_indev_get_vect(indev, &vect);
            if (vect.x == 0 && vect.y == 0) {
                break;
            }
            /* Smoothed velocity for the fling on release. */
            s_hc_fl_vx = s_hc_fl_vx * 0.5f + (float)vect.x * 0.5f;
            s_hc_fl_vy = s_hc_fl_vy * 0.5f + (float)vect.y * 0.5f;
            hc_pan_by(vect.x, vect.y);
            break;
        }

        case LV_EVENT_RELEASED:
        case LV_EVENT_PRESS_LOST:
            if ((fabsf(s_hc_fl_vx) > 4.0f || fabsf(s_hc_fl_vy) > 4.0f) &&
                s_hc_fling == NULL) {
                s_hc_fling = lv_timer_create(hc_fling_cb, 33, NULL);
            }
            break;

        default:
            break;
    }
}

static lv_obj_t *hc_icon_create(lv_obj_t *canvas, unsigned index,
                                int32_t cx, int32_t cy)
{
    lv_obj_t *icon = lv_obj_create(canvas);
    lv_obj_remove_style_all(icon);
    lv_obj_set_scrollable(icon, false);
    /* Not a button: the press must reach the panning surface below. */
    lv_obj_set_clickable(icon, false);
    lv_obj_set_size(icon, HC_ICON_MAX, HC_ICON_MAX);
    lv_obj_set_pos(icon, cx - HC_ICON_MAX / 2, cy - HC_ICON_MAX / 2);
    lv_obj_set_style_radius(icon, LV_PCT(50), LV_PART_MAIN);
    lv_obj_set_style_bg_color(icon, hc_colour(index), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *glyph = lv_label_create(icon);
    lv_label_set_text(glyph, hc_glyphs[index % ARRAY_LEN(hc_glyphs)]);
    lv_obj_set_clickable(glyph, false);
    lv_obj_set_style_text_color(glyph, WIN_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(glyph, WIN_FONT_TINY, LV_PART_MAIN);
    lv_obj_center(glyph);

    return icon;
}

static void hc_watch_create(lv_obj_t *parent)
{
    memset(&s_hc, 0, sizeof(s_hc));

    /* The watch must never grow a scrollbar. */
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);

    /* Metal case. No shadow: a blurred circle would be redrawn on every pan
     * step of the honeycomb. */
    lv_obj_t *case_obj = lv_obj_create(parent);
    lv_obj_set_size(case_obj, HC_CASE_SIZE, HC_CASE_SIZE);
    lv_obj_center(case_obj);
    lv_obj_set_style_radius(case_obj, LV_PCT(50), LV_PART_MAIN);
    lv_obj_set_style_bg_color(case_obj, lv_color_hex(0x9AA3B2), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(case_obj, lv_color_hex(0x3A404C),
                                   LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(case_obj, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_border_width(case_obj, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(case_obj, lv_color_hex(0xC9D2E0),
                                  LV_PART_MAIN);
    lv_obj_set_style_border_opa(case_obj, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_pad_all(case_obj, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(case_obj, false);
    win_style_decoration(case_obj);
    lv_obj_add_event_cb(case_obj, hc_delete_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *dial = lv_obj_create(case_obj);
    s_hc.dial = dial;
    lv_obj_set_size(dial, HC_DIAL_SIZE, HC_DIAL_SIZE);
    lv_obj_center(dial);
    lv_obj_set_style_radius(dial, LV_PCT(50), LV_PART_MAIN);
    lv_obj_set_style_clip_corner(dial, true, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dial, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dial, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(dial, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dial, 0, LV_PART_MAIN);
    win_style_decoration(dial);

    lv_obj_t *grid = lv_obj_create(dial);
    s_hc.grid = grid;
    lv_obj_set_size(grid, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(grid, 0, 0);
    win_style_flat(grid);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(grid, hc_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(grid, hc_press_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(grid, hc_press_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(grid, hc_press_cb, LV_EVENT_PRESS_LOST, NULL);

    lv_obj_t *canvas = lv_obj_create(grid);
    s_hc.canvas = canvas;
    lv_obj_set_size(canvas, HC_CANVAS_SIZE, HC_CANVAS_SIZE);
    lv_obj_set_pos(canvas, -HC_PAN, -HC_PAN);
    win_style_flat(canvas);
    win_style_decoration(canvas);

    /* Axial hex grid: q in [-3, 3], r in [-3, 3] with |q + r| <= 3. */
    unsigned index = 0;
    const int32_t canvas_cx = HC_CANVAS_SIZE / 2;
    const int32_t canvas_cy = HC_CANVAS_SIZE / 2;

    for (int32_t q = -HC_RADIUS; q <= HC_RADIUS; q++) {
        int32_t r0 = -q - HC_RADIUS;
        int32_t r1 = -q + HC_RADIUS;
        if (r0 < -HC_RADIUS) {
            r0 = -HC_RADIUS;
        }
        if (r1 > HC_RADIUS) {
            r1 = HC_RADIUS;
        }

        for (int32_t r = r0; r <= r1; r++) {
            const int32_t cx = canvas_cx + q * HC_DX;
            const int32_t cy = canvas_cy + (int32_t)((r + q / 2.0f) * HC_DY);

            hc_cell_t *cell = &s_hc.cells[index];
            cell->icon = hc_icon_create(canvas, index, cx, cy);
            cell->glyph = lv_obj_get_child(cell->icon, 0);
            cell->color = hc_colour(index);
            cell->cx = cx;
            cell->cy = cy;
            cell->size = 0;             /* force the first resize */
            cell->mix = 0;              /* force the first colour  */
            index++;
        }
    }

    hc_apply_fisheye();
}

/* ================================================================== */
/* Page 2: void menu (black hole galaxy)                              */
/* ================================================================== */

#define VD_COUNT        8
#define VD_PARTS        22              /* orbiting particle objects  */
#define VD_FIELD_STARS  34              /* static background stars     */
#define VD_TILT         0.42f           /* vertical squash of the ring */
#define VD_FRONT        1.5707963f      /* item 0 sits at the front    */
#define VD_STEP         (6.2831853f / (float)VD_COUNT)

#define VD_BG           lv_color_hex(0x020308)
#define VD_GOLD         lv_color_hex(0xF4B860)
#define VD_TEXT         lv_color_hex(0xF2F5F8)
#define VD_MUTED        lv_color_hex(0x8E98A6)

typedef struct {                        /* one orbiting particle       */
    lv_obj_t *obj;
    float angle;
    float radius;                       /* fraction of the orbit radius */
    float spin;                         /* rad/s                       */
    float inflow;                       /* radius/s                    */
    uint32_t tint;
    int32_t size;
    int32_t shade;                      /* cached colour step           */
} vd_part_t;

typedef struct {                        /* one selectable planet       */
    lv_obj_t *obj;
    int32_t size;
    int32_t key;                        /* cached style key             */
} vd_planet_t;

typedef struct {
    const char *name;
    const char *sub;
    uint32_t tint;
} vd_item_t;

typedef struct {
    lv_obj_t *field;                    /* static galaxy background     */
    lv_obj_t *hole;                     /* core, drawn above the planets */
    lv_obj_t *header;                   /* name of the selected planet  */
    lv_obj_t *subtitle;
    lv_obj_t *dots_box;
    lv_obj_t *dots[VD_COUNT];

    vd_planet_t planets[VD_COUNT];      /* the menu items on the ring  */
    vd_part_t parts[VD_PARTS];          /* the particle system         */

    int32_t w, h;                       /* page size                   */
    int32_t fw, fh;                     /* field size                  */
    int32_t cx, cy;                     /* black hole centre in field  */
    int32_t rdisk;                      /* orbit radius                */
    int32_t rcore;                      /* black hole core radius      */

    float pos;                          /* smooth ring position        */
    float vel;                          /* ring velocity, items/s      */
    int target;                         /* destination ring position   */
    int active;                         /* index of the focused item   */
    bool dragging;
    float drag_dist;                    /* finger travel, tap/roll test */
    float t;                            /* animation clock, seconds    */

    struct {                            /* static background stars     */
        int32_t x, y;
        int32_t size;
        uint32_t tint;
        lv_opa_t opa;
    } stars[VD_FIELD_STARS];
} vd_ctx_t;

static vd_ctx_t s_vd;

static const vd_item_t vd_items[VD_COUNT] = {
    {"planet_horizon", "planet_horizon_sub", 0xF4B860},
    {"planet_inner", "planet_inner_sub", 0xFFD27A},
    {"planet_ember", "planet_ember_sub", 0xFF7A2F},
    {"planet_glyph", "planet_glyph_sub", 0x8AA4FF},
    {"planet_moss", "planet_moss_sub", 0xFF5470},
    {"planet_void", "planet_void_sub", 0x62C6FF},
    {"planet_ember2", "planet_ember2_sub", 0x9BE7C4},
    {"planet_horizon2", "planet_horizon2_sub", 0xB18CFF},
};

/* ---- drawing ------------------------------------------------------ */

static void vd_circle(lv_layer_t *layer, int32_t x, int32_t y, int32_t r,
                      lv_color_t color, lv_opa_t opa)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = opa;
    dsc.radius = LV_RADIUS_CIRCLE;

    lv_area_t a;
    a.x1 = x - r;
    a.y1 = y - r;
    a.x2 = x + r;
    a.y2 = y + r;
    lv_draw_rect(layer, &dsc, &a);
}

static void vd_dot(lv_layer_t *layer, int32_t x, int32_t y, int32_t d,
                   lv_color_t color, lv_opa_t opa)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = opa;
    dsc.radius = (d > 2) ? 2 : 1;

    lv_area_t a;
    a.x1 = x;
    a.y1 = y;
    a.x2 = x + d - 1;
    a.y2 = y + d - 1;
    lv_draw_rect(layer, &dsc, &a);
}

/* The background is drawn once per layout: nebula, distant stars, the orbit
 * lane and the accretion glow. Everything that moves is a real LVGL object, so
 * the field never has to be invalidated per frame. */
static void vd_draw_bg(lv_event_t *event)
{
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t base;
    lv_obj_get_coords(lv_event_get_target(event), &base);

    const int32_t ox = base.x1;
    const int32_t oy = base.y1;
    const vd_ctx_t *vd = &s_vd;

    /* Nebula clouds behind the stars. */
    vd_circle(layer, ox + vd->cx - 18, oy + vd->cy - 10, 46,
              lv_color_hex(0x16205A), 26);
    vd_circle(layer, ox + vd->cx + 26, oy + vd->cy + 12, 38,
              lv_color_hex(0x3A1350), 22);
    vd_circle(layer, ox + vd->cx - 30, oy + vd->cy + 24, 26,
              lv_color_hex(0x0E3B55), 18);

    /* Distant stars. */
    for (unsigned i = 0; i < VD_FIELD_STARS; i++) {
        vd_dot(layer, ox + vd->stars[i].x, oy + vd->stars[i].y,
               vd->stars[i].size, lv_color_hex(vd->stars[i].tint),
               vd->stars[i].opa);
    }

    /* Dashed orbit lane. */
    const float step = 6.2831853f / 48.0f;
    for (unsigned i = 0; i < 48; i++) {
        const float a = VD_FRONT + (float)i * step;
        const int32_t x = vd->cx + (int32_t)(cosf(a) * (float)vd->rdisk);
        const int32_t y = vd->cy +
                          (int32_t)(sinf(a) * (float)vd->rdisk * VD_TILT);
        vd_dot(layer, ox + x, oy + y, 1, lv_color_hex(0xD8AC68), 95);
    }

    /* Accretion glow, drawn behind the planets. */
    vd_circle(layer, ox + vd->cx, oy + vd->cy, vd->rcore + 12,
              lv_color_hex(0x4A2A08), 30);
    vd_circle(layer, ox + vd->cx, oy + vd->cy, vd->rcore + 5,
              lv_color_hex(0x7A420C), 55);
}

/* The core is a separate object placed above the planets, so particles and
 * the far side of the ring disappear into it. */
static void vd_draw_hole(lv_event_t *event)
{
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_obj_t *obj = lv_event_get_target(event);
    lv_area_t base;
    lv_obj_get_coords(obj, &base);

    const int32_t cx = base.x1 + lv_obj_get_width(obj) / 2;
    const int32_t cy = base.y1 + lv_obj_get_height(obj) / 2;
    const int32_t r = s_vd.rcore;

    vd_circle(layer, cx, cy, r + 2, VD_GOLD, 70);
    vd_circle(layer, cx, cy, r, lv_color_hex(0x1A0F04), 255);
    vd_circle(layer, cx, cy, r - 3, lv_color_black(), 255);
}

/* ---- layout ------------------------------------------------------- */

static void vd_apply_item(int index)
{
    const vd_item_t *it = &vd_items[index];

    lv_label_set_text(s_vd.header, _(it->name));
    lv_label_set_text(s_vd.subtitle, _(it->sub));

    for (unsigned i = 0; i < VD_COUNT; i++) {
        const bool on = (i == (unsigned)index);
        lv_obj_set_size(s_vd.dots[i], on ? 6 : 4, 4);
        lv_obj_set_style_bg_color(s_vd.dots[i],
                                  on ? lv_color_hex(it->tint)
                                     : lv_color_hex(0x3A3F4A),
                                  LV_PART_MAIN);
    }
}

/* ---- planets and particles ---------------------------------------- */

static void vd_spawn_part(vd_part_t *p, bool initial)
{
    p->radius = 1.0f + ui_rand01() * 0.35f;
    p->angle = ui_rand01() * 6.2831853f;
    p->spin = 0.55f + ui_rand01() * 1.05f;
    p->inflow = 0.05f + ui_rand01() * 0.09f;
    p->tint = (ui_rand01() < 0.55f) ? 0xFFE7B0u : 0xBFD8FFu;
    p->size = 0;
    p->shade = -1;

    if (initial) {
        /* Spread the first generation along the spiral. */
        p->radius = 0.35f + ui_rand01() * 0.95f;
    }
}

static void vd_planet_style(vd_planet_t *pl, int index, float dist)
{
    const bool on = (index == s_vd.active);

    int32_t size = (on ? 20 : 16) - (int32_t)(dist * 1.5f);
    if (size < 9) {
        size = 9;
    }
    const float fade = on ? 0.0f : (0.18f + 0.14f * dist);
    const int32_t key = (on ? 1 : 0) | ((int32_t)(fade * 8.0f) << 1);
    if (key == pl->key && size == pl->size) {
        return;                         /* already styled               */
    }
    pl->key = key;
    pl->size = size;

    lv_obj_set_size(pl->obj, size, size);
    lv_obj_set_style_radius(pl->obj, LV_PCT(50), LV_PART_MAIN);
    lv_obj_set_style_bg_color(pl->obj,
                              ui_fade(lv_color_hex(vd_items[index].tint),
                                      VD_BG, fade),
                              LV_PART_MAIN);
    lv_obj_set_style_border_width(pl->obj, on ? 2 : 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(pl->obj,
                                  on ? VD_GOLD
                                     : ui_fade(lv_color_hex(vd_items[index].tint),
                                               lv_color_black(), 0.50f),
                                  LV_PART_MAIN);
}

static void vd_place_planets(void)
{
    if (s_vd.rdisk <= 0) {
        return;
    }

    for (unsigned i = 0; i < VD_COUNT; i++) {
        vd_planet_t *pl = &s_vd.planets[i];
        const float rel = ui_ring_delta((float)i - s_vd.pos, VD_COUNT);
        const float dist = fabsf(rel);

        /* The farthest planet is hidden behind the hole. */
        if (dist > 3.2f) {
            lv_obj_set_hidden(pl->obj, true);
            continue;
        }
        lv_obj_set_hidden(pl->obj, false);

        vd_planet_style(pl, (int)i, dist);

        const float a = VD_FRONT + rel * VD_STEP;
        const int32_t x = s_vd.cx +
                          (int32_t)(cosf(a) * (float)s_vd.rdisk);
        const int32_t y = s_vd.cy +
                          (int32_t)(sinf(a) * (float)s_vd.rdisk * VD_TILT);
        lv_obj_set_pos(pl->obj, x - pl->size / 2, y - pl->size / 2);
    }
}

static void vd_place_parts(void)
{
    if (s_vd.rdisk <= 0) {
        return;
    }

    for (unsigned i = 0; i < VD_PARTS; i++) {
        vd_part_t *p = &s_vd.parts[i];
        float r = p->radius;
        if (r < 0.30f) {
            r = 0.30f;
        }

        int32_t size = 3 + (int32_t)(r * 3.0f);
        if (size > 6) {
            size = 6;
        }

        const int32_t x = s_vd.cx +
                          (int32_t)(cosf(p->angle) * r * (float)s_vd.rdisk);
        const int32_t y = s_vd.cy +
                          (int32_t)(sinf(p->angle) * r * (float)s_vd.rdisk *
                                    VD_TILT);

        if (size != p->size) {
            p->size = size;
            lv_obj_set_size(p->obj, size, size);
        }
        lv_obj_set_pos(p->obj, x - size / 2, y - size / 2);

        /* The closer to the hole, the darker the particle gets. */
        float lit = 1.0f - r;
        if (lit < 0.0f) {
            lit = 0.0f;
        }
        if (lit > 1.0f) {
            lit = 1.0f;
        }
        const int32_t shade = (int32_t)(lit * 6.0f);
        if (shade != p->shade) {
            p->shade = shade;
            lv_obj_set_style_bg_color(p->obj,
                                      ui_fade(lv_color_hex(p->tint), VD_BG,
                                              0.12f + 0.72f * (float)shade / 6.0f),
                                      LV_PART_MAIN);
        }
    }
}

static void vd_layout(void)
{
    if (s_vd.field == NULL) {
        return;
    }

    const int32_t w = lv_obj_get_width(lv_obj_get_parent(s_vd.field));
    const int32_t h = lv_obj_get_height(lv_obj_get_parent(s_vd.field));
    if (w <= 0 || h <= 0) {
        return;
    }
    s_vd.w = w;
    s_vd.h = h;

    if (s_vd.fw == w && s_vd.fh == h && s_vd.rdisk > 0) {
        return;                         /* nothing changed              */
    }
    s_vd.fw = w;
    s_vd.fh = h;

    lv_obj_set_pos(s_vd.field, 0, 0);
    lv_obj_set_size(s_vd.field, w, h);

    s_vd.cx = w / 2;
    s_vd.cy = h / 2 + (h / 14);

    int32_t min = (w < h) ? w : h;
    s_vd.rdisk = (min * 42) / 100;
    s_vd.rcore = s_vd.rdisk / 4;
    if (s_vd.rcore < 9) {
        s_vd.rcore = 9;
    }

    const int32_t hs = 2 * s_vd.rcore + 6;
    lv_obj_set_size(s_vd.hole, hs, hs);
    lv_obj_set_pos(s_vd.hole, s_vd.cx - hs / 2, s_vd.cy - hs / 2);

    /* Header: the selected planet is named on top, like the coverflow. */
    lv_obj_set_pos(s_vd.header, 0, 3);
    lv_obj_set_size(s_vd.header, w, 16);
    lv_obj_set_pos(s_vd.subtitle, 0, 19);
    lv_obj_set_size(s_vd.subtitle, w, 12);

    const int32_t dots_w = (int32_t)VD_COUNT * 7;
    lv_obj_set_pos(s_vd.dots_box, s_vd.cx - dots_w / 2, h - 14);
    lv_obj_set_size(s_vd.dots_box, dots_w, 8);
    for (unsigned i = 0; i < VD_COUNT; i++) {
        lv_obj_set_pos(s_vd.dots[i], (int32_t)i * 7 + 2, 2);
    }

    /* Background stars are laid out in field coordinates. */
    for (unsigned i = 0; i < VD_FIELD_STARS; i++) {
        s_vd.stars[i].x = (int32_t)(ui_rand01() * (float)(s_vd.fw - 2));
        s_vd.stars[i].y = (int32_t)(ui_rand01() * (float)(s_vd.fh - 2));
        s_vd.stars[i].size = (ui_rand01() < 0.7f) ? 1 : 2;
        s_vd.stars[i].tint = (ui_rand01() < 0.7f) ? 0xDCE6F4u : 0xFFE7B0u;
        s_vd.stars[i].opa = (lv_opa_t)(70 + 120 * ui_rand01());
    }

    vd_place_planets();
    vd_place_parts();
    lv_obj_invalidate(s_vd.field);
    lv_obj_invalidate(s_vd.hole);
}

/* ---- physics and widgets ------------------------------------------ */

static void vd_tick(lv_timer_t *timer)
{
    (void)timer;

    if (s_vd.field == NULL) {
        vd_stop();
        return;
    }

    const float dt = 0.033f;
    s_vd.t += dt;

    /* Particles spiral into the hole, then respawn at the rim. */
    for (unsigned i = 0; i < VD_PARTS; i++) {
        vd_part_t *p = &s_vd.parts[i];
        p->angle += p->spin * dt;
        p->radius -= p->inflow * dt;
        if (p->radius < 0.30f) {
            vd_spawn_part(p, false);
        }
    }
    vd_place_parts();

    /* The ring springs to the selected planet. */
    if (!s_vd.dragging) {
        const float err = ui_ring_delta((float)s_vd.target - s_vd.pos,
                                        VD_COUNT);
        s_vd.vel += err * 110.0f * dt;
        s_vd.vel *= 0.52f;
        s_vd.pos += s_vd.vel * dt;

        if (fabsf(s_vd.vel) < 0.04f && fabsf(err) < 0.004f) {
            s_vd.vel = 0.0f;
            s_vd.pos = (float)s_vd.target;
        }
    }

    vd_place_planets();

    const int active = ((int)lroundf(s_vd.pos) % VD_COUNT + VD_COUNT) % VD_COUNT;
    if (active != s_vd.active) {
        s_vd.active = active;
        vd_apply_item(active);
        vd_place_planets();             /* restyle the new active planet */
    }
}

static void vd_press_cb(lv_event_t *event)
{
    switch (lv_event_get_code(event)) {
        case LV_EVENT_PRESSED:
            s_vd.dragging = true;
            s_vd.vel = 0.0f;
            s_vd.drag_dist = 0.0f;
            break;

        case LV_EVENT_PRESSING: {
            lv_indev_t *indev = lv_indev_active();
            if (indev == NULL) {
                break;
            }
            lv_point_t vect;
            lv_indev_get_vect(indev, &vect);
            if (vect.x == 0 && vect.y == 0) {
                break;
            }

            s_vd.drag_dist += (float)((vect.x < 0 ? -vect.x : vect.x) +
                                      (vect.y < 0 ? -vect.y : vect.y));

            /* Drag right rolls the ring right: 240 px sweep one item. */
            const float gain = 1.0f / 240.0f;
            s_vd.pos += (float)vect.x * gain;
            s_vd.vel = s_vd.vel * 0.6f +
                       ((float)vect.x * gain / 0.033f) * 0.4f;
            vd_place_planets();         /* follow the finger at once    */
            break;
        }

        case LV_EVENT_RELEASED:
        case LV_EVENT_PRESS_LOST: {
            s_vd.dragging = false;

            float dest;
            if (s_vd.vel > 1.0f) {
                dest = floorf(s_vd.pos) + 1.0f;
            } else if (s_vd.vel < -1.0f) {
                dest = ceilf(s_vd.pos) - 1.0f;
            } else {
                dest = lroundf(s_vd.pos);
            }
            s_vd.target = (int)dest;
            break;
        }

        default:
            break;
    }
}

/* Tapping a planet rolls the ring until that planet is at the front. */
static void vd_planet_cb(lv_event_t *event)
{
    if (s_vd.drag_dist > 10.0f) {
        return;                         /* it was a roll, not a tap     */
    }

    const int index = (int)(intptr_t)lv_event_get_user_data(event);
    if (index < 0 || index >= VD_COUNT || index == s_vd.active) {
        return;
    }

    const float dest = s_vd.pos +
                       ui_ring_delta((float)index - s_vd.pos, VD_COUNT);
    s_vd.target = (int)lroundf(dest);
    s_vd.vel = 0.0f;
}

static lv_obj_t *vd_planet_create(lv_obj_t *page, int index)
{
    lv_obj_t *obj = lv_obj_create(page);
    lv_obj_remove_style_all(obj);
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_clickable(obj, true);
    lv_obj_set_size(obj, 16, 16);
    lv_obj_set_style_radius(obj, LV_PCT(50), LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_hex(vd_items[index].tint),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj,
                                  ui_fade(lv_color_hex(vd_items[index].tint),
                                          lv_color_black(), 0.50f),
                                  LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);

    /* Presses bubble to the page so a drag that starts on a planet still
     * rolls the ring. */
    lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(obj, vd_planet_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)index);
    return obj;
}

static lv_obj_t *vd_label(lv_obj_t *parent, const lv_font_t *font,
                          lv_color_t color, lv_text_align_t align)
{
    lv_obj_t *lab = lv_label_create(parent);
    lv_obj_set_clickable(lab, false);
    lv_obj_set_style_text_font(lab, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lab, color, LV_PART_MAIN);
    lv_obj_set_style_text_align(lab, align, LV_PART_MAIN);
    lv_obj_set_style_pad_all(lab, 0, LV_PART_MAIN);
    return lab;
}

static void vd_create(lv_obj_t *page)
{
    memset(&s_vd, 0, sizeof(s_vd));
    s_rng = 0x12345678u;

    lv_obj_set_style_bg_color(page, VD_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(page, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(page, false);
    lv_obj_add_flag(page, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(page, vd_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(page, vd_press_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(page, vd_press_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(page, vd_press_cb, LV_EVENT_PRESS_LOST, NULL);

    /* The field that draws the galaxy. */
    s_vd.field = lv_obj_create(page);
    lv_obj_remove_style_all(s_vd.field);
    lv_obj_set_style_bg_color(s_vd.field, VD_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_vd.field, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(s_vd.field, false);
    lv_obj_set_clickable(s_vd.field, false);
    lv_obj_add_event_cb(s_vd.field, vd_draw_bg, LV_EVENT_DRAW_MAIN, NULL);

    /* The particle system: each particle is one small LVGL object. */
    for (unsigned i = 0; i < VD_PARTS; i++) {
        lv_obj_t *dot = lv_obj_create(page);
        lv_obj_remove_style_all(dot);
        lv_obj_set_scrollable(dot, false);
        lv_obj_set_clickable(dot, false);
        lv_obj_set_size(dot, 2, 2);
        lv_obj_set_style_radius(dot, LV_PCT(50), LV_PART_MAIN);
        lv_obj_set_style_bg_color(dot, VD_GOLD, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);

        vd_spawn_part(&s_vd.parts[i], true);
        s_vd.parts[i].obj = dot;
    }

    /* The planets: the selectable menu items. */
    for (unsigned i = 0; i < VD_COUNT; i++) {
        s_vd.planets[i].obj = vd_planet_create(page, (int)i);
        s_vd.planets[i].key = -1;
        s_vd.planets[i].size = 0;
    }

    /* The core sits above the ring so the far planets vanish into it. */
    s_vd.hole = lv_obj_create(page);
    lv_obj_remove_style_all(s_vd.hole);
    lv_obj_set_scrollable(s_vd.hole, false);
    lv_obj_set_clickable(s_vd.hole, false);
    lv_obj_add_event_cb(s_vd.hole, vd_draw_hole, LV_EVENT_DRAW_MAIN, NULL);

    /* Header: the planet the user is on, like the coverflow tab. */
    s_vd.header = vd_label(page, WIN_FONT_SMALL, VD_TEXT, LV_TEXT_ALIGN_CENTER);
    s_vd.subtitle = vd_label(page, WIN_FONT_TINY, VD_MUTED,
                             LV_TEXT_ALIGN_CENTER);

    s_vd.dots_box = lv_obj_create(page);
    lv_obj_remove_style_all(s_vd.dots_box);
    lv_obj_set_scrollable(s_vd.dots_box, false);
    lv_obj_set_clickable(s_vd.dots_box, false);
    for (unsigned i = 0; i < VD_COUNT; i++) {
        lv_obj_t *dot = lv_obj_create(s_vd.dots_box);
        lv_obj_remove_style_all(dot);
        lv_obj_set_scrollable(dot, false);
        lv_obj_set_clickable(dot, false);
        lv_obj_set_size(dot, 4, 4);
        lv_obj_set_style_radius(dot, LV_PCT(50), LV_PART_MAIN);
        lv_obj_set_style_bg_color(dot, lv_color_hex(0x3A3F4A), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
        s_vd.dots[i] = dot;
    }

    s_vd.target = 0;
    s_vd.active = 0;
    s_vd.pos = 0.0f;
    s_vd.vel = 0.0f;
    s_vd.t = 0.0f;
    vd_apply_item(0);

    lv_obj_add_event_cb(page, vd_size_cb, LV_EVENT_SIZE_CHANGED, NULL);
    vd_layout();
}

static void vd_size_cb(lv_event_t *event)
{
    (void)event;
    vd_layout();
}

static void vd_start(void)
{
    if (s_vd_tmr == NULL && s_vd.field != NULL) {
        s_vd_tmr = lv_timer_create(vd_tick, 33, NULL);
    }
}

static void vd_stop(void)
{
    if (s_vd_tmr != NULL) {
        lv_timer_delete(s_vd_tmr);
        s_vd_tmr = NULL;
    }
}

/* ================================================================== */
/* Page 3: silk menu (coverflow)                                      */
/* ================================================================== */

#define SK_COUNT        8
#define SK_DUST         26
#define SK_W0           WIN_PX(88)
#define SK_H0           WIN_PX(124)
#define SK_R            215             /* horizontal arc radius        */
#define SK_RV           62              /* vertical arc radius          */
#define SK_STEP         0.44f           /* arc between two cards        */
#define SK_TILT         420.0f          /* rad -> 0.1 deg, fanned cards */

#define SK_BG           lv_color_hex(0x050506)
#define SK_ORANGE       lv_color_hex(0xFF7A18)
#define SK_ORANGE_DARK  lv_color_hex(0xFF3D00)
#define SK_TEXT         lv_color_hex(0xF2F2F2)
#define SK_MUTED        lv_color_hex(0x8A8F98)
#define SK_CARD         lv_color_hex(0x3A3D44)

typedef struct {
    lv_obj_t *card;
    lv_obj_t *glyph;
    lv_obj_t *name;
    lv_obj_t *num;
    int32_t w, h;
    int32_t x, y;
    int32_t rot;                        /* cache for the fan angle      */
    int style_state;                    /* cache for colour updates     */
} sk_card_t;

typedef struct {
    lv_obj_t *field;
    lv_obj_t *header;
    lv_obj_t *subtitle;
    lv_obj_t *dots[SK_COUNT];
    lv_obj_t *dots_box;

    sk_card_t cards[SK_COUNT];

    int32_t w, h;
    int32_t cx;
    int32_t cy;
    float pos;
    float vel;
    int target;
    int active;
    bool dragging;
    float drag_dist;

    struct {
        float x, y, z;
    } dust[SK_DUST];
} sk_ctx_t;

static sk_ctx_t s_sk;

static const struct {
    const char *name;
    const char *sub;
    const char *glyph;
    uint32_t tint;
} sk_items[SK_COUNT] = {
    {"item_music", "item_music_sub", LV_SYMBOL_AUDIO, 0xFF7A18},
    {"item_photos", "item_photos_sub", LV_SYMBOL_IMAGE, 0x5A6470},
    {"item_chat", "item_chat_sub", LV_SYMBOL_ENVELOPE, 0x5A6470},
    {"item_home", "item_home_sub", LV_SYMBOL_HOME, 0x5A6470},
    {"item_map", "item_map_sub", LV_SYMBOL_GPS, 0x5A6470},
    {"item_store", "item_store_sub", LV_SYMBOL_DRIVE, 0x5A6470},
    {"item_battery", "item_battery_sub", LV_SYMBOL_CHARGE, 0x5A6470},
    {"item_settings", "item_settings_sub", LV_SYMBOL_SETTINGS, 0x5A6470},
};

/* ---- drawing ------------------------------------------------------ */

static void sk_draw_cb(lv_event_t *event)
{
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t base;
    lv_obj_get_coords(lv_event_get_target(event), &base);

    const int32_t ox = base.x1;
    const int32_t oy = base.y1;
    const sk_ctx_t *sk = &s_sk;

    /* Static dust field: it never moves or twinkles, so the page below the
     * cards only has to be redrawn when the layout changes. */
    for (unsigned i = 0; i < SK_DUST; i++) {
        lv_draw_rect_dsc_t dsc;
        lv_draw_rect_dsc_init(&dsc);
        dsc.bg_color = lv_color_hex(0xAAB4C8);
        dsc.bg_opa = (lv_opa_t)(34 + 46 * sk->dust[i].z);
        dsc.radius = LV_RADIUS_CIRCLE;

        const int32_t d = (sk->dust[i].z > 0.75f) ? 2 : 1;
        lv_area_t a;
        a.x1 = ox + (int32_t)sk->dust[i].x;
        a.y1 = oy + (int32_t)sk->dust[i].y;
        a.x2 = a.x1 + d;
        a.y2 = a.y1 + d;
        lv_draw_rect(layer, &dsc, &a);
    }
}

/* ---- layout ------------------------------------------------------- */

static void sk_apply_style(sk_card_t *c, float dist)
{
    const bool center = fabsf(dist) < 0.35f;

    /* 3 states: front, near, far. The labels are only restyled when the
     * state changes, so a moving card does not touch the label engine. */
    const int state = center ? 0 : (dist < 1.5f ? 1 : 2);
    if (state == c->style_state) {
        return;
    }
    c->style_state = state;

    if (state == 0) {
        lv_obj_set_style_bg_color(c->card, SK_ORANGE, LV_PART_MAIN);
        lv_obj_set_style_bg_grad_color(c->card, SK_ORANGE_DARK, LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(c->card, LV_GRAD_DIR_VER, LV_PART_MAIN);
        lv_obj_set_style_border_color(c->card, lv_color_hex(0xFFB37A),
                                      LV_PART_MAIN);
        lv_obj_set_style_text_color(c->name, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_color(c->glyph, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_color(c->num, lv_color_hex(0x4A1B08),
                                    LV_PART_MAIN);
        lv_obj_set_style_text_font(c->name, WIN_FONT_SMALL, LV_PART_MAIN);
        lv_obj_set_style_text_font(c->glyph, WIN_FONT_LARGE, LV_PART_MAIN);
    } else {
        /* The farther the card, the more its colours are pulled towards
         * the black background. */
        const float fade = (state == 1) ? 0.35f : 0.62f;
        lv_obj_set_style_bg_color(c->card,
                                  ui_fade(SK_CARD, SK_BG, fade), LV_PART_MAIN);
        lv_obj_set_style_bg_grad_color(c->card,
                                       ui_fade(SK_CARD, SK_BG, fade + 0.12f),
                                       LV_PART_MAIN);
        lv_obj_set_style_border_color(c->card,
                                      ui_fade(lv_color_hex(0x555A64), SK_BG,
                                              fade),
                                      LV_PART_MAIN);
        lv_obj_set_style_text_color(c->name,
                                    ui_fade(SK_TEXT, SK_BG, fade + 0.15f),
                                    LV_PART_MAIN);
        lv_obj_set_style_text_color(c->glyph,
                                    ui_fade(SK_TEXT, SK_BG, fade + 0.25f),
                                    LV_PART_MAIN);
        lv_obj_set_style_text_color(c->num,
                                    ui_fade(SK_MUTED, SK_BG, fade + 0.2f),
                                    LV_PART_MAIN);
        lv_obj_set_style_text_font(c->name, WIN_FONT_TINY, LV_PART_MAIN);
        lv_obj_set_style_text_font(c->glyph, WIN_FONT_SMALL, LV_PART_MAIN);
    }
}

static void sk_layout(void)
{
    if (s_sk.field == NULL) {
        return;
    }

    const int32_t w = lv_obj_get_width(lv_obj_get_parent(s_sk.field));
    const int32_t h = lv_obj_get_height(lv_obj_get_parent(s_sk.field));
    if (w <= 0 || h <= 0) {
        return;
    }

    if (w == s_sk.w && h == s_sk.h && s_sk.cy > 0) {
        return;
    }
    s_sk.w = w;
    s_sk.h = h;

    lv_obj_set_pos(s_sk.field, 0, 0);
    lv_obj_set_size(s_sk.field, w, h);

    s_sk.cx = w / 2;
    s_sk.cy = h / 2 + (h / 12);

    lv_obj_set_pos(s_sk.header, 0, 3);
    lv_obj_set_size(s_sk.header, w, 16);
    lv_obj_set_pos(s_sk.subtitle, 0, 18);
    lv_obj_set_size(s_sk.subtitle, w, 12);

    lv_obj_set_pos(s_sk.dots_box, s_sk.cx - 30, h - 14);
    lv_obj_set_size(s_sk.dots_box, 60, 8);
    for (unsigned i = 0; i < SK_COUNT; i++) {
        lv_obj_set_pos(s_sk.dots[i], (int32_t)i * 7 + 2, 2);
        lv_obj_set_size(s_sk.dots[i], 4, 4);
    }

    for (unsigned i = 0; i < SK_DUST; i++) {
        s_sk.dust[i].x = ui_rand01() * (float)w;
        s_sk.dust[i].y = 10.0f + ui_rand01() * (float)(h - 20);
        s_sk.dust[i].z = 0.2f + ui_rand01() * 0.8f;
    }

    lv_obj_invalidate(s_sk.field);
}

static void sk_place_cards(void)
{
    for (unsigned i = 0; i < SK_COUNT; i++) {
        sk_card_t *c = &s_sk.cards[i];

        /* Wrap the ring so the first card is centred with cards on both
         * sides, and so scrolling never runs off the ends. */
        const float rel = ui_ring_delta((float)i - s_sk.pos, SK_COUNT);
        const float dist = fabsf(rel);

        float scale = 1.0f - 0.26f * dist;
        if (scale < 0.40f) {
            scale = 0.40f;
        }
        /* Only the middle and the first pair on each side stay on screen. */
        if (dist > 2.15f) {
            scale = 0.0f;
        }

        const int32_t cw = (int32_t)(SK_W0 * scale);
        const int32_t ch = (int32_t)(SK_H0 * scale);
        if (cw <= 0 || ch <= 0) {
            lv_obj_set_hidden(c->card, true);
            continue;
        }
        lv_obj_set_hidden(c->card, false);

        if (cw != c->w || ch != c->h) {
            c->w = cw;
            c->h = ch;
            lv_obj_set_size(c->card, cw, ch);
            lv_obj_set_style_radius(c->card, cw / 9, LV_PART_MAIN);
            /* Keep the text inside the shrinking card. */
            lv_obj_set_width(c->name, cw - 4);
            lv_obj_set_pos(c->glyph, cw / 2 - 6, ch / 3 - 2);
            lv_obj_set_pos(c->name, 2, ch - 16);
            lv_obj_set_pos(c->num, 3, 3);
        }

        const float arc = rel * SK_STEP;
        const int32_t x = s_sk.cx + (int32_t)(sinf(arc) * (float)SK_R) - cw / 2;
        const int32_t y = s_sk.cy +
                          (int32_t)((1.0f - cosf(arc)) * (float)SK_RV) - ch / 2;
        if (x != c->x || y != c->y) {
            c->x = x;
            c->y = y;
            lv_obj_set_pos(c->card, x, y);
        }

        /* Fan the side cards outwards a little. The front card stays flat,
         * so it never needs a transform layer. */
        const int32_t rot = (int32_t)(arc * SK_TILT);
        if (rot != c->rot) {
            c->rot = rot;
            lv_obj_set_style_transform_rotation(c->card, rot, LV_PART_MAIN);
        }

        sk_apply_style(c, dist);
    }
}

/* ---- physics and widgets ------------------------------------------ */

static void sk_tick(lv_timer_t *timer)
{
    (void)timer;

    if (s_sk.field == NULL) {
        sk_stop();
        return;
    }

    const float dt = 0.033f;

    bool moving = s_sk.dragging;

    if (!s_sk.dragging) {
        const float err = ui_ring_delta((float)s_sk.target - s_sk.pos,
                                        SK_COUNT);
        s_sk.vel += err * 110.0f * dt;
        s_sk.vel *= 0.52f;
        s_sk.pos += s_sk.vel * dt;

        if (fabsf(s_sk.vel) < 0.03f && fabsf(err) < 0.004f) {
            s_sk.vel = 0.0f;
            s_sk.pos = (float)s_sk.target;
        } else {
            moving = true;
        }
    }

    if (moving) {
        sk_place_cards();
    }

    const int active = ((int)lroundf(s_sk.pos) % SK_COUNT + SK_COUNT) % SK_COUNT;
    if (active != s_sk.active) {
        s_sk.active = active;
        lv_label_set_text(s_sk.header, _(sk_items[active].name));
        lv_label_set_text(s_sk.subtitle, _(sk_items[active].sub));
        lv_label_set_text(s_sk.cards[active].name, _(sk_items[active].name));
        lv_obj_move_foreground(s_sk.cards[active].card);
        for (unsigned i = 0; i < SK_COUNT; i++) {
            const bool on = (i == (unsigned)active);
            lv_obj_set_size(s_sk.dots[i], on ? 6 : 4, 4);
            lv_obj_set_style_bg_color(s_sk.dots[i],
                                      on ? SK_ORANGE : lv_color_hex(0x3A3F4A),
                                      LV_PART_MAIN);
        }
    }
}

static void sk_press_cb(lv_event_t *event)
{
    switch (lv_event_get_code(event)) {
        case LV_EVENT_PRESSED:
            s_sk.dragging = true;
            s_sk.drag_dist = 0.0f;
            s_sk.vel = 0.0f;
            break;

        case LV_EVENT_PRESSING: {
            lv_indev_t *indev = lv_indev_active();
            if (indev == NULL) {
                break;
            }
            lv_point_t vect;
            lv_indev_get_vect(indev, &vect);
            if (vect.x == 0 && vect.y == 0) {
                break;
            }

            s_sk.drag_dist += fabsf((float)vect.x);

            /* One card pitch of travel moves the coverflow by one card.
             * The cards follow the finger immediately instead of waiting
             * for the next timer tick. */
            const float gain = 1.0f / 55.0f;
            s_sk.pos -= (float)vect.x * gain;
            s_sk.vel = s_sk.vel * 0.6f -
                       ((float)vect.x * gain / 0.033f) * 0.4f;
            sk_place_cards();
            break;
        }

        case LV_EVENT_RELEASED:
        case LV_EVENT_PRESS_LOST: {
            s_sk.dragging = false;

            /* Always snap in the direction the finger was moving. The
             * target is at most one card away, so it can never jump
             * backwards or skip several cards. */
            float dest;
            if (s_sk.vel < -1.0f) {
                dest = ceilf(s_sk.pos) - 1.0f;
            } else if (s_sk.vel > 1.0f) {
                dest = floorf(s_sk.pos) + 1.0f;
            } else {
                dest = lroundf(s_sk.pos);
            }
            s_sk.target = (int)dest;
            break;
        }

        default:
            break;
    }
}

static void sk_card_cb(lv_event_t *event)
{
    sk_card_t *c = (sk_card_t *)lv_event_get_user_data(event);

    /* A press that turned into a drag must not also select the card. */
    if (s_sk.drag_dist > 10.0f) {
        return;
    }

    /* Tapping a side card brings it to the front, by the short way round. */
    for (unsigned i = 0; i < SK_COUNT; i++) {
        if (&s_sk.cards[i] == c && (int)i != s_sk.active) {
            s_sk.target = (int)lroundf(s_sk.pos +
                                       ui_ring_delta((float)i - s_sk.pos,
                                                     SK_COUNT));
            s_sk.vel = 0.0f;
            break;
        }
    }
}

static lv_obj_t *sk_card_create(lv_obj_t *page, unsigned index)
{
    sk_card_t *c = &s_sk.cards[index];

    c->card = lv_obj_create(page);
    lv_obj_remove_style_all(c->card);
    lv_obj_set_scrollbar_mode(c->card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(c->card, SK_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(c->card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(c->card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(c->card, lv_color_hex(0x555A64),
                                  LV_PART_MAIN);
    lv_obj_set_style_pad_all(c->card, 0, LV_PART_MAIN);
    /* Rotate around the middle of the card. */
    lv_obj_set_style_transform_pivot_x(c->card, LV_PCT(50), LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(c->card, LV_PCT(50), LV_PART_MAIN);
    /* A drag that starts on a card still scrolls the coverflow. */
    lv_obj_add_flag(c->card, LV_OBJ_FLAG_EVENT_BUBBLE);
    c->rot = 0;
    c->style_state = -1;

    c->num = lv_label_create(c->card);
    lv_obj_set_clickable(c->num, false);
    lv_obj_set_style_text_font(c->num, WIN_FONT_TINY, LV_PART_MAIN);
    lv_obj_set_style_text_color(c->num, SK_MUTED, LV_PART_MAIN);
    lv_label_set_text_fmt(c->num, "%02u", index + 1);

    c->glyph = lv_label_create(c->card);
    lv_obj_set_clickable(c->glyph, false);
    lv_obj_set_style_text_font(c->glyph, WIN_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(c->glyph, SK_TEXT, LV_PART_MAIN);
    lv_label_set_text(c->glyph, sk_items[index].glyph);

    c->name = lv_label_create(c->card);
    lv_obj_set_clickable(c->name, false);
    lv_obj_set_style_text_font(c->name, WIN_FONT_TINY, LV_PART_MAIN);
    lv_obj_set_style_text_color(c->name, SK_TEXT, LV_PART_MAIN);
    lv_obj_set_style_pad_all(c->name, 0, LV_PART_MAIN);
    lv_obj_set_style_text_align(c->name, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(c->name, _(sk_items[index].name));
    lv_obj_set_style_clip_corner(c->card, true, LV_PART_MAIN);

    lv_obj_add_event_cb(c->card, sk_card_cb, LV_EVENT_CLICKED, c);

    return c->card;
}

static void sk_create(lv_obj_t *page)
{
    memset(&s_sk, 0, sizeof(s_sk));
    s_rng = 0x0BADCAFEu;

    lv_obj_set_style_bg_color(page, SK_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(page, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(page, false);
    lv_obj_add_flag(page, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(page, sk_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(page, sk_press_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(page, sk_press_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(page, sk_press_cb, LV_EVENT_PRESS_LOST, NULL);

    s_sk.field = lv_obj_create(page);
    lv_obj_remove_style_all(s_sk.field);
    lv_obj_set_style_bg_color(s_sk.field, SK_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_sk.field, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(s_sk.field, false);
    lv_obj_set_clickable(s_sk.field, false);
    lv_obj_add_event_cb(s_sk.field, sk_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    s_sk.header = lv_label_create(page);
    lv_obj_set_clickable(s_sk.header, false);
    lv_obj_set_style_text_align(s_sk.header, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(s_sk.header, SK_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_sk.header, WIN_FONT_SMALL, LV_PART_MAIN);
    lv_label_set_text(s_sk.header, _(sk_items[0].name));

    s_sk.subtitle = lv_label_create(page);
    lv_obj_set_clickable(s_sk.subtitle, false);
    lv_obj_set_style_text_align(s_sk.subtitle, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_obj_set_style_text_color(s_sk.subtitle, SK_MUTED, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_sk.subtitle, WIN_FONT_TINY, LV_PART_MAIN);
    lv_label_set_text(s_sk.subtitle, _(sk_items[0].sub));

    /* Cards are created from the middle outwards so the front card ends up
     * at the top of the z order on the first frame. */
    for (unsigned i = 0; i < SK_COUNT; i++) {
        sk_card_create(page, i);
        if (i == 0) {
            lv_obj_move_foreground(s_sk.cards[i].card);
        }
    }

    s_sk.dots_box = lv_obj_create(page);
    lv_obj_remove_style_all(s_sk.dots_box);
    lv_obj_set_scrollable(s_sk.dots_box, false);
    lv_obj_set_clickable(s_sk.dots_box, false);
    for (unsigned i = 0; i < SK_COUNT; i++) {
        lv_obj_t *dot = lv_obj_create(s_sk.dots_box);
        lv_obj_remove_style_all(dot);
        lv_obj_set_clickable(dot, false);
        lv_obj_set_size(dot, 4, 4);
        lv_obj_set_style_radius(dot, LV_PCT(50), LV_PART_MAIN);
        lv_obj_set_style_bg_color(dot, lv_color_hex(0x3A3F4A), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
        s_sk.dots[i] = dot;
    }

    s_sk.target = 0;
    s_sk.active = 0;
    s_sk.pos = 0.0f;
    s_sk.vel = 0.0f;
    s_sk.drag_dist = 0.0f;

    lv_obj_add_event_cb(page, sk_size_cb, LV_EVENT_SIZE_CHANGED, NULL);
    sk_layout();
    sk_place_cards();
}

static void sk_size_cb(lv_event_t *event)
{
    (void)event;
    sk_layout();
    sk_place_cards();
}

static void sk_start(void)
{
    if (s_sk_tmr == NULL && s_sk.field != NULL) {
        s_sk_tmr = lv_timer_create(sk_tick, 33, NULL);
    }
}

static void sk_stop(void)
{
    if (s_sk_tmr != NULL) {
        lv_timer_delete(s_sk_tmr);
        s_sk_tmr = NULL;
    }
}

/* ================================================================== */
/* Tabview                                                            */
/* ================================================================== */

static void ui_activity_update(void)
{
    if (s_paused || s_tv == NULL) {
        vd_stop();
        sk_stop();
        hc_fling_stop();
        return;
    }

    if (s_tab == 1) {
        vd_start();
        sk_stop();
        hc_fling_stop();
    } else if (s_tab == 2) {
        sk_start();
        vd_stop();
        hc_fling_stop();
    } else {
        vd_stop();
        sk_stop();
    }
}

static void ui_tab_changed_cb(lv_event_t *event)
{
    (void)event;
    s_tab = lv_tabview_get_tab_active(s_tv);
    ui_activity_update();
}

static void ui_delete_cb(lv_event_t *event)
{
    (void)event;
    vd_stop();
    sk_stop();
    hc_fling_stop();
    memset(&s_vd, 0, sizeof(s_vd));
    memset(&s_sk, 0, sizeof(s_sk));
    s_tv = NULL;
}

static void ui_tab_style(lv_obj_t *tv)
{
    lv_obj_t *bar = lv_tabview_get_tab_bar(tv);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x12141A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, WIN_PX(6), LV_PART_MAIN);
    lv_obj_set_style_pad_all(bar, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_row(bar, 3, LV_PART_MAIN);

    for (unsigned i = 0; i < 3; i++) {
        lv_obj_t *btn = lv_tabview_get_tab_button(tv, (int32_t)i);
        if (btn == NULL) {
            continue;
        }
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_text_color(btn, WIN_COLOR_TEXT_MUTED, LV_PART_MAIN);
        lv_obj_set_style_text_font(btn, WIN_FONT_SMALL, LV_PART_MAIN);
        lv_obj_set_style_radius(btn, WIN_PX(4), LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, WIN_COLOR_ACCENT,
                                  LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_80,
                                LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(btn, lv_color_white(),
                                    LV_PART_MAIN | LV_STATE_CHECKED);
    }
}

/* ------------------------------------------------------------------ */
/* Application entry point                                            */
/* ------------------------------------------------------------------ */

void win_content_ui(lv_obj_t *content)
{
    if (content == NULL) {
        return;
    }

    lv_obj_set_style_pad_all(content, 0, LV_PART_MAIN);

    s_tv = lv_tabview_create(content);
    lv_tabview_set_tab_bar_position(s_tv, LV_DIR_LEFT);
    lv_tabview_set_tab_bar_size(s_tv, WIN_PX(44));
    lv_obj_set_size(s_tv, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_tv, 0, 0);
    lv_obj_set_style_bg_opa(s_tv, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_tv, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_tv, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_tv, 0, LV_PART_MAIN);

    lv_obj_t *tv_content = lv_tabview_get_content(s_tv);
    lv_obj_set_style_bg_opa(tv_content, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tv_content, 0, LV_PART_MAIN);
    /* Every page handles its own drag gestures (panning, ring, coverflow).
     * If the tabview kept its horizontal swipe, dragging a menu would switch
     * tabs instead. Tabs are changed with the left bar only. */
    lv_obj_set_scrollable(tv_content, false);
    lv_obj_set_scrollbar_mode(tv_content, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *p_hc = lv_tabview_add_tab(s_tv, "1");
    lv_obj_t *p_vd = lv_tabview_add_tab(s_tv, "2");
    lv_obj_t *p_sk = lv_tabview_add_tab(s_tv, "3");

    lv_obj_set_style_pad_all(p_hc, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(p_hc, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(p_hc, LV_OPA_COVER, LV_PART_MAIN);

    hc_watch_create(p_hc);
    vd_create(p_vd);
    sk_create(p_sk);

    ui_tab_style(s_tv);
    lv_obj_add_event_cb(s_tv, ui_tab_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_tv, ui_delete_cb, LV_EVENT_DELETE, NULL);

    s_paused = false;
    s_tab = 0;
    s_vd_tmr = NULL;
    s_sk_tmr = NULL;

    /* Resolve the page sizes now so the first frame is laid out. */
    lv_obj_update_layout(content);
    vd_layout();
    sk_layout();
    sk_place_cards();

    ui_activity_update();
}

void win_app_ui_pause(void)
{
    /* Hidden windows must not keep animating. */
    s_paused = true;
    ui_activity_update();
}

void win_app_ui_resume(void)
{
    s_paused = false;
    ui_activity_update();
}
