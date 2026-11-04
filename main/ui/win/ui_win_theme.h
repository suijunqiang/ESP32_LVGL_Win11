#ifndef UI_WIN_THEME_H
#define UI_WIN_THEME_H

/*
 * Windows 11 look and feel, adapted for the 320x240 panel on this board.
 *
 * The reference design of this demo is 420x320 px. The panel here is
 * 320x240, so every metric of the design is scaled by 3/4 with WIN_PX().
 * Using one uniform scale keeps circles round and icons square; the design
 * becomes 315x240 and the remaining 5 px are simply covered by the
 * percentage sized wallpaper and taskbar.
 *
 *   - the "Mica" material: a slightly translucent dark surface with a thin
 *     light border, used by the start menu and by the app windows.
 *   - the "Accent" blue (#0078D4) used for the start button, the selection
 *     highlights and the focus rings.
 *   - the "Bloom" wallpaper gradient used by the desktop.
 *
 * Everything is header only, the helpers are `static inline` so a view gets
 * them without extra linkage, exactly like ui_theme.h does.
 */

#include "lvgl.h"

/* ------------------------------------------------------------------ */
/* Scale                                                              */
/* ------------------------------------------------------------------ */

/* The design canvas of the original desktop demo. */
#define WIN_DESIGN_W            420
#define WIN_DESIGN_H            320

/* 420x320 -> 315x240, i.e. 3/4, rounded to the nearest pixel. */
#define WIN_PX(v)               ((lv_coord_t)(((v) * 3 + 2) / 4))

/* The real panel, in landscape. */
#define WIN_SCREEN_W            320
#define WIN_SCREEN_H            240

/* ------------------------------------------------------------------ */
/* Palette                                                            */
/* ------------------------------------------------------------------ */

/* Windows accent colours. */
#define WIN_COLOR_ACCENT        lv_color_hex(0x0078D4)
#define WIN_COLOR_ACCENT_DARK   lv_color_hex(0x005FB7)
#define WIN_COLOR_ACCENT_SOFT   lv_color_hex(0x4CC2FF)
#define WIN_COLOR_ACCENT_2      lv_color_hex(0x7C5CFF)

/* Text. */
#define WIN_COLOR_TEXT          lv_color_hex(0xFFFFFF)
#define WIN_COLOR_TEXT_DIM      lv_color_hex(0xBFC7D1)
#define WIN_COLOR_TEXT_MUTED    lv_color_hex(0x8A929E)

/* Surfaces. */
#define WIN_COLOR_TASKBAR       lv_color_hex(0x1C1C24)
#define WIN_COLOR_TASKBAR_EDGE  lv_color_hex(0x2E2E38)
#define WIN_COLOR_MICA          lv_color_hex(0x2B2B35)
#define WIN_COLOR_MICA_BORDER   lv_color_hex(0x4A4A57)
#define WIN_COLOR_FIELD         lv_color_hex(0x35353F)
#define WIN_COLOR_CARD          lv_color_hex(0x3A3A46)
#define WIN_COLOR_WINDOW        lv_color_hex(0x2E2E38)
#define WIN_COLOR_WINDOW_TITLE  lv_color_hex(0x35353F)
#define WIN_COLOR_HOVER         lv_color_hex(0x4A4A57)
#define WIN_COLOR_CLOSE         lv_color_hex(0xC42B1C)

/* The "Bloom" wallpaper. */
#define WIN_COLOR_WALL_TOP      lv_color_hex(0x0A2A63)
#define WIN_COLOR_WALL_BOTTOM   lv_color_hex(0x4B1D7E)
#define WIN_COLOR_BLOB_BLUE     lv_color_hex(0x2E7BE8)
#define WIN_COLOR_BLOB_TEAL     lv_color_hex(0x18B7C9)
#define WIN_COLOR_BLOB_PURPLE   lv_color_hex(0x8B3BD1)

/* ------------------------------------------------------------------ */
/* Typography                                                         */
/* ------------------------------------------------------------------ */

/* Fonts are one step smaller than the design sizes: on a 320 px wide panel
 * the original 12/14/16/20 px text would eat too much of the layout. */
#define WIN_FONT_TINY           (&lv_font_montserrat_10)
#define WIN_FONT_SMALL          (&lv_font_montserrat_12)
#define WIN_FONT_BODY           (&lv_font_montserrat_12)
#define WIN_FONT_LARGE          (&lv_font_montserrat_16)
#define WIN_FONT_XL             (&lv_font_montserrat_16)

/* ------------------------------------------------------------------ */
/* Metrics (design values, already scaled)                            */
/* ------------------------------------------------------------------ */

/* Taskbar. */
#define WIN_TASKBAR_H           WIN_PX(44)
#define WIN_TASKBTN_SIZE        WIN_PX(30)
#define WIN_TASKBTN_GAP         WIN_PX(2)
#define WIN_TRAY_ICON           WIN_PX(14)
#define WIN_TASKBAR_RADIUS      WIN_PX(6)

/* Desktop icons. */
#define WIN_DESKTOP_ICON_SIZE   WIN_PX(34)
#define WIN_DESKTOP_ICON_GAP    WIN_PX(12)
#define WIN_DESKTOP_COL_W       WIN_PX(76)

/* Start menu. */
#define WIN_START_W             WIN_PX(300)
#define WIN_START_H             WIN_PX(262)
#define WIN_START_MARGIN        WIN_PX(6)
#define WIN_START_APP_SIZE      WIN_PX(32)
#define WIN_START_COLS          3

/* App windows. */
#define WIN_WINDOW_W            WIN_PX(262)
#define WIN_WINDOW_H            WIN_PX(162)
#define WIN_WINDOW_TITLE_H      WIN_PX(30)
#define WIN_WINDOW_CAPTION_W    WIN_PX(26)

/* Corner radii. */
#define WIN_RADIUS_PANEL        WIN_PX(8)
#define WIN_RADIUS_ICON         WIN_PX(7)
#define WIN_RADIUS_FIELD        WIN_PX(4)

/* ------------------------------------------------------------------ */
/* Style helpers                                                      */
/* ------------------------------------------------------------------ */

/*
 * The Mica material: a translucent panel with a hairline border and a
 * soft drop shadow. `opa` controls how much of the wallpaper shows
 * through, which is what makes it look like real acrylic.
 */
static inline void win_style_mica(lv_obj_t *obj, lv_color_t color, lv_opa_t opa)
{
    lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, opa, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, WIN_COLOR_MICA_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_opa(obj, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, WIN_RADIUS_PANEL, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(obj, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(obj, WIN_PX(24), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_shadow_offset_y(obj, WIN_PX(5), LV_PART_MAIN);
}

/* A flat, borderless, non scrollable container. */
static inline void win_style_flat(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(obj, false);
}

/*
 * Pure decoration.
 *
 * A plain LVGL object is clickable by default, so a decorative shape would
 * swallow the click of the widget below it. Every object that only paints
 * something is marked with this helper.
 */
static inline void win_style_decoration(lv_obj_t *obj)
{
    lv_obj_set_clickable(obj, false);
}

/* A rounded "pill" used by the search box and the weather widget. */
static inline void win_style_field(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, WIN_COLOR_FIELD, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, WIN_COLOR_MICA_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_opa(obj, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, WIN_RADIUS_FIELD, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
}

/*
 * The blue accent glow of the start button. Windows 11 paints a soft
 * halo around the logo when the menu is open, so the button is drawn as
 * a rounded square with a blur like shadow.
 */
static inline void win_style_start_button(lv_obj_t *obj, bool active)
{
    lv_obj_set_style_bg_color(obj, WIN_COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, active ? LV_OPA_100 : LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, WIN_TASKBTN_SIZE / 2, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(obj, WIN_COLOR_ACCENT_SOFT, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(obj, WIN_PX(16), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(obj, active ? LV_OPA_50 : LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_shadow_offset_y(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_spread(obj, 2, LV_PART_MAIN);
}

#endif /* UI_WIN_THEME_H */
