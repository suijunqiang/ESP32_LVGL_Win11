/*
 * The Windows 11 desktop view.
 *
 * See ui_win_desktop.h for an overview of the screen.
 *
 * Ported to the 320x240 panel: every pixel constant of the 420x320 design
 * is wrapped in WIN_PX() (a uniform 3/4 scale) so the layout keeps its
 * proportions. Text uses the one-step-smaller font set of ui_win_theme.h.
 */

#include "ui_win_desktop.h"

#include "../ui_page_base.h"

#include "app_bt.h"
#include "app_camera.h"
#include "app_mqtt.h"
#include "app_wifi.h"
#include "ui_win_icons.h"
#include "ui_win_app_ui.h"
#include "ui_win_start.h"
#include "ui_win_theme.h"
#include "ui_win_widgets.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ------------------------------------------------------------------ */
/* State                                                              */
/* ------------------------------------------------------------------ */

static ui_page_t s_page;
static lv_obj_t *s_root;
static lv_obj_t *s_wallpaper;
static lv_obj_t *s_icon_layer;
static lv_obj_t *s_window_layer;
static lv_obj_t *s_start;
static lv_obj_t *s_taskbar;
static lv_obj_t *s_start_button;

/* The "Apps" dropdown of the taskbar: it lists the launchable apps and the
 * running windows, and replaces the old row of per-app taskbar buttons. */
static lv_obj_t *s_apps_dd;
static win_app_id_t s_dd_map[WIN_APP_COUNT];
static unsigned s_dd_map_count;

/* The flex row that holds the system buttons and the Apps dropdown. */
static lv_obj_t *s_taskbar_center;

/* The open windows, one per application. */
static lv_obj_t *s_windows[WIN_APP_COUNT];

/* Every new window is placed a little bit lower than the previous one. */
static int s_cascade;

/* Live status shown by the system tray. */
static lv_obj_t *s_clock;
static lv_obj_t *s_wifi_glyph;
static lv_obj_t *s_mqtt_dot;

static void win_window_open(win_app_id_t id);
static void win_window_set_visible(lv_obj_t *window, bool visible);
static void win_desktop_build(ui_page_t *page);

static bool win_app_is_pinned(win_app_id_t id)
{
    for (unsigned i = 0; i < win_taskbar_app_count; i++) {
        if (win_taskbar_apps[i] == id) {
            return true;
        }
    }

    return false;
}

static void win_start_dismiss(void)
{
    if (s_start != NULL && ui_win_start_is_visible(s_start)) {
        ui_win_start_hide(s_start);
        if (s_start_button != NULL) {
            win_style_start_button(s_start_button, false);
        }
    }
}

static void win_desktop_clear_shortcut_selection(void)
{
    if (s_icon_layer == NULL) {
        return;
    }

    uint32_t count = lv_obj_get_child_count(s_icon_layer);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *cell = lv_obj_get_child(s_icon_layer, (int32_t)i);
        lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    }
}

/* ------------------------------------------------------------------ */
/* Wallpaper                                                          */
/* ------------------------------------------------------------------ */

/* One soft coloured blob of the "Bloom" wallpaper. */
static void win_blob_create(lv_obj_t *parent,
                            lv_coord_t size,
                            lv_coord_t x,
                            lv_coord_t y,
                            lv_color_t color,
                            lv_opa_t opa)
{
    lv_obj_t *blob = lv_obj_create(parent);
    lv_obj_set_size(blob, size, size);
    lv_obj_set_style_bg_color(blob, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(blob, opa, LV_PART_MAIN);
    lv_obj_set_style_border_width(blob, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(blob, LV_PCT(50), LV_PART_MAIN);
    lv_obj_set_style_pad_all(blob, 0, LV_PART_MAIN);
    lv_obj_set_pos(blob, x, y);
    win_style_decoration(blob);
}

static void win_wallpaper_create(lv_obj_t *parent)
{
    s_wallpaper = lv_obj_create(parent);
    lv_obj_set_size(s_wallpaper, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_wallpaper, WIN_COLOR_WALL_TOP, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(s_wallpaper, WIN_COLOR_WALL_BOTTOM,
                                   LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(s_wallpaper, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_wallpaper, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_wallpaper, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_wallpaper, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(s_wallpaper, false);
    win_style_decoration(s_wallpaper);

    /* The soft coloured shapes of the Windows 11 wallpaper. */
    win_blob_create(s_wallpaper, WIN_PX(260), WIN_PX(-70), WIN_PX(-80),
                    WIN_COLOR_BLOB_BLUE, LV_OPA_50);
    win_blob_create(s_wallpaper, WIN_PX(200), WIN_PX(250), WIN_PX(40),
                    WIN_COLOR_BLOB_PURPLE, LV_OPA_40);
    win_blob_create(s_wallpaper, WIN_PX(160), WIN_PX(40), WIN_PX(150),
                    WIN_COLOR_BLOB_TEAL, LV_OPA_30);
}

/* ------------------------------------------------------------------ */
/* Desktop icons                                                      */
/* ------------------------------------------------------------------ */

static void win_shortcut_click_cb(lv_event_t *event)
{
    lv_obj_t *cell = lv_event_get_target(event);
    win_app_id_t app = (win_app_id_t)(uintptr_t)lv_obj_get_user_data(cell);

    /* Windows paints a blue rectangle behind the selected icon. */
    win_desktop_clear_shortcut_selection();
    lv_obj_set_style_bg_color(cell, WIN_COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cell, LV_OPA_40, LV_PART_MAIN);

    win_window_open(app);
}

static void win_shortcut_create(lv_obj_t *parent, unsigned index, unsigned rows, unsigned cols)
{
    const win_shortcut_t *shortcut = &win_desktop_shortcuts[index];

    unsigned col = index / rows;
    unsigned row = index % rows;

    lv_obj_t *cell = lv_button_create(parent);
    lv_obj_set_size(cell, WIN_DESKTOP_COL_W, WIN_PX(60));
    lv_obj_set_pos(cell,
                   WIN_PX(8) + (int)col * (WIN_DESKTOP_COL_W + WIN_PX(8)),
                   WIN_PX(8) + (int)row *
                       (WIN_DESKTOP_ICON_SIZE + WIN_PX(30)));
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(cell, WIN_COLOR_ACCENT, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(cell, LV_OPA_40, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(cell, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(cell, WIN_PX(4), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(cell, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cell, 0, LV_PART_MAIN);
    /* Stack icon over caption with a small gap. */
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cell, WIN_PX(5), LV_PART_MAIN);
    lv_obj_set_user_data(cell, (void *)(uintptr_t)shortcut->app);
    lv_obj_add_event_cb(cell, win_shortcut_click_cb, LV_EVENT_CLICKED, NULL);

    /* The icon: a rounded tile with the shortcut symbol on it. */
    lv_obj_t *icon = lv_obj_create(cell);
    lv_obj_set_size(icon, WIN_DESKTOP_ICON_SIZE, WIN_DESKTOP_ICON_SIZE);
    lv_obj_set_style_bg_color(icon, shortcut->color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(icon, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_border_width(icon, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(icon, WIN_RADIUS_ICON, LV_PART_MAIN);
    lv_obj_set_style_pad_all(icon, 0, LV_PART_MAIN);
    win_style_decoration(icon);

    lv_obj_t *glyph = lv_label_create(icon);
    lv_label_set_text(glyph, shortcut->symbol);
    lv_obj_set_style_text_color(glyph, WIN_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(glyph, WIN_FONT_LARGE, LV_PART_MAIN);
    lv_obj_center(glyph);

    /* The caption under the icon. */
    lv_obj_t *caption = lv_label_create(cell);
    lv_label_set_text(caption, shortcut->label);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(caption, WIN_DESKTOP_COL_W);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(caption, WIN_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(caption, WIN_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_opa(caption, LV_OPA_90, LV_PART_MAIN);
}

static void win_desktop_icons_create(lv_obj_t *parent)
{
    const unsigned rows = 4;
    const unsigned count = win_desktop_shortcut_count;
    const unsigned cols = (count + rows - 1) / rows;

    s_icon_layer = lv_obj_create(parent);
    lv_obj_set_size(s_icon_layer,
                    cols * WIN_DESKTOP_COL_W + (cols - 1) * WIN_PX(8) + WIN_PX(8),
                    WIN_SCREEN_H - WIN_TASKBAR_H);
    lv_obj_set_pos(s_icon_layer, 0, 0);
    win_style_flat(s_icon_layer);
    win_style_decoration(s_icon_layer);

    for (unsigned i = 0; i < count; i++) {
        win_shortcut_create(s_icon_layer, i, rows, cols);
    }
}

/* ------------------------------------------------------------------ */
/* Taskbar                                                            */
/* ------------------------------------------------------------------ */

static void win_start_button_cb(lv_event_t *event)
{
    (void)event;

    if (ui_win_start_is_visible(s_start)) {
        ui_win_start_hide(s_start);
    } else {
        ui_win_start_show(s_start);
    }

    win_style_start_button(s_start_button, ui_win_start_is_visible(s_start));
}

static void win_desktop_click_cb(lv_event_t *event)
{
    (void)event;

    /* Clicking the desktop closes the start menu, like in Windows. */
    win_start_dismiss();
    win_desktop_clear_shortcut_selection();
}

/*
 * Rebuild the entries of the Apps dropdown. Every formerly pinned app is
 * always listed (selecting it launches the app); running apps — pinned or
 * not — carry a marker: "> " when the window is visible, "- " when it is
 * minimized. s_dd_map maps each entry index back to its application.
 */
static void win_apps_dropdown_refresh(void)
{
    if (s_apps_dd == NULL) {
        return;
    }

    static char buf[320];
    size_t len = 0;
    buf[0] = '\0';
    s_dd_map_count = 0;

    for (unsigned pass = 0; pass < 2; pass++) {
        for (unsigned e = 0; e < WIN_APP_COUNT; e++) {
            /* pass 0: the pinned apps, in their old taskbar order;
             * pass 1: running apps that are not pinned. */
            const win_app_id_t id =
                pass == 0 ? (e < win_taskbar_app_count ? win_taskbar_apps[e]
                                                       : WIN_APP_COUNT)
                          : (win_app_id_t)e;
            if (id >= WIN_APP_COUNT) {
                break;
            }
            if (pass == 1 &&
                (win_app_is_pinned(id) || s_windows[id] == NULL)) {
                continue;
            }

            lv_obj_t *window = s_windows[id];
            const char *mark = window == NULL ? "  "
                             : lv_obj_is_hidden(window) ? "- " : "> ";
            const win_app_t *app = win_app_get(id);

            if (s_dd_map_count > 0) {
                len += snprintf(buf + len, sizeof(buf) - len, "\n");
            }
            len += snprintf(buf + len, sizeof(buf) - len, "%s%s %s",
                            mark, app->symbol, app->label);
            s_dd_map[s_dd_map_count++] = id;
            if (s_dd_map_count >= WIN_APP_COUNT || len >= sizeof(buf) - 20) {
                break;
            }
        }
    }

    lv_dropdown_set_options(s_apps_dd, buf);
}

/* Dark style for the popup list (it opens on the top layer). */
static void win_apps_dropdown_style_list(void)
{
    lv_obj_t *list = lv_dropdown_get_list(s_apps_dd);
    if (list == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(list, WIN_COLOR_WINDOW, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(list, WIN_COLOR_MICA_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_opa(list, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_radius(list, WIN_PX(4), LV_PART_MAIN);
    lv_obj_set_style_text_color(list, WIN_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(list, WIN_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_bg_color(list, WIN_COLOR_ACCENT, LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(list, LV_OPA_40, LV_PART_SELECTED);
}

/* The dropdown opens on LV_EVENT_RELEASED, so at LV_EVENT_CLICKED the list
 * exists and can be styled here. */
static void win_apps_dropdown_open_cb(lv_event_t *event)
{
    (void)event;
    win_apps_dropdown_style_list();
}

static void win_apps_dropdown_cb(lv_event_t *event)
{
    (void)event;
    uint32_t sel = lv_dropdown_get_selected(s_apps_dd);
    if (sel >= s_dd_map_count) {
        return;
    }

    win_start_dismiss();

    win_app_id_t id = s_dd_map[sel];
    lv_obj_t *window = s_windows[id];

    if (window != NULL) {
        /* Running: unhide and raise. */
        win_window_set_visible(window, true);
        win_window_bring_to_front(window);
    } else {
        win_window_open(id);
    }
}

static void win_taskbar_create(lv_obj_t *parent)
{
    s_taskbar = lv_obj_create(parent);
    lv_obj_set_size(s_taskbar, LV_PCT(100), WIN_TASKBAR_H);
    lv_obj_set_style_bg_color(s_taskbar, WIN_COLOR_TASKBAR, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_taskbar, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_taskbar, 0, LV_PART_MAIN);
    lv_obj_set_style_border_side(s_taskbar, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_taskbar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_taskbar, WIN_COLOR_TASKBAR_EDGE,
                                   LV_PART_MAIN);
    lv_obj_set_style_radius(s_taskbar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_taskbar, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(s_taskbar, false);
    lv_obj_align(s_taskbar, LV_ALIGN_BOTTOM_MID, 0, 0);

    /* Left: the weather widget. */
    lv_obj_t *weather = lv_obj_create(s_taskbar);
    lv_obj_set_size(weather, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    win_style_flat(weather);
    lv_obj_set_flex_flow(weather, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(weather, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(weather, WIN_PX(4), LV_PART_MAIN);
    lv_obj_align(weather, LV_ALIGN_LEFT_MID, WIN_PX(10), 0);
    win_style_decoration(weather);

    lv_obj_t *sun = lv_label_create(weather);
    lv_label_set_text(sun, LV_SYMBOL_TINT);
    lv_obj_set_style_text_color(sun, WIN_COLOR_ACCENT_SOFT, LV_PART_MAIN);
    lv_obj_set_style_text_font(sun, WIN_FONT_SMALL, LV_PART_MAIN);

    lv_obj_t *temp = lv_label_create(weather);
    lv_label_set_text(temp, "22");
    lv_obj_set_style_text_color(temp, WIN_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(temp, WIN_FONT_SMALL, LV_PART_MAIN);

    /* Centre: the system buttons and the pinned applications. */
    lv_obj_t *center = lv_obj_create(s_taskbar);
    s_taskbar_center = center;
    lv_obj_set_size(center, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    win_style_flat(center);
    lv_obj_set_flex_flow(center, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(center, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(center, WIN_TASKBTN_GAP, LV_PART_MAIN);
    lv_obj_align(center, LV_ALIGN_BOTTOM_MID, 0, -WIN_PX(5));
    win_style_decoration(center);

    /* Start button: the Windows logo in a round blue button. */
    s_start_button = lv_button_create(center);
    lv_obj_set_size(s_start_button, WIN_TASKBTN_SIZE, WIN_TASKBTN_SIZE);
    lv_obj_set_style_bg_opa(s_start_button, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_start_button, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_start_button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_start_button, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(s_start_button, win_start_button_cb, LV_EVENT_CLICKED,
                        NULL);

    lv_obj_t *logo = win_logo_create(s_start_button, WIN_PX(15),
                                     WIN_COLOR_TEXT);
    lv_obj_center(logo);

    /* Search, task view and widgets. */
    const lv_coord_t system_size = WIN_PX(26);
    const lv_coord_t system_glyph = WIN_PX(16);

    lv_obj_t *search = lv_button_create(center);
    lv_obj_set_size(search, system_size, system_size);
    lv_obj_set_style_bg_opa(search, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(search, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(search, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(search, 0, LV_PART_MAIN);

    lv_obj_t *search_glyph = win_search_glyph_create(search, system_glyph,
                                                      WIN_COLOR_TEXT);
    lv_obj_center(search_glyph);

    lv_obj_t *taskview = lv_button_create(center);
    lv_obj_set_size(taskview, system_size, system_size);
    lv_obj_set_style_bg_opa(taskview, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(taskview, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(taskview, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(taskview, 0, LV_PART_MAIN);

    lv_obj_t *taskview_glyph = win_taskview_glyph_create(taskview, system_glyph,
                                                          WIN_COLOR_TEXT);
    lv_obj_center(taskview_glyph);

    lv_obj_t *widgets = lv_button_create(center);
    lv_obj_set_size(widgets, system_size, system_size);
    lv_obj_set_style_bg_opa(widgets, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(widgets, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(widgets, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(widgets, 0, LV_PART_MAIN);

    lv_obj_t *widgets_glyph = win_widgets_glyph_create(widgets, WIN_PX(15),
                                                        WIN_COLOR_TEXT);
    lv_obj_center(widgets_glyph);

    /* Every app lives in one dropdown instead of one taskbar button each:
     * the bar stays uncluttered no matter how many windows are open. */
    s_apps_dd = lv_dropdown_create(center);
    lv_dropdown_set_text_static(s_apps_dd, "Apps");
    lv_dropdown_set_dir(s_apps_dd, LV_DIR_TOP);
    lv_dropdown_set_symbol(s_apps_dd, NULL);
    lv_obj_set_size(s_apps_dd, WIN_PX(64), WIN_TASKBTN_SIZE);
    lv_obj_set_style_bg_color(s_apps_dd, WIN_COLOR_FIELD, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_apps_dd, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_apps_dd, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_apps_dd, WIN_COLOR_MICA_BORDER,
                                  LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_apps_dd, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_radius(s_apps_dd, WIN_PX(4), LV_PART_MAIN);
    lv_obj_set_style_pad_hor(s_apps_dd, WIN_PX(8), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_apps_dd, WIN_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_apps_dd, WIN_FONT_SMALL, LV_PART_MAIN);
    lv_obj_add_event_cb(s_apps_dd, win_apps_dropdown_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_apps_dd, win_apps_dropdown_open_cb,
                        LV_EVENT_CLICKED, NULL);
    win_apps_dropdown_refresh();

    /* Right: the system tray. */
    lv_obj_t *tray = lv_obj_create(s_taskbar);
    lv_obj_set_size(tray, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    win_style_flat(tray);
    lv_obj_set_flex_flow(tray, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tray, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(tray, WIN_PX(4), LV_PART_MAIN);
    lv_obj_align(tray, LV_ALIGN_RIGHT_MID, -WIN_PX(10), 0);
    win_style_decoration(tray);

    /* Wi-Fi and MQTT state, both updated from the UI task. */
    lv_obj_t *wifi_item = lv_obj_create(tray);
    lv_obj_set_size(wifi_item, WIN_TRAY_ICON, WIN_TRAY_ICON);
    win_style_flat(wifi_item);
    win_style_decoration(wifi_item);
    s_wifi_glyph = lv_label_create(wifi_item);
    lv_label_set_text(s_wifi_glyph, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(s_wifi_glyph, WIN_COLOR_TEXT_MUTED,
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(s_wifi_glyph, WIN_FONT_TINY, LV_PART_MAIN);
    lv_obj_center(s_wifi_glyph);

    lv_obj_t *mqtt_item = lv_obj_create(tray);
    lv_obj_set_size(mqtt_item, WIN_TRAY_ICON, WIN_TRAY_ICON);
    win_style_flat(mqtt_item);
    win_style_decoration(mqtt_item);
    s_mqtt_dot = lv_obj_create(mqtt_item);
    lv_obj_set_size(s_mqtt_dot, WIN_PX(5), WIN_PX(5));
    lv_obj_set_style_bg_color(s_mqtt_dot, WIN_COLOR_TEXT_MUTED, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_mqtt_dot, LV_OPA_100, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_mqtt_dot, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_mqtt_dot, LV_PCT(50), LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_mqtt_dot, 0, LV_PART_MAIN);
    lv_obj_center(s_mqtt_dot);
    win_style_decoration(s_mqtt_dot);

    s_clock = lv_label_create(tray);
    lv_label_set_text(s_clock, "10:24");
    lv_obj_set_style_text_color(s_clock, WIN_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_clock, WIN_FONT_SMALL, LV_PART_MAIN);
}

/* ------------------------------------------------------------------ */
/* App windows                                                        */
/* ------------------------------------------------------------------ */

/* A small helper: a caption line inside a window. */
static lv_obj_t *win_content_label(lv_obj_t *parent,
                                   const char *text,
                                   lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, WIN_FONT_SMALL, LV_PART_MAIN);

    return label;
}

/* Select one row of a simple list-style app. */
static void win_content_select_cb(lv_event_t *event)
{
    lv_obj_t *row = lv_event_get_target(event);
    lv_obj_t *list = lv_obj_get_parent(row);

    uint32_t count = lv_obj_get_child_count(list);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *child = lv_obj_get_child(list, (int32_t)i);
        lv_obj_set_style_bg_opa(child, LV_OPA_TRANSP, LV_PART_MAIN);
    }

    lv_obj_set_style_bg_color(row, WIN_COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_PART_MAIN);
}

/* A compact transparent row used by Explorer, Mail and Store. */
static lv_obj_t *win_content_row_create(lv_obj_t *parent, lv_coord_t height)
{
    lv_obj_t *row = lv_button_create(parent);
    lv_obj_set_size(row, LV_PCT(100), height);
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

    return row;
}

/* One row of a settings list: a caption and a switch. */
static void win_settings_row_create(lv_obj_t *parent,
                                    const char *text,
                                    bool on)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), WIN_PX(22));
    win_style_flat(row);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    win_content_label(row, text, WIN_COLOR_TEXT);

    lv_obj_t *toggle = lv_switch_create(row);
    lv_obj_set_size(toggle, WIN_PX(30), WIN_PX(16));
    if (on) {
        lv_obj_add_state(toggle, LV_STATE_CHECKED);
    }
}

static void win_content_explorer(lv_obj_t *content)
{
    static const char *folders[] = {
        "Documents", "Downloads", "Pictures", "Projects"
    };

    /* Address bar. */
    lv_obj_t *toolbar = lv_obj_create(content);
    lv_obj_set_size(toolbar, LV_PCT(100), WIN_PX(22));
    lv_obj_set_pos(toolbar, 0, 0);
    win_style_field(toolbar);
    lv_obj_set_flex_flow(toolbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(toolbar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(toolbar, WIN_PX(6), LV_PART_MAIN);
    lv_obj_set_style_pad_column(toolbar, WIN_PX(6), LV_PART_MAIN);

    win_content_label(toolbar, LV_SYMBOL_LEFT, WIN_COLOR_TEXT_DIM);
    win_content_label(toolbar, LV_SYMBOL_RIGHT, WIN_COLOR_TEXT_MUTED);
    win_content_label(toolbar, "This PC  >  Documents", WIN_COLOR_TEXT_DIM);

    /* File list. */
    lv_obj_t *list = lv_obj_create(content);
    lv_obj_set_size(list, LV_PCT(100), WIN_PX(90));
    lv_obj_set_pos(list, 0, WIN_PX(28));
    win_style_flat(list);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(list, WIN_PX(4), LV_PART_MAIN);

    for (unsigned i = 0; i < sizeof(folders) / sizeof(folders[0]); i++) {
        lv_obj_t *row = win_content_row_create(list, WIN_PX(20));
        lv_obj_add_event_cb(row, win_content_select_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *icon = lv_obj_create(row);
        lv_obj_set_size(icon, WIN_PX(14), WIN_PX(12));
        lv_obj_set_style_bg_color(icon, WIN_COLOR_ACCENT_SOFT, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(icon, LV_OPA_100, LV_PART_MAIN);
        lv_obj_set_style_border_width(icon, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(icon, WIN_PX(2), LV_PART_MAIN);
        lv_obj_set_style_pad_all(icon, 0, LV_PART_MAIN);
        win_style_decoration(icon);

        win_content_label(row, folders[i], WIN_COLOR_TEXT);
    }
}

static void win_content_notepad(lv_obj_t *content)
{
    lv_obj_t *area = lv_textarea_create(content);
    lv_obj_set_size(area, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(area, WIN_COLOR_WINDOW_TITLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(area, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_border_width(area, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(area, WIN_PX(4), LV_PART_MAIN);
    lv_obj_set_style_text_color(area, WIN_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(area, WIN_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_pad_all(area, WIN_PX(6), LV_PART_MAIN);
    lv_textarea_set_text(area,
                         "Windows 11 demo\n\nEverything on this screen is "
                         "drawn with LVGL objects.\n\nDrag the title bar to "
                         "move the window.");
}

static void win_content_settings(lv_obj_t *content)
{
    lv_obj_t *list = lv_obj_create(content);
    lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));
    win_style_flat(list);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(list, WIN_PX(4), LV_PART_MAIN);

    win_settings_row_create(list, "Bluetooth", true);
    win_settings_row_create(list, "Wi-Fi", true);
    win_settings_row_create(list, "Night light", false);
    win_settings_row_create(list, "Airplane mode", false);
}

static void win_content_edge(lv_obj_t *content)
{
    /* The address bar. */
    lv_obj_t *address = lv_obj_create(content);
    lv_obj_set_size(address, LV_PCT(100), WIN_PX(22));
    win_style_field(address);
    lv_obj_set_flex_flow(address, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(address, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(address, WIN_PX(6), LV_PART_MAIN);
    lv_obj_set_style_pad_column(address, WIN_PX(6), LV_PART_MAIN);

    lv_obj_t *lock = lv_label_create(address);
    lv_label_set_text(lock, LV_SYMBOL_OK);
    lv_obj_set_style_text_color(lock, WIN_COLOR_ACCENT_SOFT, LV_PART_MAIN);
    lv_obj_set_style_text_font(lock, WIN_FONT_SMALL, LV_PART_MAIN);

    win_content_label(address, "https://lvgl.io", WIN_COLOR_TEXT_DIM);

    /* The page itself. */
    lv_obj_t *page = lv_obj_create(content);
    lv_obj_set_size(page, LV_PCT(100), WIN_PX(78));
    lv_obj_set_pos(page, 0, WIN_PX(28));
    lv_obj_set_style_bg_color(page, WIN_COLOR_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(page, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(page, WIN_PX(4), LV_PART_MAIN);
    lv_obj_set_style_pad_all(page, WIN_PX(8), LV_PART_MAIN);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(page, WIN_PX(6), LV_PART_MAIN);

    lv_obj_t *title = win_content_label(page, "Light and Versatile",
                                        WIN_COLOR_TEXT);
    lv_obj_set_style_text_font(title, WIN_FONT_LARGE, LV_PART_MAIN);

    win_content_label(page, "Embedded graphics library", WIN_COLOR_TEXT_DIM);
    win_content_label(page, "for MCU and MPU", WIN_COLOR_TEXT_DIM);
}

static void win_content_play_cb(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_target(event);
    lv_obj_t *glyph = lv_obj_get_child(button, 0);
    bool playing = (bool)(uintptr_t)lv_obj_get_user_data(button);

    playing = !playing;
    lv_obj_set_user_data(button, (void *)(uintptr_t)playing);

    if (glyph != NULL) {
        lv_label_set_text(glyph, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    }
}

static lv_obj_t *win_content_icon_button(lv_obj_t *parent,
                                         const char *symbol,
                                         bool primary)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, WIN_PX(26), WIN_PX(22));
    lv_obj_set_style_bg_color(button,
                              primary ? WIN_COLOR_ACCENT : WIN_COLOR_FIELD,
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, primary ? LV_OPA_100 : LV_OPA_60,
                            LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, WIN_COLOR_HOVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(button, WIN_PX(5), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);

    lv_obj_t *glyph = lv_label_create(button);
    lv_label_set_text(glyph, symbol);
    lv_obj_set_style_text_color(glyph, WIN_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(glyph, WIN_FONT_SMALL, LV_PART_MAIN);
    lv_obj_center(glyph);

    return button;
}

static void win_content_music(lv_obj_t *content)
{
    lv_obj_t *cover = lv_obj_create(content);
    lv_obj_set_size(cover, WIN_PX(46), WIN_PX(46));
    lv_obj_set_pos(cover, 0, 0);
    lv_obj_set_style_bg_color(cover, WIN_COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(cover, WIN_COLOR_ACCENT_2, LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(cover, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_border_width(cover, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(cover, WIN_PX(6), LV_PART_MAIN);
    lv_obj_set_style_pad_all(cover, 0, LV_PART_MAIN);
    win_style_decoration(cover);

    lv_obj_t *note = lv_label_create(cover);
    lv_label_set_text(note, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(note, WIN_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(note, WIN_FONT_LARGE, LV_PART_MAIN);
    lv_obj_center(note);

    lv_obj_t *title = win_content_label(content, "Windows 11 theme",
                                        WIN_COLOR_TEXT);
    lv_obj_set_pos(title, WIN_PX(54), WIN_PX(5));
    lv_obj_set_style_text_font(title, WIN_FONT_LARGE, LV_PART_MAIN);

    lv_obj_t *artist = win_content_label(content, "Sui Junqiang",
                                         WIN_COLOR_TEXT_DIM);
    lv_obj_set_pos(artist, WIN_PX(54), WIN_PX(25));

    lv_obj_t *bar = lv_bar_create(content);
    lv_obj_set_size(bar, LV_PCT(100), WIN_PX(4));
    lv_obj_set_pos(bar, 0, WIN_PX(54));
    lv_obj_set_style_bg_color(bar, WIN_COLOR_HOVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, WIN_COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, WIN_PX(2), LV_PART_MAIN);
    lv_bar_set_value(bar, 42, LV_ANIM_OFF);

    lv_obj_t *elapsed = win_content_label(content, "1:12", WIN_COLOR_TEXT_MUTED);
    lv_obj_set_pos(elapsed, 0, WIN_PX(62));
    lv_obj_t *duration = win_content_label(content, "3:04", WIN_COLOR_TEXT_MUTED);
    lv_obj_align(duration, LV_ALIGN_TOP_RIGHT, 0, WIN_PX(62));

    lv_obj_t *controls = lv_obj_create(content);
    lv_obj_set_size(controls, LV_PCT(100), WIN_PX(24));
    lv_obj_set_pos(controls, 0, WIN_PX(84));
    win_style_flat(controls);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(controls, WIN_PX(8), LV_PART_MAIN);
    win_style_decoration(controls);

    win_content_icon_button(controls, LV_SYMBOL_PREV, false);
    lv_obj_t *play = win_content_icon_button(controls, LV_SYMBOL_PAUSE, true);
    win_content_icon_button(controls, LV_SYMBOL_NEXT, false);
    lv_obj_set_user_data(play, (void *)(uintptr_t)1);
    lv_obj_add_event_cb(play, win_content_play_cb, LV_EVENT_CLICKED, NULL);
}

static void win_content_photo_click_cb(lv_event_t *event)
{
    lv_obj_t *tile = lv_event_get_target(event);
    lv_obj_t *grid = lv_obj_get_parent(tile);

    uint32_t count = lv_obj_get_child_count(grid);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *child = lv_obj_get_child(grid, (int32_t)i);
        lv_obj_set_style_border_width(child, 0, LV_PART_MAIN);
    }

    lv_obj_set_style_border_width(tile, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(tile, WIN_COLOR_TEXT, LV_PART_MAIN);
}

static void win_content_photos(lv_obj_t *content)
{
    static const lv_color_t colors[] = {
        LV_COLOR_MAKE(0xE8, 0x44, 0x7A), LV_COLOR_MAKE(0x1B, 0xA3, 0x7B),
        LV_COLOR_MAKE(0xF2, 0xA3, 0x3C), LV_COLOR_MAKE(0x2F, 0x7B, 0xD9),
        LV_COLOR_MAKE(0x7B, 0x3F, 0xE4), LV_COLOR_MAKE(0x18, 0xB7, 0xC9),
    };

    lv_obj_t *grid = lv_obj_create(content);
    lv_obj_set_size(grid, LV_PCT(100), LV_PCT(100));
    win_style_flat(grid);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(grid, WIN_PX(4), LV_PART_MAIN);
    lv_obj_set_style_pad_row(grid, WIN_PX(4), LV_PART_MAIN);

    for (unsigned i = 0; i < sizeof(colors) / sizeof(colors[0]); i++) {
        lv_obj_t *tile = lv_obj_create(grid);
        lv_obj_set_size(tile, WIN_PX(52), WIN_PX(40));
        lv_obj_set_style_bg_color(tile, colors[i], LV_PART_MAIN);
        lv_obj_set_style_bg_opa(tile, LV_OPA_90, LV_PART_MAIN);
        lv_obj_set_style_border_width(tile, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(tile, WIN_PX(4), LV_PART_MAIN);
        lv_obj_set_style_pad_all(tile, 0, LV_PART_MAIN);
        lv_obj_set_clickable(tile, true);
        lv_obj_add_event_cb(tile, win_content_photo_click_cb, LV_EVENT_CLICKED,
                            NULL);
    }
}

static void win_content_terminal(lv_obj_t *content)
{
    lv_obj_t *area = lv_obj_create(content);
    lv_obj_set_size(area, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(area, lv_color_hex(0x101016), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(area, LV_OPA_100, LV_PART_MAIN);
    lv_obj_set_style_border_width(area, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(area, WIN_PX(4), LV_PART_MAIN);
    lv_obj_set_style_pad_all(area, WIN_PX(6), LV_PART_MAIN);
    lv_obj_set_flex_flow(area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(area, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(area, WIN_PX(2), LV_PART_MAIN);

    static const char *lines[] = {
        "C:\\Users\\Sui> ls",
        "Desktop  Documents  Pictures",
        "C:\\Users\\Sui> _",
    };

    for (unsigned i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
        win_content_label(area, lines[i], WIN_COLOR_TEXT_DIM);
    }

/* ------------------------------------------------------------------ */
/* App windows                                                        */
/* ------------------------------------------------------------------ */

}

static void win_content_store_click_cb(lv_event_t *event)
{
    lv_obj_t *row = lv_event_get_target(event);
    win_app_id_t id = (win_app_id_t)(uintptr_t)lv_obj_get_user_data(row);

    win_window_open(id);
}

static void win_content_store(lv_obj_t *content)
{
    lv_obj_t *list = lv_obj_create(content);
    lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));
    win_style_flat(list);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(list, WIN_PX(6), LV_PART_MAIN);

    static const win_app_id_t apps[] = {
        WIN_APP_EDGE, WIN_APP_PHOTOS, WIN_APP_MUSIC, WIN_APP_TERMINAL
    };

    for (unsigned i = 0; i < sizeof(apps) / sizeof(apps[0]); i++) {
        const win_app_t *app = win_app_get(apps[i]);

        lv_obj_t *row = win_content_row_create(list, WIN_PX(24));
        lv_obj_set_user_data(row, (void *)(uintptr_t)apps[i]);
        lv_obj_add_event_cb(row, win_content_store_click_cb, LV_EVENT_CLICKED,
                            NULL);

        lv_obj_t *icon = win_app_icon_create(row, app, WIN_PX(18));
        win_style_decoration(icon);

        lv_obj_t *name = win_content_label(row, app->label, WIN_COLOR_TEXT);
        lv_obj_set_flex_grow(name, 1);

        lv_obj_t *open = win_content_label(row, "Open", WIN_COLOR_TEXT_MUTED);
        lv_obj_set_style_text_opa(open, LV_OPA_80, LV_PART_MAIN);
    }
}

static void win_content_mail(lv_obj_t *content)
{
    static const char *mails[] = {
        "Windows 11 design guide",
        "Your weekly digest",
    };

    lv_obj_t *list = lv_obj_create(content);
    lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));
    win_style_flat(list);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(list, WIN_PX(8), LV_PART_MAIN);

    for (unsigned i = 0; i < sizeof(mails) / sizeof(mails[0]); i++) {
        lv_obj_t *row = win_content_row_create(list, WIN_PX(24));
        lv_obj_add_event_cb(row, win_content_select_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *dot = lv_obj_create(row);
        lv_obj_set_size(dot, WIN_PX(6), WIN_PX(6));
        lv_obj_set_style_bg_color(dot, WIN_COLOR_ACCENT, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dot, LV_OPA_100, LV_PART_MAIN);
        lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(dot, LV_PCT(50), LV_PART_MAIN);
        lv_obj_set_style_pad_all(dot, 0, LV_PART_MAIN);
        win_style_decoration(dot);

        lv_obj_t *subject = win_content_label(row, mails[i], WIN_COLOR_TEXT);
        lv_obj_set_flex_grow(subject, 1);

        win_content_label(row, "10:24", WIN_COLOR_TEXT_MUTED);
    }
}

/* Forward declarations — defined below the switch in win_content_fill(). */
static void win_content_wifi(lv_obj_t *content);
static void win_content_mqtt(lv_obj_t *content);
static void win_content_bt(lv_obj_t *content);
static void win_content_cam(lv_obj_t *content);

static void win_content_fill(lv_obj_t *content, win_app_id_t id)
{
    if (content == NULL) {
        return;
    }

    switch (id) {
        case WIN_APP_EDGE:
            win_content_edge(content);
            break;

        case WIN_APP_EXPLORER:
            win_content_explorer(content);
            break;

        case WIN_APP_SETTINGS:
            win_content_settings(content);
            break;

        case WIN_APP_STORE:
            win_content_store(content);
            break;

        case WIN_APP_NOTEPAD:
            win_content_notepad(content);
            break;

        case WIN_APP_MAIL:
            win_content_mail(content);
            break;

        case WIN_APP_MUSIC:
            win_content_music(content);
            break;

        case WIN_APP_PHOTOS:
            win_content_photos(content);
            break;

        case WIN_APP_TERMINAL:
            win_content_terminal(content);
            break;

        case WIN_APP_WIFI:
            win_content_wifi(content);
            break;

        case WIN_APP_MQTT:
            win_content_mqtt(content);
            break;

        case WIN_APP_BT:
            win_content_bt(content);
            break;

        case WIN_APP_CAM:
            win_content_cam(content);
            break;

        case WIN_APP_UI:
            win_content_ui(content);
            break;

        default:
            break;
    }
}

/* ------------------------------------------------------------------ */
/* Wi-Fi & MQTT panels (inserted before the Store click handler)      */
/* ------------------------------------------------------------------ */

/* ---- Wi-Fi ---- */

/* Static objects kept between helper closures. */
struct wifi_ap_row_ud {
    char ssid[WIFI_SSID_MAX];
    int8_t rssi;
};

static lv_obj_t *win_panel_field(lv_obj_t *parent, const char *hint, int width)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_size(ta, width, WIN_PX(20));
    lv_obj_set_style_bg_color(ta, WIN_COLOR_FIELD, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ta, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, WIN_COLOR_MICA_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_opa(ta, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_radius(ta, WIN_PX(4), LV_PART_MAIN);
    lv_obj_set_style_pad_hor(ta, WIN_PX(6), LV_PART_MAIN);
    lv_obj_set_style_pad_ver(ta, WIN_PX(3), LV_PART_MAIN);
    lv_obj_set_style_text_color(ta, WIN_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(ta, WIN_FONT_SMALL, LV_PART_MAIN);
    lv_textarea_set_placeholder_text(ta, hint);
    lv_textarea_set_cursor_click_pos(ta, true);
    return ta;
}

static lv_obj_t *win_panel_button(lv_obj_t *parent, const char *text,
                                 bool primary, int w)
{
    if (w <= 0) w = WIN_PX(52);
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, WIN_PX(22));
    lv_obj_set_style_bg_color(btn,
                              primary ? WIN_COLOR_ACCENT : WIN_COLOR_FIELD,
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, primary ? LV_OPA_100 : LV_OPA_80,
                            LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, WIN_COLOR_MICA_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_opa(btn, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, WIN_PX(4), LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, WIN_COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, WIN_FONT_SMALL, LV_PART_MAIN);
    lv_obj_center(label);

    return btn;
}

/* Wi-Fi state helpers — simple globals, one panel at a time. */
static lv_obj_t *s_wifi_status;    /* "name  ip  rssi" or "disconnected" */
static lv_obj_t *s_wifi_ssid_ta;   /* editable SSID textarea            */
static lv_obj_t *s_wifi_psk_ta;    /* editable PSK textarea             */
static lv_obj_t *s_wifi_list;      /* scan result list                  */

static void win_wifi_refresh_status(void)
{
    if (s_wifi_status == NULL) return;
    char ssid[WIFI_SSID_MAX] = {0}, ip[16] = {0};
    int8_t rssi = app_wifi_get_rssi();
    app_wifi_get_credentials(ssid, sizeof(ssid), NULL, 0);
    app_wifi_get_ip(ip, sizeof(ip));

    if (app_wifi_is_connected()) {
        lv_label_set_text_fmt(s_wifi_status, LV_SYMBOL_WIFI " %s  %s  %ddBm",
                               ssid, ip, (int)rssi);
    } else {
        lv_label_set_text_fmt(s_wifi_status, LV_SYMBOL_CLOSE " not connected");
    }
}

/* Free wifi_ap_row_ud when the row is deleted (avoids leaks on window close). */
static void win_wifi_ap_row_delete_cb(lv_event_t *event)
{
    lv_obj_t *row = lv_event_get_target(event);
    lv_obj_t *holder = lv_obj_get_child(row, 0);
    if (holder != NULL) {
        void *ud = lv_obj_get_user_data(holder);
        if (ud != NULL) {
            free(ud);
            lv_obj_set_user_data(holder, NULL);
        }
    }
}

static void win_wifi_ap_click_cb(lv_event_t *event)
{
    lv_obj_t *row = lv_event_get_target(event);
    lv_obj_t *holder = lv_obj_get_child(row, 0);
    struct wifi_ap_row_ud *ap = holder
        ? (struct wifi_ap_row_ud *)lv_obj_get_user_data(holder) : NULL;

    if (ap != NULL && s_wifi_ssid_ta != NULL) {
        lv_textarea_set_text(s_wifi_ssid_ta, ap->ssid);
        /* Clear all sibling selections. */
        lv_obj_t *list = lv_obj_get_parent(row);
        uint32_t count = lv_obj_get_child_count(list);
        for (uint32_t i = 0; i < count; i++) {
            lv_obj_t *child = lv_obj_get_child(list, (int32_t)i);
            lv_obj_set_style_bg_opa(child, LV_OPA_TRANSP, LV_PART_MAIN);
        }
        lv_obj_set_style_bg_color(row, WIN_COLOR_ACCENT, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row, LV_OPA_40, LV_PART_MAIN);
    }
}

static void win_wifi_scan_cb(lv_event_t *event)
{
    lv_obj_t *btn = lv_event_get_target(event);
    lv_obj_t *label = lv_obj_get_child(btn, 0);

    if (s_wifi_list == NULL) return;

    lv_obj_clean(s_wifi_list);
    lv_obj_set_state(btn, LV_STATE_DISABLED, true);
    lv_label_set_text(label, "...");

    wifi_ap_t aps[WIFI_SCAN_MAX];
    int count = app_wifi_scan(aps, WIFI_SCAN_MAX);

    lv_label_set_text(label, "Scan");
    lv_obj_set_state(btn, LV_STATE_DISABLED, false);

    win_wifi_refresh_status();

    if (count == 0) {
        lv_obj_t *empty = win_content_label(s_wifi_list, "no AP",
                                             WIN_COLOR_TEXT_MUTED);
        lv_obj_set_flex_grow(empty, 1);
        return;
    }

    for (int i = 0; i < count; i++) {
        lv_obj_t *row = lv_button_create(s_wifi_list);
        lv_obj_set_size(row, LV_PCT(100), WIN_PX(18));
        win_style_flat(row);
        lv_obj_set_style_radius(row, WIN_PX(3), LV_PART_MAIN);
        lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(row, WIN_PX(4), LV_PART_MAIN);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(row, win_wifi_ap_click_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(row, win_wifi_ap_row_delete_cb, LV_EVENT_DELETE,
                            NULL);

        struct wifi_ap_row_ud *ud = malloc(sizeof(*ud));
        if (ud == NULL) continue;
        snprintf(ud->ssid, sizeof(ud->ssid), "%s",
                 aps[i].ssid[0] ? aps[i].ssid : "(hidden)");
        ud->rssi = aps[i].rssi;

        lv_obj_t *holder = lv_obj_create(row);
        win_style_flat(holder);
        win_style_decoration(holder);
        lv_obj_set_user_data(holder, ud);

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, ud->ssid);
        lv_obj_set_style_text_color(name, WIN_COLOR_TEXT, LV_PART_MAIN);
        lv_obj_set_style_text_font(name, WIN_FONT_TINY, LV_PART_MAIN);
        lv_obj_set_flex_grow(name, 1);

        char buf[12];
        snprintf(buf, sizeof(buf), "%ddBm", (int)aps[i].rssi);
        lv_obj_t *sig = lv_label_create(row);
        lv_label_set_text(sig, buf);
        lv_obj_set_style_text_color(sig, WIN_COLOR_TEXT_MUTED, LV_PART_MAIN);
        lv_obj_set_style_text_font(sig, WIN_FONT_TINY, LV_PART_MAIN);
    }
}

static void win_wifi_join_cb(lv_event_t *event)
{
    (void)event;

    if (s_wifi_ssid_ta == NULL) return;
    const char *ssid = lv_textarea_get_text(s_wifi_ssid_ta);
    const char *psk = (s_wifi_psk_ta != NULL) ? lv_textarea_get_text(s_wifi_psk_ta) : "";

    if (ssid == NULL || ssid[0] == '\0') {
        if (s_wifi_status != NULL) {
            lv_label_set_text(s_wifi_status, LV_SYMBOL_CLOSE " SSID empty");
        }
        return;
    }

    esp_err_t err = app_wifi_set_credentials(ssid, psk);
    if (err == ESP_OK) {
        if (s_wifi_status != NULL) {
            lv_label_set_text_fmt(s_wifi_status,
                                  LV_SYMBOL_WARNING " reconnecting to \"%s\"...",
                                  ssid);
        }
        app_wifi_reconnect();
    }
}

static void win_content_wifi(lv_obj_t *content)
{
    /* Reset the statics first. */
    s_wifi_status = NULL;
    s_wifi_ssid_ta = NULL;
    s_wifi_psk_ta = NULL;
    s_wifi_list = NULL;

    /* Content stacks vertically; the scan list absorbs leftover space. */
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content, WIN_PX(4), LV_PART_MAIN);

    s_wifi_status = lv_label_create(content);
    lv_obj_set_size(s_wifi_status, LV_PCT(100), WIN_PX(14));
    lv_obj_set_style_text_color(s_wifi_status, WIN_COLOR_TEXT_DIM,
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(s_wifi_status, WIN_FONT_SMALL, LV_PART_MAIN);

    win_wifi_refresh_status();

    /* Row 1: Scan button + SSID field. */
    lv_obj_t *row1 = lv_obj_create(content);
    lv_obj_set_size(row1, LV_PCT(100), WIN_PX(22));
    win_style_flat(row1);
    lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row1, WIN_PX(4), LV_PART_MAIN);

    lv_obj_t *scan_btn = win_panel_button(row1, "Scan", false, WIN_PX(44));
    lv_obj_add_event_cb(scan_btn, win_wifi_scan_cb, LV_EVENT_CLICKED, NULL);

    char ssid[WIFI_SSID_MAX] = {0}, psk[WIFI_PSK_MAX] = {0};
    app_wifi_get_credentials(ssid, sizeof(ssid), psk, sizeof(psk));

    s_wifi_ssid_ta = win_panel_field(row1, "SSID", WIN_PX(130));
    lv_obj_set_flex_grow(s_wifi_ssid_ta, 1);
    lv_textarea_set_text(s_wifi_ssid_ta, ssid);

    /* Row 2: Password + Connect button. */
    lv_obj_t *row2 = lv_obj_create(content);
    lv_obj_set_size(row2, LV_PCT(100), WIN_PX(22));
    win_style_flat(row2);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row2, WIN_PX(4), LV_PART_MAIN);

    s_wifi_psk_ta = win_panel_field(row2, "password", WIN_PX(140));
    lv_obj_set_flex_grow(s_wifi_psk_ta, 1);
    lv_textarea_set_text(s_wifi_psk_ta, psk);

    lv_obj_t *join_btn = win_panel_button(row2, "Connect", true, WIN_PX(64));
    lv_obj_add_event_cb(join_btn, win_wifi_join_cb, LV_EVENT_CLICKED, NULL);

    /* The scan results list below the rows. */
    s_wifi_list = lv_obj_create(content);
    lv_obj_set_width(s_wifi_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_wifi_list, 1);
    win_style_flat(s_wifi_list);
    lv_obj_set_flex_flow(s_wifi_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_wifi_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_wifi_list, WIN_PX(2), LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(s_wifi_list, LV_SCROLLBAR_MODE_AUTO);

    /* Placeholder row so the area is visible before first scan. */
    lv_obj_t *ph = win_content_label(s_wifi_list, "press Scan to list APs",
                                      WIN_COLOR_TEXT_MUTED);
    lv_obj_set_flex_grow(ph, 1);
}

/* ---- MQTT ---- */

static lv_obj_t *s_mqtt_status;
static lv_obj_t *s_mqtt_url_ta;
static lv_obj_t *s_mqtt_topic_ta;
static lv_obj_t *s_mqtt_payload_ta;
static lv_obj_t *s_mqtt_log;

static void win_mqtt_refresh_status(void)
{
    if (s_mqtt_status == NULL) return;
    mqtt_status_t st = {0};
    app_mqtt_get_status(&st);

    const char *sym = st.connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE;
    lv_label_set_text_fmt(s_mqtt_status, "%s %s (uptime %ds)",
                           sym, st.uri, st.uptime_s);
}

static void win_mqtt_log_refresh(void)
{
    if (s_mqtt_log == NULL) return;
    char buf[2048] = {0};
    app_mqtt_get_log(buf, sizeof(buf));
    lv_textarea_set_text(s_mqtt_log, buf);
}

static void win_mqtt_publish_cb(lv_event_t *event)
{
    (void)event;
    if (s_mqtt_topic_ta == NULL || s_mqtt_payload_ta == NULL) return;
    const char *topic = lv_textarea_get_text(s_mqtt_topic_ta);
    const char *payload = lv_textarea_get_text(s_mqtt_payload_ta);
    int id = app_mqtt_publish(topic ?: "", payload ?: "");
    if (s_mqtt_status != NULL) {
        lv_label_set_text_fmt(s_mqtt_status, "published #%d to %s",
                               id, topic);
    }
    win_mqtt_log_refresh();
}

static void win_mqtt_subscribe_cb(lv_event_t *event)
{
    (void)event;
    if (s_mqtt_topic_ta == NULL) return;
    const char *topic = lv_textarea_get_text(s_mqtt_topic_ta);
    esp_err_t err = app_mqtt_subscribe(topic ?: "");
    if (s_mqtt_status != NULL) {
        lv_label_set_text(s_mqtt_status,
                          err == ESP_OK ? "subscribed" : "subscribe failed");
    }
    win_mqtt_log_refresh();
}

static void win_mqtt_restart_cb(lv_event_t *event)
{
    (void)event;
    /* Persist whatever the user typed into the textareas first. */
    if (s_mqtt_url_ta != NULL || s_mqtt_topic_ta != NULL) {
        app_mqtt_set_config(
            s_mqtt_url_ta    ? lv_textarea_get_text(s_mqtt_url_ta)    : NULL,
            s_mqtt_topic_ta  ? lv_textarea_get_text(s_mqtt_topic_ta)  : NULL);
    }

    if (s_mqtt_status != NULL) {
        lv_label_set_text(s_mqtt_status, "restarting...");
    }
    app_mqtt_restart();
    win_mqtt_refresh_status();
    win_mqtt_log_refresh();
}

static void win_mqtt_log_click_cb(lv_event_t *event)
{
    /* Clicking the log area refreshes it. */
    (void)event;
    win_mqtt_log_refresh();
}

static void win_content_mqtt(lv_obj_t *content)
{
    s_mqtt_status = NULL;
    s_mqtt_url_ta = NULL;
    s_mqtt_topic_ta = NULL;
    s_mqtt_payload_ta = NULL;
    s_mqtt_log = NULL;

    mqtt_status_t st = {0};
    app_mqtt_get_status(&st);

    /* Content stacks vertically; the log area absorbs leftover space. */
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content, WIN_PX(4), LV_PART_MAIN);

    /* Header row: status label + Restart button. */
    lv_obj_t *header = lv_obj_create(content);
    lv_obj_set_size(header, LV_PCT(100), WIN_PX(16));
    win_style_flat(header);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, WIN_PX(4), LV_PART_MAIN);

    s_mqtt_status = lv_label_create(header);
    lv_obj_set_flex_grow(s_mqtt_status, 1);
    lv_obj_set_style_text_color(s_mqtt_status, WIN_COLOR_TEXT_DIM,
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(s_mqtt_status, WIN_FONT_TINY, LV_PART_MAIN);

    lv_obj_t *restart = win_panel_button(header, "Restart", false, WIN_PX(58));
    lv_obj_add_event_cb(restart, win_mqtt_restart_cb, LV_EVENT_CLICKED, NULL);

    win_mqtt_refresh_status();

    /* Broker URL field (full width). */
    s_mqtt_url_ta = win_panel_field(content, "mqtt://host:1883", WIN_PX(254));
    lv_obj_set_width(s_mqtt_url_ta, LV_PCT(100));
    lv_textarea_set_text(s_mqtt_url_ta, st.uri);

    /* Row 3: Topic field + Subscribe button. */
    lv_obj_t *row3 = lv_obj_create(content);
    lv_obj_set_size(row3, LV_PCT(100), WIN_PX(20));
    win_style_flat(row3);
    lv_obj_set_flex_flow(row3, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row3, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row3, WIN_PX(4), LV_PART_MAIN);

    s_mqtt_topic_ta = win_panel_field(row3, "topic", WIN_PX(160));
    lv_obj_set_flex_grow(s_mqtt_topic_ta, 1);
    lv_textarea_set_text(s_mqtt_topic_ta, st.topic);

    lv_obj_t *sub = win_panel_button(row3, "Sub", false, WIN_PX(46));
    lv_obj_add_event_cb(sub, win_mqtt_subscribe_cb, LV_EVENT_CLICKED, NULL);

    /* Row 4: Payload field + Publish button. */
    lv_obj_t *row4 = lv_obj_create(content);
    lv_obj_set_size(row4, LV_PCT(100), WIN_PX(20));
    win_style_flat(row4);
    lv_obj_set_flex_flow(row4, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row4, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row4, WIN_PX(4), LV_PART_MAIN);

    s_mqtt_payload_ta = win_panel_field(row4, "payload", WIN_PX(160));
    lv_obj_set_flex_grow(s_mqtt_payload_ta, 1);
    lv_textarea_set_text(s_mqtt_payload_ta, "{\"hi\":true}");

    lv_obj_t *pub = win_panel_button(row4, "Pub", true, WIN_PX(46));
    lv_obj_add_event_cb(pub, win_mqtt_publish_cb, LV_EVENT_CLICKED, NULL);

    /* Log area — fills the remaining height. */
    s_mqtt_log = lv_textarea_create(content);
    lv_obj_set_width(s_mqtt_log, LV_PCT(100));
    lv_obj_set_flex_grow(s_mqtt_log, 1);
    lv_obj_set_style_bg_color(s_mqtt_log, lv_color_hex(0x101016),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_mqtt_log, LV_OPA_100, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_mqtt_log, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_mqtt_log, WIN_COLOR_MICA_BORDER,
                                  LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_mqtt_log, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_radius(s_mqtt_log, WIN_PX(4), LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_mqtt_log, WIN_PX(4), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_mqtt_log, WIN_COLOR_TEXT_DIM,
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(s_mqtt_log, WIN_FONT_TINY, LV_PART_MAIN);
    /* LVGL 9 has no LV_STATE_READONLY — textarea may be editable by touch,
     * but win_mqtt_log_refresh() overwrites it on every update. */
    lv_obj_add_event_cb(s_mqtt_log, win_mqtt_log_click_cb, LV_EVENT_CLICKED,
                        NULL);

    win_mqtt_log_refresh();
}

/* ---- Bluetooth ---- */

/* One row of the scan result list. */
struct bt_row_ud {
    char addr[BT_ADDR_MAX];
    lv_obj_t *rssi_lbl;
};

static lv_obj_t *s_bt_status;
static lv_obj_t *s_bt_list;
static lv_obj_t *s_bt_scan_btn;
static lv_obj_t *s_bt_adv_btn;
static lv_obj_t *s_bt_info;
static lv_obj_t *s_bt_name_ta;
static lv_obj_t *s_bt_value_ta;
static lv_obj_t *s_bt_value_lbl;
static lv_obj_t *s_bt_log;
static lv_obj_t *s_bt_panel[3];
static lv_obj_t *s_bt_tabbtn[3];
static lv_timer_t *s_bt_timer;

/* Cached copy of the addresses currently shown, to avoid rebuilding the
 * list on every timer tick while the scan results did not change. */
static char s_bt_seen_addrs[BT_MAX_DEVICES][BT_ADDR_MAX];
static unsigned s_bt_seen_count;

static void win_bt_style_toggle(lv_obj_t *btn, bool on)
{
    if (btn == NULL) return;
    lv_obj_set_style_bg_color(btn,
                              on ? WIN_COLOR_ACCENT : WIN_COLOR_FIELD,
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, on ? LV_OPA_100 : LV_OPA_80, LV_PART_MAIN);
}

static void win_bt_refresh_status(void)
{
    if (s_bt_status == NULL) return;

    bt_status_t st;
    app_bt_get_status(&st);

    if (!st.host_ready) {
        lv_label_set_text(s_bt_status, LV_SYMBOL_BLUETOOTH " adapter starting");
    } else if (st.connected) {
        lv_label_set_text_fmt(s_bt_status,
                              LV_SYMBOL_BLUETOOTH " linked %s", st.peer_addr);
    } else {
        lv_label_set_text_fmt(s_bt_status,
                              LV_SYMBOL_BLUETOOTH " ready  adv:%s  scan:%s  %u dev",
                              st.advertising ? "on" : "off",
                              st.scanning ? "on" : "off",
                              st.device_count);
    }

    win_bt_style_toggle(s_bt_scan_btn, st.scanning);
    win_bt_style_toggle(s_bt_adv_btn, st.advertising);
}

static void win_bt_row_delete_cb(lv_event_t *event)
{
    struct bt_row_ud *ud = lv_event_get_user_data(event);
    if (ud != NULL) {
        lv_free(ud);
    }
}

static void win_bt_row_click_cb(lv_event_t *event)
{
    struct bt_row_ud *ud = lv_event_get_user_data(event);
    if (ud == NULL) return;

    app_bt_connect(ud->addr);
    win_bt_refresh_status();
}

static void win_bt_refresh_list(bool force)
{
    if (s_bt_list == NULL) return;

    bt_device_t devs[BT_MAX_DEVICES];
    unsigned n = app_bt_get_devices(devs, BT_MAX_DEVICES);

    bool changed = force || (n != s_bt_seen_count);
    for (unsigned i = 0; i < n && !changed; i++) {
        if (strcmp(devs[i].addr, s_bt_seen_addrs[i]) != 0) {
            changed = true;
        }
    }

    if (!changed) {
        /* Only refresh the RSSI text of the rows we already built. */
        uint32_t rows = lv_obj_get_child_count(s_bt_list);
        for (uint32_t r = 0; r < rows; r++) {
            lv_obj_t *row = lv_obj_get_child(s_bt_list, (int32_t)r);
            struct bt_row_ud *ud = lv_obj_get_user_data(row);
            if (ud == NULL || ud->rssi_lbl == NULL) continue;

            for (unsigned i = 0; i < n; i++) {
                if (strcmp(devs[i].addr, ud->addr) == 0) {
                    lv_label_set_text_fmt(ud->rssi_lbl, "%d dBm",
                                          (int)devs[i].rssi);
                    break;
                }
            }
        }
        return;
    }

    lv_obj_clean(s_bt_list);
    s_bt_seen_count = n;

    if (n == 0) {
        lv_obj_t *empty = win_content_label(s_bt_list, "press Scan to look for devices",
                                            WIN_COLOR_TEXT_MUTED);
        lv_label_set_long_mode(empty, LV_LABEL_LONG_DOT);
        lv_obj_set_width(empty, LV_PCT(100));
        return;
    }

    for (unsigned i = 0; i < n; i++) {
        snprintf(s_bt_seen_addrs[i], sizeof(s_bt_seen_addrs[i]), "%s",
                 devs[i].addr);

        lv_obj_t *row = lv_button_create(s_bt_list);
        lv_obj_set_size(row, LV_PCT(100), WIN_PX(18));
        lv_obj_set_style_bg_opa(row, LV_OPA_0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(row, WIN_COLOR_HOVER, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_50, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(row, WIN_PX(4), LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(row, WIN_PX(4), LV_PART_MAIN);
        lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, WIN_PX(4), LV_PART_MAIN);

        struct bt_row_ud *ud = lv_malloc(sizeof(*ud));
        if (ud == NULL) {
            lv_obj_delete(row);
            break;
        }
        memset(ud, 0, sizeof(*ud));
        snprintf(ud->addr, sizeof(ud->addr), "%s", devs[i].addr);

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, devs[i].name[0] ? devs[i].name : devs[i].addr);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_flex_grow(name, 1);
        lv_obj_set_style_text_color(name, WIN_COLOR_TEXT, LV_PART_MAIN);
        lv_obj_set_style_text_font(name, WIN_FONT_TINY, LV_PART_MAIN);

        lv_obj_t *rssi = lv_label_create(row);
        lv_label_set_text_fmt(rssi, "%d dBm", (int)devs[i].rssi);
        lv_obj_set_style_text_color(rssi, WIN_COLOR_TEXT_MUTED, LV_PART_MAIN);
        lv_obj_set_style_text_font(rssi, WIN_FONT_TINY, LV_PART_MAIN);
        ud->rssi_lbl = rssi;

        lv_obj_set_user_data(row, ud);
        lv_obj_add_event_cb(row, win_bt_row_click_cb, LV_EVENT_CLICKED, ud);
        lv_obj_add_event_cb(row, win_bt_row_delete_cb, LV_EVENT_DELETE, ud);
    }
}

static void win_bt_refresh_gatt(void)
{
    bt_status_t st;
    app_bt_get_status(&st);

    if (s_bt_info != NULL) {
        lv_label_set_text_fmt(s_bt_info, "addr %s\npeer %s",
                              st.own_addr[0] ? st.own_addr : "---",
                              st.connected ? st.peer_addr : "not connected");
    }
    if (s_bt_value_lbl != NULL) {
        lv_label_set_text_fmt(s_bt_value_lbl, "val: %s", st.gatt_value);
    }
}

static void win_bt_tick_cb(lv_timer_t *timer)
{
    (void)timer;
    win_bt_refresh_status();
    win_bt_refresh_list(false);
    win_bt_refresh_gatt();
}

static void win_bt_scan_cb(lv_event_t *event)
{
    (void)event;

    bt_status_t st;
    app_bt_get_status(&st);

    if (st.scanning) {
        app_bt_scan_stop();
    } else {
        app_bt_clear_devices();
        s_bt_seen_count = 0;
        s_bt_seen_addrs[0][0] = '\0';
        app_bt_scan_start();
    }

    win_bt_refresh_status();
    win_bt_refresh_list(true);
}

static void win_bt_adv_cb(lv_event_t *event)
{
    (void)event;

    bt_status_t st;
    app_bt_get_status(&st);

    if (st.advertising) {
        app_bt_advertise_stop();
    } else {
        app_bt_advertise_start();
    }

    win_bt_refresh_status();
}

static void win_bt_disc_cb(lv_event_t *event)
{
    (void)event;
    app_bt_disconnect();
    win_bt_refresh_status();
}

static void win_bt_clear_cb(lv_event_t *event)
{
    (void)event;
    app_bt_clear_devices();
    s_bt_seen_count = 0;
    s_bt_seen_addrs[0][0] = '\0';
    win_bt_refresh_list(true);
}

static void win_bt_name_cb(lv_event_t *event)
{
    (void)event;
    if (s_bt_name_ta == NULL) return;

    app_bt_set_name(lv_textarea_get_text(s_bt_name_ta));
    win_bt_refresh_status();
    win_bt_refresh_gatt();
}

static void win_bt_value_cb(lv_event_t *event)
{
    (void)event;
    if (s_bt_value_ta == NULL) return;

    app_bt_gatt_set_value(lv_textarea_get_text(s_bt_value_ta));
    win_bt_refresh_gatt();
}

static void win_bt_notify_cb(lv_event_t *event)
{
    (void)event;
    app_bt_gatt_notify();
    win_bt_refresh_gatt();
}

static void win_bt_log_click_cb(lv_event_t *event)
{
    (void)event;
    if (s_bt_log == NULL) return;

    char buf[2048] = {0};
    app_bt_get_log(buf, sizeof(buf));
    lv_textarea_set_text(s_bt_log, buf);
}

static void win_bt_apply_tab(long index)
{
    for (int i = 0; i < 3; i++) {
        if (s_bt_panel[i] != NULL) {
            lv_obj_set_hidden(s_bt_panel[i], index != i);
        }
        if (s_bt_tabbtn[i] != NULL) {
            lv_obj_set_style_bg_color(s_bt_tabbtn[i],
                                      index == i ? WIN_COLOR_ACCENT
                                                 : WIN_COLOR_FIELD,
                                      LV_PART_MAIN);
            lv_obj_set_style_bg_opa(s_bt_tabbtn[i],
                                    index == i ? LV_OPA_100 : LV_OPA_60,
                                    LV_PART_MAIN);
        }
    }
}

static void win_bt_tab_cb(lv_event_t *event)
{
    long index = (long)(intptr_t)lv_event_get_user_data(event);

    win_bt_apply_tab(index);

    if (index == 2) {
        win_bt_log_click_cb(NULL);
    }
}

static void win_bt_cleanup_cb(lv_event_t *event)
{
    (void)event;

    if (s_bt_timer != NULL) {
        lv_timer_delete(s_bt_timer);
        s_bt_timer = NULL;
    }

    s_bt_status = NULL;
    s_bt_list = NULL;
    s_bt_scan_btn = NULL;
    s_bt_adv_btn = NULL;
    s_bt_info = NULL;
    s_bt_name_ta = NULL;
    s_bt_value_ta = NULL;
    s_bt_value_lbl = NULL;
    s_bt_log = NULL;

    for (int i = 0; i < 3; i++) {
        s_bt_panel[i] = NULL;
        s_bt_tabbtn[i] = NULL;
    }
}

/* A tab page: transparent, full size, no scrolling. */
static lv_obj_t *win_bt_page(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    win_style_flat(page);
    lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(page, WIN_PX(4), LV_PART_MAIN);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_scrollable(page, false);
    return page;
}

static void win_content_bt(lv_obj_t *content)
{
    s_bt_status = NULL;
    s_bt_list = NULL;
    s_bt_scan_btn = NULL;
    s_bt_adv_btn = NULL;
    s_bt_info = NULL;
    s_bt_name_ta = NULL;
    s_bt_value_ta = NULL;
    s_bt_value_lbl = NULL;
    s_bt_log = NULL;
    s_bt_timer = NULL;
    s_bt_seen_count = 0;
    s_bt_seen_addrs[0][0] = '\0';

    bt_status_t st;
    app_bt_get_status(&st);

    /* Root: tab bar on top, the three pages below it. */
    lv_obj_t *root = lv_obj_create(content);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(root, 0, 0);
    win_style_flat(root);
    lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(root, WIN_PX(4), LV_PART_MAIN);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_scrollable(root, false);
    lv_obj_add_event_cb(root, win_bt_cleanup_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *bar = lv_obj_create(root);
    lv_obj_set_size(bar, LV_PCT(100), WIN_PX(20));
    win_style_flat(bar);
    lv_obj_set_style_pad_all(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(bar, WIN_PX(2), LV_PART_MAIN);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_scrollable(bar, false);

    static const char *tab_names[3] = { "Devices", "GATT", "Log" };
    for (int i = 0; i < 3; i++) {
        s_bt_tabbtn[i] = win_panel_button(bar, tab_names[i], false, WIN_PX(82));
        lv_obj_set_height(s_bt_tabbtn[i], WIN_PX(20));
        lv_obj_set_flex_grow(s_bt_tabbtn[i], 1);
        lv_obj_add_event_cb(s_bt_tabbtn[i], win_bt_tab_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }

    lv_obj_t *pages = lv_obj_create(root);
    lv_obj_set_width(pages, LV_PCT(100));
    lv_obj_set_flex_grow(pages, 1);
    win_style_flat(pages);
    lv_obj_set_style_pad_all(pages, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(pages, false);

    for (int i = 0; i < 3; i++) {
        s_bt_panel[i] = win_bt_page(pages);
        lv_obj_set_hidden(s_bt_panel[i], i != 0);
    }

    /* ---- Devices page ---- */
    lv_obj_t *devices = s_bt_panel[0];

    s_bt_status = lv_label_create(devices);
    lv_obj_set_size(s_bt_status, LV_PCT(100), WIN_PX(11));
    lv_obj_set_style_text_color(s_bt_status, WIN_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_bt_status, WIN_FONT_TINY, LV_PART_MAIN);

    lv_obj_t *controls = lv_obj_create(devices);
    lv_obj_set_size(controls, LV_PCT(100), WIN_PX(22));
    win_style_flat(controls);
    lv_obj_set_style_pad_all(controls, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(controls, WIN_PX(3), LV_PART_MAIN);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollable(controls, false);

    s_bt_scan_btn = win_panel_button(controls, "Scan", false, WIN_PX(52));
    lv_obj_add_event_cb(s_bt_scan_btn, win_bt_scan_cb, LV_EVENT_CLICKED, NULL);

    s_bt_adv_btn = win_panel_button(controls, "Adv", false, WIN_PX(46));
    lv_obj_add_event_cb(s_bt_adv_btn, win_bt_adv_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *disc = win_panel_button(controls, "Disc", false, WIN_PX(48));
    lv_obj_add_event_cb(disc, win_bt_disc_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *clear = win_panel_button(controls, "Clear", false, WIN_PX(46));
    lv_obj_add_event_cb(clear, win_bt_clear_cb, LV_EVENT_CLICKED, NULL);

    s_bt_list = lv_obj_create(devices);
    lv_obj_set_width(s_bt_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_bt_list, 1);
    win_style_flat(s_bt_list);
    lv_obj_set_style_pad_all(s_bt_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_bt_list, WIN_PX(2), LV_PART_MAIN);
    lv_obj_set_flex_flow(s_bt_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_bt_list, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scrollbar_mode(s_bt_list, LV_SCROLLBAR_MODE_AUTO);

    /* ---- GATT page ---- */
    lv_obj_t *gatt = s_bt_panel[1];

    s_bt_info = lv_label_create(gatt);
    lv_obj_set_size(s_bt_info, LV_PCT(100), WIN_PX(26));
    lv_obj_set_style_text_color(s_bt_info, WIN_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_bt_info, WIN_FONT_TINY, LV_PART_MAIN);

    lv_obj_t *name_row = lv_obj_create(gatt);
    lv_obj_set_size(name_row, LV_PCT(100), WIN_PX(20));
    win_style_flat(name_row);
    lv_obj_set_style_pad_all(name_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(name_row, WIN_PX(4), LV_PART_MAIN);
    lv_obj_set_flex_flow(name_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(name_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(name_row, false);

    s_bt_name_ta = win_panel_field(name_row, "device name", WIN_PX(176));
    lv_obj_set_flex_grow(s_bt_name_ta, 1);
    lv_textarea_set_text(s_bt_name_ta, st.name);

    lv_obj_t *name_set = win_panel_button(name_row, "Set", false, WIN_PX(46));
    lv_obj_add_event_cb(name_set, win_bt_name_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *value_row = lv_obj_create(gatt);
    lv_obj_set_size(value_row, LV_PCT(100), WIN_PX(20));
    win_style_flat(value_row);
    lv_obj_set_style_pad_all(value_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(value_row, WIN_PX(4), LV_PART_MAIN);
    lv_obj_set_flex_flow(value_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(value_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(value_row, false);

    s_bt_value_ta = win_panel_field(value_row, "characteristic value",
                                    WIN_PX(176));
    lv_obj_set_flex_grow(s_bt_value_ta, 1);
    lv_textarea_set_text(s_bt_value_ta, st.gatt_value);

    lv_obj_t *value_set = win_panel_button(value_row, "Set", true,
                                           WIN_PX(46));
    lv_obj_add_event_cb(value_set, win_bt_value_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *notify_row = lv_obj_create(gatt);
    lv_obj_set_size(notify_row, LV_PCT(100), WIN_PX(22));
    win_style_flat(notify_row);
    lv_obj_set_style_pad_all(notify_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(notify_row, WIN_PX(4), LV_PART_MAIN);
    lv_obj_set_flex_flow(notify_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(notify_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(notify_row, false);

    lv_obj_t *notify = win_panel_button(notify_row, "Notify", false,
                                        WIN_PX(62));
    lv_obj_add_event_cb(notify, win_bt_notify_cb, LV_EVENT_CLICKED, NULL);

    s_bt_value_lbl = lv_label_create(notify_row);
    lv_obj_set_flex_grow(s_bt_value_lbl, 1);
    lv_label_set_long_mode(s_bt_value_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_bt_value_lbl, WIN_COLOR_TEXT_MUTED,
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(s_bt_value_lbl, WIN_FONT_TINY, LV_PART_MAIN);

    /* ---- Log page ---- */
    lv_obj_t *log_page = s_bt_panel[2];

    s_bt_log = lv_textarea_create(log_page);
    lv_obj_set_width(s_bt_log, LV_PCT(100));
    lv_obj_set_flex_grow(s_bt_log, 1);
    lv_obj_set_style_bg_color(s_bt_log, lv_color_hex(0x101016),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bt_log, LV_OPA_100, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_bt_log, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_bt_log, WIN_COLOR_MICA_BORDER,
                                  LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_bt_log, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bt_log, WIN_PX(4), LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_bt_log, WIN_PX(4), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_bt_log, WIN_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_bt_log, WIN_FONT_TINY, LV_PART_MAIN);
    lv_obj_add_event_cb(s_bt_log, win_bt_log_click_cb, LV_EVENT_CLICKED,
                        NULL);

    /* Initial paint + the live refresh timer. */
    win_bt_apply_tab(0);
    win_bt_refresh_status();
    win_bt_refresh_list(true);
    win_bt_refresh_gatt();

    s_bt_timer = lv_timer_create(win_bt_tick_cb, 700, NULL);
}

/* ---- Camera ---- */

static lv_obj_t   *s_cam_status;
static lv_obj_t   *s_cam_img;
static lv_obj_t   *s_cam_auto_btn;
static lv_obj_t   *s_cam_bs_btn;
static lv_obj_t   *s_cam_tp_btn;
static lv_obj_t   *s_cam_sz_cont;
static lv_obj_t   *s_cam_log;
static lv_obj_t   *s_cam_panel[3];
static lv_obj_t   *s_cam_tabbtn[3];
static lv_timer_t *s_cam_timer;
static lv_img_dsc_t s_cam_dsc;
static bool        s_cam_auto;

static void win_cam_apply_tab(long index)
{
    for (int i = 0; i < 3; i++) {
        if (s_cam_panel[i] != NULL) {
            lv_obj_set_hidden(s_cam_panel[i], index != i);
        }
        if (s_cam_tabbtn[i] != NULL) {
            lv_obj_set_style_bg_color(s_cam_tabbtn[i],
                                      index == i ? WIN_COLOR_ACCENT
                                                 : WIN_COLOR_FIELD,
                                      LV_PART_MAIN);
            lv_obj_set_style_bg_opa(s_cam_tabbtn[i],
                                    index == i ? LV_OPA_100 : LV_OPA_60,
                                    LV_PART_MAIN);
        }
    }
}

static void win_cam_tab_cb(lv_event_t *event)
{
    long index = (long)(intptr_t)lv_event_get_user_data(event);
    win_cam_apply_tab(index);
    if (index == 2) {
        char buf[2048] = {0};
        app_camera_get_log(buf, sizeof(buf));
        if (s_cam_log != NULL) {
            lv_textarea_set_text(s_cam_log, buf);
        }
    }
}

static void win_cam_refresh_image(void)
{
    if (s_cam_img == NULL) {
        return;
    }

    const uint16_t *buf = NULL;
    int w = 0, h = 0;
    app_camera_get_last_frame(&buf, &w, &h);

    if (buf == NULL || w <= 0 || h <= 0) {
        return;
    }

    s_cam_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_cam_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_cam_dsc.header.w = (uint16_t)w;
    s_cam_dsc.header.h = (uint16_t)h;
    s_cam_dsc.data = (const uint8_t *)buf;
    s_cam_dsc.data_size = (uint32_t)w * (uint32_t)h * 2U;

    lv_image_set_src(s_cam_img, &s_cam_dsc);

    /* Scale to fit the image widget, preserving aspect ratio. */
    lv_coord_t ow = lv_obj_get_content_width(s_cam_img);
    lv_coord_t oh = lv_obj_get_content_height(s_cam_img);
    if (ow > 0 && oh > 0) {
        uint16_t zx = (uint16_t)(((uint32_t)ow * 256U) / (uint32_t)w);
        uint16_t zy = (uint16_t)(((uint32_t)oh * 256U) / (uint32_t)h);
        uint16_t zoom = zx < zy ? zx : zy;
        if (zoom == 0U) {
            zoom = 1U;
        }
        lv_image_set_scale(s_cam_img, zoom);
    }
    lv_obj_invalidate(s_cam_img);
}

static void win_cam_refresh_status(void)
{
    if (s_cam_status != NULL) {
        cam_status_t st;
        app_camera_get_status(&st);

        if (!st.inited && !st.test_pattern) {
            lv_label_set_text_fmt(s_cam_status,
                                  "camera not ready: %s",
                                  esp_err_to_name(st.last_error));
        } else if (st.test_pattern) {
            lv_label_set_text_fmt(s_cam_status,
                                  "test pattern  %dx%d  frames=%d",
                                  st.width, st.height, st.frame_count);
        } else {
            lv_label_set_text_fmt(s_cam_status,
                                  "live %dx%d  swap:%s  frames=%d",
                                  st.width, st.height,
                                  st.byte_swap ? "on" : "off",
                                  st.frame_count);
        }
    }

    cam_status_t st;
    app_camera_get_status(&st);

    win_bt_style_toggle(s_cam_auto_btn, s_cam_auto);
    win_bt_style_toggle(s_cam_bs_btn, st.byte_swap);
    win_bt_style_toggle(s_cam_tp_btn, st.test_pattern);

    /* Highlight the active frame-size button. */
    if (s_cam_sz_cont != NULL) {
        uint32_t n = lv_obj_get_child_count(s_cam_sz_cont);
        for (uint32_t i = 0; i < n; i++) {
            lv_obj_t *b = lv_obj_get_child(s_cam_sz_cont, (int32_t)i);
            int v = (int)(intptr_t)lv_obj_get_user_data(b);
            bool active = (v == st.frame_size);
            lv_obj_set_style_bg_color(b, active ? WIN_COLOR_ACCENT
                                                : WIN_COLOR_FIELD,
                                      LV_PART_MAIN);
            lv_obj_set_style_bg_opa(b, active ? LV_OPA_100 : LV_OPA_60,
                                    LV_PART_MAIN);
        }
    }
}

static void win_cam_capture_cb(lv_event_t *event)
{
    (void)event;
    app_camera_capture();
    win_cam_refresh_image();
    win_cam_refresh_status();
}

static void win_cam_auto_cb(lv_event_t *event)
{
    (void)event;
    s_cam_auto = !s_cam_auto;
    win_cam_refresh_status();
}

static void win_cam_size_cb(lv_event_t *event)
{
    int v = (int)(intptr_t)lv_event_get_user_data(event);
    app_camera_set_frame_size(v);
    win_cam_refresh_image();
    win_cam_refresh_status();
}

static void win_cam_bs_cb(lv_event_t *event)
{
    (void)event;
    cam_status_t st;
    app_camera_get_status(&st);
    app_camera_set_byte_swap(!st.byte_swap);
    app_camera_capture();
    win_cam_refresh_image();
    win_cam_refresh_status();
}

static void win_cam_tp_cb(lv_event_t *event)
{
    (void)event;
    cam_status_t st;
    app_camera_get_status(&st);
    app_camera_set_test_pattern(!st.test_pattern);
    win_cam_refresh_image();
    win_cam_refresh_status();
}

static void win_cam_reinit_cb(lv_event_t *event)
{
    (void)event;
    app_camera_reinit();
    app_camera_capture();
    win_cam_refresh_image();
    win_cam_refresh_status();
}

static void win_cam_log_click_cb(lv_event_t *event)
{
    (void)event;
    if (s_cam_log == NULL) return;
    char buf[2048] = {0};
    app_camera_get_log(buf, sizeof(buf));
    lv_textarea_set_text(s_cam_log, buf);
}

static void win_cam_tick_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_cam_auto) {
        app_camera_capture();
        win_cam_refresh_image();
    }
    win_cam_refresh_status();
}

static void win_cam_cleanup_cb(lv_event_t *event)
{
    (void)event;
    if (s_cam_timer != NULL) {
        lv_timer_delete(s_cam_timer);
        s_cam_timer = NULL;
    }
    s_cam_status = NULL;
    s_cam_img = NULL;
    s_cam_auto_btn = NULL;
    s_cam_bs_btn = NULL;
    s_cam_tp_btn = NULL;
    s_cam_sz_cont = NULL;
    s_cam_log = NULL;
    for (int i = 0; i < 3; i++) {
        s_cam_panel[i] = NULL;
        s_cam_tabbtn[i] = NULL;
    }
}

static void win_content_cam(lv_obj_t *content)
{
    s_cam_status = NULL;
    s_cam_img = NULL;
    s_cam_auto_btn = NULL;
    s_cam_bs_btn = NULL;
    s_cam_tp_btn = NULL;
    s_cam_sz_cont = NULL;
    s_cam_log = NULL;
    s_cam_timer = NULL;
    s_cam_auto = false;
    memset(&s_cam_dsc, 0, sizeof(s_cam_dsc));

    /* Make sure there is at least one frame to show (test pattern if the
     * real sensor is absent). */
    app_camera_capture();

    lv_obj_t *root = lv_obj_create(content);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(root, 0, 0);
    win_style_flat(root);
    lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(root, WIN_PX(4), LV_PART_MAIN);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(root, false);
    lv_obj_add_event_cb(root, win_cam_cleanup_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *bar = lv_obj_create(root);
    lv_obj_set_size(bar, LV_PCT(100), WIN_PX(20));
    win_style_flat(bar);
    lv_obj_set_style_pad_all(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(bar, WIN_PX(2), LV_PART_MAIN);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_scrollable(bar, false);

    static const char *tab_names[3] = { "View", "Settings", "Log" };
    for (int i = 0; i < 3; i++) {
        s_cam_tabbtn[i] = win_panel_button(bar, tab_names[i], false, WIN_PX(82));
        lv_obj_set_height(s_cam_tabbtn[i], WIN_PX(20));
        lv_obj_set_flex_grow(s_cam_tabbtn[i], 1);
        lv_obj_add_event_cb(s_cam_tabbtn[i], win_cam_tab_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }

    lv_obj_t *pages = lv_obj_create(root);
    lv_obj_set_width(pages, LV_PCT(100));
    lv_obj_set_flex_grow(pages, 1);
    win_style_flat(pages);
    lv_obj_set_style_pad_all(pages, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(pages, false);

    for (int i = 0; i < 3; i++) {
        s_cam_panel[i] = win_bt_page(pages);
        lv_obj_set_hidden(s_cam_panel[i], i != 0);
    }

    /* ---- View page ---- */
    lv_obj_t *view = s_cam_panel[0];

    s_cam_status = lv_label_create(view);
    lv_obj_set_size(s_cam_status, LV_PCT(100), WIN_PX(11));
    lv_obj_set_style_text_color(s_cam_status, WIN_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_cam_status, WIN_FONT_TINY, LV_PART_MAIN);

    s_cam_img = lv_image_create(view);
    lv_obj_set_width(s_cam_img, LV_PCT(100));
    lv_obj_set_flex_grow(s_cam_img, 1);
    lv_obj_set_style_bg_color(s_cam_img, lv_color_hex(0x101016), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_cam_img, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_cam_img, WIN_PX(4), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_cam_img, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_cam_img, WIN_COLOR_MICA_BORDER,
                                  LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_cam_img, LV_OPA_50, LV_PART_MAIN);
    lv_image_set_inner_align(s_cam_img, LV_ALIGN_CENTER);

    lv_obj_t *controls = lv_obj_create(view);
    lv_obj_set_size(controls, LV_PCT(100), WIN_PX(22));
    win_style_flat(controls);
    lv_obj_set_style_pad_all(controls, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(controls, WIN_PX(3), LV_PART_MAIN);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_scrollable(controls, false);

    lv_obj_t *cap = win_panel_button(controls, "Capture", true, WIN_PX(66));
    lv_obj_add_event_cb(cap, win_cam_capture_cb, LV_EVENT_CLICKED, NULL);

    s_cam_auto_btn = win_panel_button(controls, "Auto", false, WIN_PX(52));
    lv_obj_add_event_cb(s_cam_auto_btn, win_cam_auto_cb, LV_EVENT_CLICKED, NULL);

    /* ---- Settings page ---- */
    lv_obj_t *settings = s_cam_panel[1];

    lv_obj_t *sz_lbl = win_content_label(settings, "frame size",
                                         WIN_COLOR_TEXT_DIM);
    lv_obj_set_style_text_font(sz_lbl, WIN_FONT_TINY, LV_PART_MAIN);

    s_cam_sz_cont = lv_obj_create(settings);
    lv_obj_set_width(s_cam_sz_cont, LV_PCT(100));
    lv_obj_set_flex_grow(s_cam_sz_cont, 1);
    win_style_flat(s_cam_sz_cont);
    lv_obj_set_style_pad_all(s_cam_sz_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_cam_sz_cont, WIN_PX(2), LV_PART_MAIN);
    lv_obj_set_style_pad_column(s_cam_sz_cont, WIN_PX(2), LV_PART_MAIN);
    lv_obj_set_flex_flow(s_cam_sz_cont, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_scrollable(s_cam_sz_cont, true);

    int count = app_camera_frame_size_count();
    for (int i = 0; i < count; i++) {
        lv_obj_t *b = win_panel_button(s_cam_sz_cont,
                                       app_camera_frame_size_name(i),
                                       false, WIN_PX(110));
        lv_obj_set_height(b, WIN_PX(16));
        lv_obj_set_style_text_font(b, WIN_FONT_TINY, LV_PART_MAIN);
        lv_obj_set_user_data(b, (void *)(intptr_t)app_camera_frame_size_value(i));
        lv_obj_add_event_cb(b, win_cam_size_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)app_camera_frame_size_value(i));
    }

    lv_obj_t *row2 = lv_obj_create(settings);
    lv_obj_set_size(row2, LV_PCT(100), WIN_PX(22));
    win_style_flat(row2);
    lv_obj_set_style_pad_all(row2, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row2, WIN_PX(3), LV_PART_MAIN);
    lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
    lv_obj_set_scrollable(row2, false);

    s_cam_bs_btn = win_panel_button(row2, "Swap", false, WIN_PX(54));
    lv_obj_add_event_cb(s_cam_bs_btn, win_cam_bs_cb, LV_EVENT_CLICKED, NULL);

    s_cam_tp_btn = win_panel_button(row2, "Test", false, WIN_PX(54));
    lv_obj_add_event_cb(s_cam_tp_btn, win_cam_tp_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *reinit = win_panel_button(row2, "Reinit", false, WIN_PX(60));
    lv_obj_add_event_cb(reinit, win_cam_reinit_cb, LV_EVENT_CLICKED, NULL);

    /* ---- Log page ---- */
    lv_obj_t *log_page = s_cam_panel[2];

    s_cam_log = lv_textarea_create(log_page);
    lv_obj_set_width(s_cam_log, LV_PCT(100));
    lv_obj_set_flex_grow(s_cam_log, 1);
    lv_obj_set_style_bg_color(s_cam_log, lv_color_hex(0x101016), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_cam_log, LV_OPA_100, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_cam_log, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_cam_log, WIN_COLOR_MICA_BORDER,
                                  LV_PART_MAIN);
    lv_obj_set_style_border_opa(s_cam_log, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_radius(s_cam_log, WIN_PX(4), LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_cam_log, WIN_PX(4), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_cam_log, WIN_COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_cam_log, WIN_FONT_TINY, LV_PART_MAIN);
    lv_obj_add_event_cb(s_cam_log, win_cam_log_click_cb, LV_EVENT_CLICKED, NULL);

    /* Initial paint + live refresh timer. */
    win_cam_apply_tab(0);
    win_cam_refresh_image();
    win_cam_refresh_status();

    s_cam_timer = lv_timer_create(win_cam_tick_cb, 600, NULL);
}

/* ------------------------------------------------------------------ */
/* App windows                                                        */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* App lifecycle (the ui_page_base contract applied to app windows)   */
/*                                                                    */
/* Every application follows the six lifecycle phases of ui_views /   */
/* ui_manager / ui_page_base:                                         */
/*                                                                    */
/*   init    first open: one-time bring-up (the modules are already   */
/*           started from app_main, so this is NULL for every app)    */
/*   open    the window was created and its content built             */
/*   pause   the window was hidden (minimized): stop the live work    */
/*           (camera auto-capture, BT scan, honeycomb fling, ...)     */
/*   resume  the window is shown again: restart what pause stopped    */
/*   close   the window is being destroyed: stop every activity       */
/*   deinit  release the application resources (frame buffer, scan    */
/*           results) so the RAM is free for the next app             */
/*                                                                    */
/* The window LVGL tree itself is always deleted on close and rebuilt */
/* on the next open ("destroy on close"). Phases may be NULL, like    */
/* in ui_view_t.                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    void (*init)(void);
    void (*open)(void);
    void (*pause)(void);
    void (*resume)(void);
    void (*close)(void);
    void (*deinit)(void);
} win_app_lifecycle_t;

/* ---- Bluetooth ---- */

/* True when a scan was paused because the window was minimized. */
static bool s_bt_scan_paused;

static void win_bt_pause(void)
{
    if (s_bt_timer != NULL) {
        lv_timer_pause(s_bt_timer);
    }

    bt_status_t st;
    app_bt_get_status(&st);
    if (st.scanning) {
        app_bt_scan_stop();
        s_bt_scan_paused = true;
    }
}

static void win_bt_resume(void)
{
    if (s_bt_timer != NULL) {
        lv_timer_resume(s_bt_timer);
    }

    if (s_bt_scan_paused) {
        s_bt_scan_paused = false;
        app_bt_scan_start();
    }
}

static void win_bt_close(void)
{
    s_bt_scan_paused = false;

    bt_status_t st;
    app_bt_get_status(&st);
    if (st.scanning) {
        app_bt_scan_stop();
    }
}

static void win_bt_deinit(void)
{
    app_bt_clear_devices();
}

/* ---- Camera ---- */

static void win_cam_pause(void)
{
    if (s_cam_timer != NULL) {
        lv_timer_pause(s_cam_timer);
    }
}

static void win_cam_resume(void)
{
    if (s_cam_timer != NULL) {
        lv_timer_resume(s_cam_timer);
    }
}

static void win_cam_deinit(void)
{
    /* The big one: the RGB565 display buffer lives in PSRAM. */
    app_camera_release();
}

/* ---- UI honeycomb ---- */
/* pause: win_app_ui_pause (stop the fling); close: the DELETE handler of
 * ui_win_app_ui.c drops s_hc. Nothing else to release. */

static const win_app_lifecycle_t s_app_lifecycle[WIN_APP_COUNT] = {
    [WIN_APP_BT] = {
        .pause  = win_bt_pause,
        .resume = win_bt_resume,
        .close  = win_bt_close,
        .deinit = win_bt_deinit,
    },
    [WIN_APP_CAM] = {
        .pause  = win_cam_pause,
        .resume = win_cam_resume,
        .deinit = win_cam_deinit,
    },
    [WIN_APP_UI] = {
        .pause  = win_app_ui_pause,
        .resume = win_app_ui_resume,
        .close  = win_app_ui_pause,
    },
};

typedef enum {
    APP_INIT,
    APP_OPEN,
    APP_PAUSE,
    APP_RESUME,
    APP_CLOSE,
    APP_DEINIT,
} win_app_phase_t;

static void win_app_phase(win_app_id_t id, win_app_phase_t phase)
{
    if (id < 0 || id >= WIN_APP_COUNT) {
        return;
    }

    const win_app_lifecycle_t *lc = &s_app_lifecycle[id];
    void (*fn)(void) = NULL;

    switch (phase) {
        case APP_INIT:   fn = lc->init;   break;
        case APP_OPEN:   fn = lc->open;   break;
        case APP_PAUSE:  fn = lc->pause;  break;
        case APP_RESUME: fn = lc->resume; break;
        case APP_CLOSE:  fn = lc->close;  break;
        case APP_DEINIT: fn = lc->deinit; break;
    }

    if (fn != NULL) {
        fn();
    }
}

/* ------------------------------------------------------------------ */
/* Window actions                                                     */
/* ------------------------------------------------------------------ */

/* The index of the application a window belongs to, or -1. */
static int win_window_index(lv_obj_t *window)
{
    for (unsigned i = 0; i < WIN_APP_COUNT; i++) {
        if (s_windows[i] == window) {
            return (int)i;
        }
    }

    return -1;
}

/* Show or minimize a window; the app pauses while hidden and resumes when
 * it is shown again. */
static void win_window_set_visible(lv_obj_t *window, bool visible)
{
    lv_obj_set_hidden(window, !visible);

    int index = win_window_index(window);
    if (index >= 0) {
        win_app_phase((win_app_id_t)index, visible ? APP_RESUME : APP_PAUSE);
    }

    win_apps_dropdown_refresh();
}

static void win_window_minimize_cb(lv_obj_t *window, void *user_data)
{
    (void)user_data;
    win_window_set_visible(window, false);
}

static void win_window_maximize_cb(lv_obj_t *window, void *user_data)
{
    (void)user_data;
    win_window_toggle_maximize(window);
}

static void win_window_close_cb(lv_obj_t *window, void *user_data)
{
    (void)user_data;

    /* Find the application this window belongs to and stop its work. */
    int index = win_window_index(window);
    if (index >= 0) {
        s_windows[index] = NULL;
        win_app_phase((win_app_id_t)index, APP_CLOSE);
    }

    win_window_destroy(window);

    /* The window tree is gone; now release the app buffers. */
    if (index >= 0) {
        win_app_phase((win_app_id_t)index, APP_DEINIT);
    }

    win_apps_dropdown_refresh();
}

/* ------------------------------------------------------------------ */
/* Opening windows                                                    */
/* ------------------------------------------------------------------ */

static void win_window_open(win_app_id_t id)
{
    if (id < 0 || id >= WIN_APP_COUNT) {
        return;
    }

    win_start_dismiss();

    /* Already open: show it again and put it on top. */
    if (s_windows[id] != NULL) {
        win_window_set_visible(s_windows[id], true);
        win_window_bring_to_front(s_windows[id]);
        return;
    }

    static const win_window_actions_t actions = {
        .on_minimize = win_window_minimize_cb,
        .on_maximize = win_window_maximize_cb,
        .on_close = win_window_close_cb,
    };

    lv_obj_t *window = win_window_create(s_window_layer, win_app_get(id),
                                          &actions);
    if (window == NULL) {
        return;
    }

    /* Every window is placed a little bit lower than the previous one. */
    lv_coord_t offset = (lv_coord_t)((s_cascade % 4) * WIN_PX(16));
    lv_obj_set_pos(window, (WIN_SCREEN_W - WIN_WINDOW_W) / 2 + offset,
                   WIN_PX(16) + offset);
    s_cascade++;

    win_content_fill(win_window_get_content(window), id);

    /* The honeycomb needs the whole workspace, so it opens maximized. */
    if (id == WIN_APP_UI) {
        win_window_toggle_maximize(window);
    }

    s_windows[id] = window;

    /* A fresh window: init once, then open. */
    win_app_phase(id, APP_INIT);
    win_app_phase(id, APP_OPEN);

    win_apps_dropdown_refresh();
    win_window_bring_to_front(window);
}

/* Called by the start menu when an application is picked. */
static void win_start_launch_cb(win_app_id_t app, void *user_data)
{
    (void)user_data;

    win_window_open(app);
    ui_win_start_hide(s_start);
    win_style_start_button(s_start_button, false);
}

/* ------------------------------------------------------------------ */
/* View lifecycle                                                     */
/* ------------------------------------------------------------------ */

static void win_desktop_build(ui_page_t *page)
{
    s_root = ui_page_create_root(page, "WIN_DESKTOP", WIN_COLOR_WALL_TOP);
    if (s_root == NULL) {
        return;
    }

    /* The desktop uses absolute positioning instead of the page column. */
    lv_obj_set_layout(s_root, LV_LAYOUT_NONE);
    lv_obj_set_scrollable(s_root, false);
    s_cascade = 0;

    /* Clicking the desktop closes the start menu. */
    lv_obj_add_event_cb(s_root, win_desktop_click_cb, LV_EVENT_CLICKED, NULL);

    win_wallpaper_create(s_root);
    win_desktop_icons_create(s_root);

    /* The layer that holds the application windows. */
    s_window_layer = lv_obj_create(s_root);
    lv_obj_set_size(s_window_layer, LV_PCT(100),
                    WIN_SCREEN_H - WIN_TASKBAR_H);
    lv_obj_set_pos(s_window_layer, 0, 0);
    win_style_flat(s_window_layer);
    win_style_decoration(s_window_layer);

    /* The start menu sits on top of the windows. */
    s_start = ui_win_start_create(s_root, win_start_launch_cb, NULL);
    if (s_start != NULL) {
        lv_obj_set_pos(s_start, (WIN_SCREEN_W - WIN_START_W) / 2,
                       WIN_SCREEN_H - WIN_TASKBAR_H - WIN_START_H - 2);
    }

    win_taskbar_create(s_root);
}

void ui_win_desktop_init(void)
{
    ui_page_view_init(&s_page, win_desktop_build);
}

void ui_win_desktop_open(void)
{
    ui_page_view_open(&s_page, win_desktop_build);
}

void ui_win_desktop_pause(void)
{
    ui_page_view_pause(&s_page);
}

void ui_win_desktop_resume(void)
{
    ui_page_view_resume(&s_page);
}

void ui_win_desktop_close(void)
{
    ui_page_view_close(&s_page);
}

void ui_win_desktop_deinit(void)
{
    for (unsigned i = 0; i < WIN_APP_COUNT; i++) {
        s_windows[i] = NULL;
    }

    s_root = NULL;
    s_wallpaper = NULL;
    s_icon_layer = NULL;
    s_window_layer = NULL;
    s_start = NULL;
    s_taskbar = NULL;
    s_start_button = NULL;
    s_apps_dd = NULL;
    s_dd_map_count = 0;
    s_taskbar_center = NULL;
    s_clock = NULL;
    s_wifi_glyph = NULL;
    s_mqtt_dot = NULL;
    s_cascade = 0;

    /* Deleting the root also releases the start menu and open windows. */
    ui_page_view_deinit(&s_page);
}

/* ------------------------------------------------------------------ */
/* Live status (called from the LVGL task only)                       */
/* ------------------------------------------------------------------ */

void ui_win_desktop_set_clock(const char *text)
{
    if (s_clock != NULL && text != NULL) {
        lv_label_set_text(s_clock, text);
    }
}

void ui_win_desktop_set_status(bool wifi_ok, bool mqtt_ok)
{
    if (s_wifi_glyph != NULL) {
        lv_obj_set_style_text_color(s_wifi_glyph,
                                    wifi_ok ? WIN_COLOR_TEXT
                                            : WIN_COLOR_TEXT_MUTED,
                                    LV_PART_MAIN);
    }

    if (s_mqtt_dot != NULL) {
        lv_obj_set_style_bg_color(s_mqtt_dot,
                                  mqtt_ok ? WIN_COLOR_ACCENT_SOFT
                                          : WIN_COLOR_TEXT_MUTED,
                                  LV_PART_MAIN);
    }
}
