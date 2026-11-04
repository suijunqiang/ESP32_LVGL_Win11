#ifndef APP_I18N_H
#define APP_I18N_H

#include "lvgl.h"

/* Noto Sans SC subsets generated with lv_font_conv. They cover every Chinese
 * character used by translations/zh-CN.yml plus ASCII, and fall back to
 * LVGL's built-in Source Han Sans for the LV_SYMBOL_* glyphs. */
LV_FONT_DECLARE(lv_font_zh_14);
LV_FONT_DECLARE(lv_font_zh_16);

/* Custom event fired on the active screen whenever the locale flips.
 * Allocated at runtime via lv_event_register_id() in app_i18n_init(); pages
 * store the returned value via `lv_obj_set_user_data` or a callback, then
 * listen on their own root object with `lv_obj_add_event_cb(obj, cb, code, NULL)`.
 *
 * Actually LVGL 9's global refresh convention is simply: callers broadcast
 * LV_EVENT_REFRESH on the screen, and pages that care register with that.
 * So we re-use LV_EVENT_REFRESH — it's a "user notify" event, exactly what
 * we need, and every widget naturally understands it.
 */

void app_i18n_init(void);
bool app_i18n_set_locale(const char *locale);
const char *app_i18n_current(void);

#endif /* APP_I18N_H */
