/*
 * Application side view registry.
 *
 * This file is the only place that knows which view owns which id, which
 * makes ui_manager.c a reusable, view agnostic engine. Adding a view means
 * adding one UI_VIEW_DESC() line here and one UI_VIEW_DECLARE() in ui_views.h.
 */

#include "ui_manager.h"
#include "ui_views.h"

/* Bind a view id to the lifecycle functions declared by UI_VIEW_DECLARE(). */
#define UI_VIEW_DESC(id_, prefix_)          \
    static const ui_view_t prefix_##_desc = \
    {                                       \
        .id = (id_),                        \
        .name = #id_,                       \
        .init = prefix_##_init,             \
        .open = prefix_##_open,             \
        .pause = prefix_##_pause,           \
        .resume = prefix_##_resume,         \
        .close = prefix_##_close,           \
        .deinit = prefix_##_deinit,         \
    }

UI_VIEW_DESC(VIEW_WIN_DESKTOP, ui_win_desktop);

static const ui_view_t *const g_views[] = {
    &ui_win_desktop_desc,
};

void ui_views_register_all(void)
{
    for (unsigned i = 0; i < sizeof(g_views) / sizeof(g_views[0]); i++) {
        ui_manager_register(g_views[i]);
    }
}
