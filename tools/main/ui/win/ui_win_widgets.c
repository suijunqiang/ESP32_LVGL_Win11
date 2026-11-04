/*
 * Reusable Windows 11 widgets.
 *
 * See ui_win_widgets.h for the description.
 *
 * Ported to the 320x240 panel: every pixel constant of the 420x320 design
 * is wrapped in WIN_PX() so the whole widget set scales as one piece.
 */

#include "ui_win_widgets.h"

/* ------------------------------------------------------------------ */
/* Glyphs                                                             */
/* ------------------------------------------------------------------ */

lv_obj_t *win_logo_create(lv_obj_t *parent, lv_coord_t size, lv_color_t color)
{
    lv_obj_t *logo = lv_obj_create(parent);
    lv_obj_set_size(logo, size, size);
    win_style_flat(logo);
    win_style_decoration(logo);

    const lv_coord_t gap = 1;
    const lv_coord_t cell = (size - gap) / 2;

    for (int i = 0; i < 4; i++) {
        lv_obj_t *square = lv_obj_create(logo);
        lv_obj_set_size(square, cell, cell);
        lv_obj_set_style_bg_color(square, color, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(square, LV_OPA_100, LV_PART_MAIN);
        lv_obj_set_style_border_width(square, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(square, 1, LV_PART_MAIN);
        lv_obj_set_style_pad_all(square, 0, LV_PART_MAIN);

        lv_obj_set_pos(square, (i % 2) ? cell + gap : 0, (i > 1) ? cell + gap : 0);
        win_style_decoration(square);
    }

    return logo;
}

lv_obj_t *win_search_glyph_create(lv_obj_t *parent,
                                  lv_coord_t size,
                                  lv_color_t color)
{
    lv_obj_t *glyph = lv_obj_create(parent);
    lv_obj_set_size(glyph, size, size);
    win_style_flat(glyph);
    win_style_decoration(glyph);

    const lv_coord_t ring_size = size - WIN_PX(6);

    /* The lens. */
    lv_obj_t *ring = lv_obj_create(glyph);
    lv_obj_set_size(ring, ring_size, ring_size);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(ring, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(ring, color, LV_PART_MAIN);
    lv_obj_set_style_radius(ring, LV_PCT(50), LV_PART_MAIN);
    lv_obj_set_style_pad_all(ring, 0, LV_PART_MAIN);
    lv_obj_align(ring, LV_ALIGN_TOP_LEFT, 0, 0);
    win_style_decoration(ring);

    /* The handle, tilted like a real magnifier. */
    lv_obj_t *handle = lv_obj_create(glyph);
    lv_obj_set_size(handle, 2, ring_size / 2 + 1);
    lv_obj_set_style_bg_color(handle, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(handle, LV_OPA_100, LV_PART_MAIN);
    lv_obj_set_style_border_width(handle, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(handle, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(handle, 0, LV_PART_MAIN);
    lv_obj_align(handle, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_transform_pivot_x(handle, 1, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(handle, (ring_size / 2 + 1) / 2, LV_PART_MAIN);
    lv_obj_set_style_transform_rotation(handle, 450, LV_PART_MAIN);
    win_style_decoration(handle);

    return glyph;
}

lv_obj_t *win_taskview_glyph_create(lv_obj_t *parent,
                                    lv_coord_t size,
                                    lv_color_t color)
{
    lv_obj_t *glyph = lv_obj_create(parent);
    lv_obj_set_size(glyph, size, size);
    win_style_flat(glyph);
    win_style_decoration(glyph);

    const lv_coord_t w = size - WIN_PX(4);
    const lv_coord_t h = size - WIN_PX(8);

    lv_obj_t *back = lv_obj_create(glyph);
    lv_obj_set_size(back, w, h);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(back, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(back, color, LV_PART_MAIN);
    lv_obj_set_style_radius(back, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(back, 0, LV_PART_MAIN);
    lv_obj_align(back, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    win_style_decoration(back);

    lv_obj_t *front = lv_obj_create(glyph);
    lv_obj_set_size(front, w, h);
    lv_obj_set_style_bg_opa(front, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(front, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(front, color, LV_PART_MAIN);
    lv_obj_set_style_radius(front, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(front, 0, LV_PART_MAIN);
    lv_obj_align(front, LV_ALIGN_TOP_RIGHT, 0, 0);
    win_style_decoration(front);

    return glyph;
}

lv_obj_t *win_widgets_glyph_create(lv_obj_t *parent,
                                   lv_coord_t size,
                                   lv_color_t color)
{
    lv_obj_t *glyph = lv_obj_create(parent);
    lv_obj_set_size(glyph, size, size);
    win_style_flat(glyph);
    win_style_decoration(glyph);

    const lv_coord_t gap = 2;
    const lv_coord_t cell = (size - gap) / 2;

    for (int i = 0; i < 4; i++) {
        lv_obj_t *square = lv_obj_create(glyph);
        lv_obj_set_size(square, cell, cell);
        lv_obj_set_style_bg_color(square, color, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(square, (i % 2) ? LV_OPA_60 : LV_OPA_100,
                                LV_PART_MAIN);
        lv_obj_set_style_border_width(square, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(square, 2, LV_PART_MAIN);
        lv_obj_set_style_pad_all(square, 0, LV_PART_MAIN);

        lv_obj_set_pos(square, (i % 2) ? cell + gap : 0, (i > 1) ? cell + gap : 0);
        win_style_decoration(square);
    }

    return glyph;
}

/* ------------------------------------------------------------------ */
/* App tiles and taskbar buttons                                       */
/* ------------------------------------------------------------------ */

lv_obj_t *win_app_icon_create(lv_obj_t *parent,
                              const win_app_t *app,
                              lv_coord_t size)
{
    lv_obj_t *icon = lv_obj_create(parent);
    lv_obj_set_size(icon, size, size);
    lv_obj_set_style_bg_color(icon, app->color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(icon, LV_OPA_100, LV_PART_MAIN);
    lv_obj_set_style_border_width(icon, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(icon, WIN_RADIUS_ICON, LV_PART_MAIN);
    lv_obj_set_style_pad_all(icon, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(icon, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(icon, WIN_PX(4), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(icon, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_shadow_offset_y(icon, WIN_PX(1), LV_PART_MAIN);
    win_style_decoration(icon);

    lv_obj_t *glyph = lv_label_create(icon);
    lv_label_set_text(glyph, app->symbol);
    lv_obj_set_style_text_color(glyph, WIN_COLOR_TEXT, LV_PART_MAIN);
    /* Scale the glyph font to the tile so small icons stay legible. */
    const lv_font_t *glyph_font = WIN_FONT_LARGE;
    if (size <= WIN_PX(14)) {
        glyph_font = WIN_FONT_TINY;
    } else if (size <= WIN_PX(17)) {
        glyph_font = WIN_FONT_SMALL;
    }
    lv_obj_set_style_text_font(glyph, glyph_font, LV_PART_MAIN);
    lv_obj_center(glyph);

    return icon;
}

lv_obj_t *win_taskbar_button_create(lv_obj_t *parent,
                                    const win_app_t *app,
                                    lv_obj_t **indicator)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, WIN_TASKBTN_SIZE, WIN_TASKBTN_SIZE);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(button, WIN_TASKBTN_SIZE / 2, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);

    lv_obj_t *icon = win_app_icon_create(button, app, WIN_PX(14));
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, WIN_PX(4));

    /* The running indicator: a small pill under the icon. */
    lv_obj_t *bar = lv_obj_create(button);
    lv_obj_set_size(bar, WIN_PX(12), WIN_PX(3));
    lv_obj_set_style_bg_color(bar, WIN_COLOR_ACCENT_SOFT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bar, 0, LV_PART_MAIN);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -WIN_PX(3));
    win_style_decoration(bar);

    if (indicator != NULL) {
        *indicator = bar;
    }

    return button;
}

/* ------------------------------------------------------------------ */
/* App windows                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    const win_app_t *app;
    win_window_actions_t actions;
    lv_obj_t *title;
    lv_obj_t *content;
    lv_obj_t *max_box;
    lv_obj_t *restore_glyph;
    lv_coord_t restore_x;
    lv_coord_t restore_y;
    lv_coord_t restore_w;
    lv_coord_t restore_h;
    bool maximized;
} win_window_t;

/* Drag state of one window. */
typedef struct {
    lv_obj_t *window;
    lv_point_t pointer;
    lv_point_t origin;
} win_drag_t;

/* Everything a window needs, allocated together with the window. */
typedef struct {
    win_window_t win;
    win_drag_t drag;
} win_window_priv_t;

#define WIN_CAPTION_MIN 0
#define WIN_CAPTION_MAX 1
#define WIN_CAPTION_CLOSE 2

void win_window_bring_to_front(lv_obj_t *window);

static void win_window_pressed_cb(lv_event_t *event)
{
    win_window_bring_to_front(lv_event_get_user_data(event));
}

static void win_window_delete_cb(lv_event_t *event)
{
    win_window_priv_t *priv = lv_event_get_user_data(event);

    lv_free(priv);
}

static void win_title_double_click_cb(lv_event_t *event)
{
    lv_obj_t *title = lv_event_get_target(event);
    lv_obj_t *window = lv_obj_get_parent(title);
    win_window_t *ctx = (win_window_t *)lv_obj_get_user_data(window);

    if (ctx != NULL && ctx->actions.on_maximize != NULL) {
        ctx->actions.on_maximize(window, NULL);
    }
}

static void win_title_pressed_cb(lv_event_t *event)
{
    win_drag_t *drag = lv_event_get_user_data(event);
    lv_indev_t *indev = lv_event_get_indev(event);

    if (drag == NULL || indev == NULL) {
        return;
    }

    win_window_t *ctx = (win_window_t *)lv_obj_get_user_data(drag->window);
    if (ctx != NULL && ctx->maximized) {
        return;
    }

    win_window_bring_to_front(drag->window);

    lv_indev_get_point(indev, &drag->pointer);
    drag->origin.x = lv_obj_get_x(drag->window);
    drag->origin.y = lv_obj_get_y(drag->window);
}

static void win_title_pressing_cb(lv_event_t *event)
{
    win_drag_t *drag = lv_event_get_user_data(event);
    lv_indev_t *indev = lv_event_get_indev(event);
    lv_point_t point;

    if (drag == NULL || indev == NULL) {
        return;
    }

    win_window_t *ctx = (win_window_t *)lv_obj_get_user_data(drag->window);
    if (ctx != NULL && ctx->maximized) {
        return;
    }

    lv_indev_get_point(indev, &point);

    lv_coord_t x = drag->origin.x + (point.x - drag->pointer.x);
    lv_coord_t y = drag->origin.y + (point.y - drag->pointer.y);

    /* Keep a part of the window on the screen, like a real window manager. */
    lv_obj_t *parent = lv_obj_get_parent(drag->window);
    lv_coord_t w = lv_obj_get_width(drag->window);
    lv_coord_t keep = WIN_PX(60);

    if (x > lv_obj_get_width(parent) - keep) {
        x = lv_obj_get_width(parent) - keep;
    }
    if (x < keep - w) {
        x = keep - w;
    }
    if (y > lv_obj_get_height(parent) - keep) {
        y = lv_obj_get_height(parent) - keep;
    }
    if (y < 0) {
        y = 0;
    }

    lv_obj_set_pos(drag->window, x, y);
}

static void win_caption_event_cb(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_target(event);
    lv_obj_t *window = lv_obj_get_parent(lv_obj_get_parent(button));
    win_window_t *ctx = (win_window_t *)lv_obj_get_user_data(window);
    int action = (int)(uintptr_t)lv_obj_get_user_data(button);

    if (ctx == NULL) {
        return;
    }

    switch (action) {
        case WIN_CAPTION_MIN:
            if (ctx->actions.on_minimize != NULL) {
                ctx->actions.on_minimize(window, NULL);
            }
            break;

        case WIN_CAPTION_MAX:
            if (ctx->actions.on_maximize != NULL) {
                ctx->actions.on_maximize(window, NULL);
            }
            break;

        case WIN_CAPTION_CLOSE:
            if (ctx->actions.on_close != NULL) {
                ctx->actions.on_close(window, NULL);
            }
            break;

        default:
            break;
    }
}

/* A caption button: a transparent square with a glyph inside. */
static lv_obj_t *win_caption_button_create(lv_obj_t *parent, int action)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, WIN_WINDOW_CAPTION_W, WIN_WINDOW_CAPTION_W);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, WIN_COLOR_HOVER, LV_STATE_HOVERED);
    lv_obj_set_style_bg_opa(button, LV_OPA_70, LV_STATE_HOVERED);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(button, WIN_PX(4), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
    lv_obj_set_user_data(button, (void *)(uintptr_t)action);
    lv_obj_add_event_cb(button, win_caption_event_cb, LV_EVENT_CLICKED, NULL);

    return button;
}

lv_obj_t *win_window_create(lv_obj_t *parent,
                             const win_app_t *app,
                             const win_window_actions_t *actions)
{
    win_window_priv_t *priv = lv_calloc(1, sizeof(win_window_priv_t));
    if (priv == NULL) {
        return NULL;
    }

    win_window_t *ctx = &priv->win;
    ctx->app = app;
    if (actions != NULL) {
        ctx->actions = *actions;
    }

    /* Frame. */
    lv_obj_t *window = lv_obj_create(parent);
    lv_obj_set_size(window, WIN_WINDOW_W, WIN_WINDOW_H);
    win_style_mica(window, WIN_COLOR_WINDOW, LV_OPA_COVER);
    lv_obj_set_user_data(window, ctx);
    lv_obj_add_event_cb(window, win_window_delete_cb, LV_EVENT_DELETE, priv);
    lv_obj_add_event_cb(window, win_window_pressed_cb, LV_EVENT_PRESSED, window);
    lv_obj_set_style_shadow_width(window, WIN_PX(30), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(window, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_shadow_offset_y(window, WIN_PX(8), LV_PART_MAIN);

    /* The frame stacks the title bar and the content, so the content follows
     * the window size when it is maximized. */
    lv_obj_set_flex_flow(window, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(window, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(window, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(window, 0, LV_PART_MAIN);

    /* Title bar. */
    lv_obj_t *title = lv_obj_create(window);
    ctx->title = title;
    lv_obj_set_size(title, LV_PCT(100), WIN_WINDOW_TITLE_H);
    lv_obj_set_pos(title, 0, 0);
    lv_obj_set_style_bg_color(title, WIN_COLOR_WINDOW_TITLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(title, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(title, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(title, WIN_RADIUS_PANEL, LV_PART_MAIN);
    lv_obj_set_style_pad_all(title, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(title, WIN_PX(8), LV_PART_MAIN);
    lv_obj_set_flex_flow(title, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(title, WIN_PX(6), LV_PART_MAIN);
    lv_obj_set_scrollable(title, false);

    lv_obj_t *icon = win_app_icon_create(title, app, WIN_PX(16));
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *label = lv_label_create(title);
    lv_label_set_text(label, app->label);
    lv_obj_set_style_text_color(label, WIN_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, WIN_FONT_SMALL, LV_PART_MAIN);

    /* Spacer between the title and the caption buttons. */
    lv_obj_t *spacer = lv_obj_create(title);
    lv_obj_set_size(spacer, 0, 0);
    lv_obj_set_flex_grow(spacer, 1);
    win_style_flat(spacer);

    lv_obj_t *min = win_caption_button_create(title, WIN_CAPTION_MIN);
    lv_obj_t *min_glyph = lv_label_create(min);
    lv_label_set_text(min_glyph, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_color(min_glyph, WIN_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(min_glyph, WIN_FONT_SMALL, LV_PART_MAIN);
    lv_obj_center(min_glyph);

    lv_obj_t *max = win_caption_button_create(title, WIN_CAPTION_MAX);
    lv_obj_t *max_box = lv_obj_create(max);
    lv_obj_set_size(max_box, WIN_PX(9), WIN_PX(9));
    lv_obj_set_style_bg_opa(max_box, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(max_box, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(max_box, WIN_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_pad_all(max_box, 0, LV_PART_MAIN);
    lv_obj_center(max_box);
    win_style_decoration(max_box);
    ctx->max_box = max_box;

    lv_obj_t *restore_glyph = lv_label_create(max);
    lv_label_set_text(restore_glyph, LV_SYMBOL_COPY);
    lv_obj_set_style_text_color(restore_glyph, WIN_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(restore_glyph, WIN_FONT_SMALL, LV_PART_MAIN);
    lv_obj_center(restore_glyph);
    lv_obj_set_hidden(restore_glyph, true);
    ctx->restore_glyph = restore_glyph;

    lv_obj_t *close = win_caption_button_create(title, WIN_CAPTION_CLOSE);
    lv_obj_set_style_bg_color(close, WIN_COLOR_CLOSE, LV_STATE_HOVERED);
    lv_obj_set_style_bg_opa(close, LV_OPA_100, LV_STATE_HOVERED);
    lv_obj_t *close_glyph = lv_label_create(close);
    lv_label_set_text(close_glyph, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_glyph, WIN_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(close_glyph, WIN_FONT_SMALL, LV_PART_MAIN);
    lv_obj_center(close_glyph);

    /* Dragging the window by its title bar. */
    priv->drag.window = window;
    lv_obj_add_event_cb(title, win_title_pressed_cb, LV_EVENT_PRESSED,
                        &priv->drag);
    lv_obj_add_event_cb(title, win_title_pressing_cb, LV_EVENT_PRESSING,
                        &priv->drag);
    lv_obj_add_event_cb(title, win_title_double_click_cb,
                        LV_EVENT_DOUBLE_CLICKED, NULL);
    lv_obj_add_event_cb(title, win_window_pressed_cb, LV_EVENT_PRESSED, window);

    /* Content area. */
    lv_obj_t *content = lv_obj_create(window);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_bg_color(content, WIN_COLOR_WINDOW, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(content, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content, WIN_PX(6), LV_PART_MAIN);
    lv_obj_set_scrollable(content, false);
    win_style_decoration(content);

    ctx->content = content;

    return window;
}

lv_obj_t *win_window_get_content(lv_obj_t *window)
{
    win_window_t *ctx = (win_window_t *)lv_obj_get_user_data(window);

    return (ctx != NULL) ? ctx->content : NULL;
}

void win_window_bring_to_front(lv_obj_t *window)
{
    if (window != NULL) {
        lv_obj_move_foreground(window);
    }
}

void win_window_toggle_maximize(lv_obj_t *window)
{
    if (window == NULL) {
        return;
    }

    win_window_t *ctx = (win_window_t *)lv_obj_get_user_data(window);
    if (ctx == NULL) {
        return;
    }

    lv_obj_t *parent = lv_obj_get_parent(window);
    if (parent == NULL) {
        return;
    }

    if (!ctx->maximized) {
        ctx->restore_x = lv_obj_get_x(window);
        ctx->restore_y = lv_obj_get_y(window);
        ctx->restore_w = lv_obj_get_width(window);
        ctx->restore_h = lv_obj_get_height(window);

        lv_obj_set_size(window, lv_obj_get_width(parent),
                        lv_obj_get_height(parent));
        lv_obj_set_pos(window, 0, 0);
        lv_obj_set_style_radius(window, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(ctx->title, 0, LV_PART_MAIN);
        lv_obj_set_hidden(ctx->max_box, true);
        lv_obj_set_hidden(ctx->restore_glyph, false);
        ctx->maximized = true;
    } else {
        lv_obj_set_size(window, ctx->restore_w, ctx->restore_h);
        lv_obj_set_pos(window, ctx->restore_x, ctx->restore_y);
        lv_obj_set_style_radius(window, WIN_RADIUS_PANEL, LV_PART_MAIN);
        lv_obj_set_style_radius(ctx->title, WIN_RADIUS_PANEL, LV_PART_MAIN);
        lv_obj_set_hidden(ctx->max_box, false);
        lv_obj_set_hidden(ctx->restore_glyph, true);
        ctx->maximized = false;
    }

    win_window_bring_to_front(window);
}

void win_window_destroy(lv_obj_t *window)
{
    if (window != NULL) {
        lv_obj_del(window);
    }
}
