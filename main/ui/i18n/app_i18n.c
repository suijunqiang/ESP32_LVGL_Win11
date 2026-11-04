/*
 * app_i18n.c — locale + font switch for the Win11 desktop.
 *
 * Wraps the lv_i18n compile-time generated translation table. On startup we
 * initialise lv_i18n and select `en-US` (English). The font set is chosen
 * from the active locale:
 *
 *   en-US → WIN_FONT_* (Montserrat 10/12/16, Latin only)
 *   zh-CN → WIN_FONT_* (Noto Sans SC 14/16, full Chinese coverage)
 *
 * Locale changes flip the font pointers then send `LV_EVENT_REFRESH` on the
 * active screen so pages that cache translated strings (e.g. a label set
 * once at build time) can re-set them.
 */

#include "app_i18n.h"

#include <string.h>

#include "lv_i18n.h"
#include "lvgl.h"
#include "ui_win_theme.h"

/* ---- font pointer definitions (previously macros in ui_win_theme.h) ---- */

const lv_font_t *WIN_FONT_TINY;
const lv_font_t *WIN_FONT_SMALL;
const lv_font_t *WIN_FONT_BODY;
const lv_font_t *WIN_FONT_LARGE;
const lv_font_t *WIN_FONT_XL;

/* ---- helpers --------------------------------------------------------- */

const char *app_i18n_current(void)
{
    const char *cur = lv_i18n_get_current_locale();
    return cur ? cur : "en-US";
}

static bool is_zh(const char *locale)
{
    return locale && locale[0] == 'z' && locale[1] == 'h';
}

/* ---- theme font switch (declared in ui_win_theme.h) ------------------ */

void ui_win_theme_set_fonts(bool zh)
{
    if (zh) {
        WIN_FONT_TINY  = &lv_font_zh_14;
        WIN_FONT_SMALL = &lv_font_zh_14;
        WIN_FONT_BODY  = &lv_font_zh_14;
        WIN_FONT_LARGE = &lv_font_zh_16;
        WIN_FONT_XL    = &lv_font_zh_16;
    } else {
        WIN_FONT_TINY  = &lv_font_montserrat_10;
        WIN_FONT_SMALL = &lv_font_montserrat_12;
        WIN_FONT_BODY  = &lv_font_montserrat_12;
        WIN_FONT_LARGE = &lv_font_montserrat_16;
        WIN_FONT_XL    = &lv_font_montserrat_16;
    }
}

/* ---- public API ------------------------------------------------------- */

void app_i18n_init(void)
{
    lv_i18n_init(lv_i18n_language_pack);
    lv_i18n_set_locale("en-US");
    ui_win_theme_set_fonts(false);
}

bool app_i18n_set_locale(const char *locale)
{
    if (locale == NULL) {
        return false;
    }

    const char *cur = app_i18n_current();
    if (strcmp(cur, locale) == 0) {
        return true;                         /* no-op */
    }

    int rc = lv_i18n_set_locale(locale);
    if (rc != 0) {
        return false;                        /* unknown locale */
    }

    ui_win_theme_set_fonts(is_zh(locale));

    /* Broadcast a refresh on the screen so every page can re-apply text. */
    lv_obj_t *scr = lv_scr_act();
    if (scr) {
        lv_obj_send_event(scr, LV_EVENT_REFRESH, NULL);
    }
    lv_obj_invalidate(scr);
    return true;
}
