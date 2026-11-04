/*
 * The Windows 11 start menu.
 *
 * See ui_win_start.h for the description.
 *
 * Ported to the 320x240 panel: the design metrics are wrapped in WIN_PX()
 * and the section heights were trimmed a little so the whole panel fits
 * above the taskbar (240 - 33 px taskbar - 2 px gap).
 */

#include "lv_i18n.h"
#include "ui_win_start.h"
#include "ui_win_theme.h"
#include "ui_win_widgets.h"

#include <stdio.h>

/* Everything the menu needs to remember. */
typedef struct {
    lv_obj_t *panel;
    ui_win_start_cb_t on_launch;
    void *user_data;
} ui_win_start_t;

static const win_app_id_t s_recommended[] = {
    WIN_APP_NOTEPAD,
    WIN_APP_MAIL,
    WIN_APP_MUSIC,
    WIN_APP_PHOTOS,
    WIN_APP_TERMINAL,
};

/* ------------------------------------------------------------------ */
/* Small parts                                                        */
/* ------------------------------------------------------------------ */

/* A section title such as "Pinned" or "Recommended". */
static lv_obj_t *win_start_header_create(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, WIN_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, WIN_FONT_TINY, LV_PART_MAIN);

    return label;
}

/* The search field: a magnifier and a placeholder text. */
static lv_obj_t *win_start_search_create(lv_obj_t *parent)
{
    lv_obj_t *field = lv_obj_create(parent);
    lv_obj_set_size(field, LV_PCT(100), WIN_PX(28));
    win_style_field(field);
    win_style_decoration(field);
    lv_obj_set_flex_flow(field, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(field, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(field, WIN_PX(8), LV_PART_MAIN);
    lv_obj_set_style_pad_column(field, WIN_PX(6), LV_PART_MAIN);

    lv_obj_t *glyph = win_search_glyph_create(field, WIN_PX(14),
                                              WIN_COLOR_TEXT_DIM);
    lv_obj_align(glyph, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *label = lv_label_create(field);
    lv_label_set_text(label, _("start_search"));
    lv_obj_set_style_text_color(label, WIN_COLOR_TEXT_MUTED, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, WIN_FONT_TINY, LV_PART_MAIN);

    return field;
}

/* One cell of the "Pinned" grid: the app tile and its caption. */
static void win_start_app_cb(lv_event_t *event)
{
    ui_win_start_t *start = lv_event_get_user_data(event);
    lv_obj_t *cell = lv_event_get_target(event);

    if (start == NULL || start->on_launch == NULL) {
        return;
    }

    win_app_id_t id = (win_app_id_t)(uintptr_t)lv_obj_get_user_data(cell);
    start->on_launch(id, start->user_data);
}

static void win_start_app_create(lv_obj_t *parent,
                                 win_app_id_t app_id,
                                 ui_win_start_t *start)
{
    const win_app_t *app = win_app_get(app_id);

    lv_obj_t *cell = lv_button_create(parent);
    lv_obj_set_size(cell, WIN_PX(84), WIN_PX(44));
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(cell, WIN_COLOR_HOVER, LV_STATE_HOVERED);
    lv_obj_set_style_bg_opa(cell, LV_OPA_50, LV_STATE_HOVERED);
    lv_obj_set_style_border_width(cell, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(cell, WIN_PX(6), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(cell, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cell, 0, LV_PART_MAIN);
    lv_obj_set_user_data(cell, (void *)(uintptr_t)app_id);
    lv_obj_add_event_cb(cell, win_start_app_cb, LV_EVENT_CLICKED, start);

    lv_obj_t *icon = win_app_icon_create(cell, app,
                                         WIN_START_APP_SIZE - WIN_PX(4));
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *caption = lv_label_create(cell);
    lv_label_set_text(caption, _(app->label));
    lv_obj_set_style_text_color(caption, WIN_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(caption, WIN_FONT_TINY, LV_PART_MAIN);
    lv_obj_align(caption, LV_ALIGN_BOTTOM_MID, 0, 0);
}

/* One row of the "Recommended" list. */
static void win_start_recommended_create(lv_obj_t *parent,
                                         win_app_id_t app_id,
                                         ui_win_start_t *start)
{
    const win_app_t *app = win_app_get(app_id);

    lv_obj_t *row = lv_button_create(parent);
    lv_obj_set_size(row, LV_PCT(100), WIN_PX(20));
    win_style_flat(row);
    lv_obj_set_style_radius(row, WIN_PX(4), LV_PART_MAIN);
    lv_obj_set_style_bg_color(row, WIN_COLOR_HOVER, LV_STATE_HOVERED);
    lv_obj_set_style_bg_opa(row, LV_OPA_50, LV_STATE_HOVERED);
    lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(row, WIN_PX(4), LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, WIN_PX(8), LV_PART_MAIN);
    lv_obj_set_user_data(row, (void *)(uintptr_t)app_id);
    lv_obj_add_event_cb(row, win_start_app_cb, LV_EVENT_CLICKED, start);

    lv_obj_t *icon = win_app_icon_create(row, app, WIN_PX(14));
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, _(app->label));
    lv_obj_set_style_text_color(label, WIN_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, WIN_FONT_TINY, LV_PART_MAIN);
}

/* The footer: the user account on the left, the power button on the right. */
static void win_start_footer_create(lv_obj_t *parent)
{
    lv_obj_t *footer = lv_obj_create(parent);
    lv_obj_set_size(footer, LV_PCT(100), WIN_PX(24));
    win_style_flat(footer);
    win_style_decoration(footer);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* User avatar. */
    lv_obj_t *avatar = lv_obj_create(footer);
    lv_obj_set_size(avatar, WIN_PX(22), WIN_PX(22));
    lv_obj_set_style_bg_color(avatar, WIN_COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(avatar, LV_OPA_100, LV_PART_MAIN);
    lv_obj_set_style_border_width(avatar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(avatar, LV_PCT(50), LV_PART_MAIN);
    lv_obj_set_style_pad_all(avatar, 0, LV_PART_MAIN);
    win_style_decoration(avatar);

    lv_obj_t *initial = lv_label_create(avatar);
    lv_label_set_text(initial, _("user_initials"));
    lv_obj_set_style_text_color(initial, WIN_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(initial, WIN_FONT_TINY, LV_PART_MAIN);
    lv_obj_center(initial);

    lv_obj_t *user = lv_label_create(footer);
    lv_label_set_text(user, _("user_name"));
    lv_obj_set_style_text_color(user, WIN_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(user, WIN_FONT_TINY, LV_PART_MAIN);
    lv_obj_align(user, LV_ALIGN_LEFT_MID, WIN_PX(30), 0);

    /* Power button. */
    lv_obj_t *power = lv_button_create(footer);
    lv_obj_set_size(power, WIN_PX(24), WIN_PX(24));
    lv_obj_set_style_bg_opa(power, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(power, WIN_COLOR_HOVER, LV_STATE_HOVERED);
    lv_obj_set_style_bg_opa(power, LV_OPA_70, LV_STATE_HOVERED);
    lv_obj_set_style_border_width(power, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(power, WIN_PX(4), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(power, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(power, 0, LV_PART_MAIN);

    lv_obj_t *glyph = lv_label_create(power);
    lv_label_set_text(glyph, LV_SYMBOL_POWER);
    lv_obj_set_style_text_color(glyph, WIN_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(glyph, WIN_FONT_SMALL, LV_PART_MAIN);
    lv_obj_center(glyph);
}

/* ------------------------------------------------------------------ */
/* The panel                                                          */
/* ------------------------------------------------------------------ */

static void win_start_delete_cb(lv_event_t *event)
{
    ui_win_start_t *start = lv_event_get_user_data(event);

    if (start != NULL) {
        lv_free(start);
    }
}

lv_obj_t *ui_win_start_create(lv_obj_t *parent,
                              ui_win_start_cb_t on_launch,
                              void *user_data)
{
    ui_win_start_t *start = lv_calloc(1, sizeof(ui_win_start_t));
    if (start == NULL) {
        return NULL;
    }

    start->on_launch = on_launch;
    start->user_data = user_data;

    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, WIN_START_W, WIN_START_H);
    win_style_mica(panel, WIN_COLOR_MICA, LV_OPA_90);
    lv_obj_set_style_shadow_width(panel, WIN_PX(40), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(panel, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_shadow_offset_y(panel, WIN_PX(8), LV_PART_MAIN);

    /* The panel is a vertical stack of sections. */
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(panel, WIN_PX(10), LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel, WIN_PX(6), LV_PART_MAIN);
    lv_obj_set_scrollable(panel, false);

    /* Every cell of the grid needs the callback context. */
    lv_obj_set_user_data(panel, start);
    lv_obj_add_event_cb(panel, win_start_delete_cb, LV_EVENT_DELETE, start);

    win_start_search_create(panel);

    win_start_header_create(panel, _("pinned"));

    /* The "Pinned" grid. */
    lv_obj_t *grid = lv_obj_create(panel);
    lv_obj_set_size(grid, LV_PCT(100), WIN_PX(88));
    win_style_flat(grid);
    win_style_decoration(grid);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(grid, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(grid, 0, LV_PART_MAIN);

    for (unsigned i = 0; i < win_start_pinned_count; i++) {
        win_start_app_create(grid, win_start_pinned[i], start);
    }

    win_start_header_create(panel, _("recommended"));

    /* The "Recommended" list scrolls if more apps are available than fit. */
    lv_obj_t *list = lv_obj_create(panel);
    lv_obj_set_size(list, LV_PCT(100), WIN_PX(32));
    win_style_flat(list);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(list, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(list, true);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    for (unsigned i = 0; i < sizeof(s_recommended) / sizeof(s_recommended[0]); i++) {
        win_start_recommended_create(list, s_recommended[i], start);
    }

    win_start_footer_create(panel);

    start->panel = panel;
    lv_obj_set_hidden(panel, true);

    return panel;
}

void ui_win_start_show(lv_obj_t *start)
{
    if (start != NULL) {
        lv_obj_set_hidden(start, false);
    }
}

void ui_win_start_hide(lv_obj_t *start)
{
    if (start != NULL) {
        lv_obj_set_hidden(start, true);
    }
}

bool ui_win_start_is_visible(lv_obj_t *start)
{
    return (start != NULL) && !lv_obj_is_hidden(start);
}
