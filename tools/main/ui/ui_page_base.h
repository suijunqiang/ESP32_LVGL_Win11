#ifndef UI_PAGE_BASE_H
#define UI_PAGE_BASE_H

/*
 * Shared page container for the views.
 *
 * A page is a full screen container owned by a view. It provides the
 * common visibility and destruction operations, plus a default
 * implementation of the six view lifecycle functions.
 *
 * This is the trimmed variant used by the Win11 demo: the navigation
 * framework (view ids, stack, headers) is not needed here, so only the
 * root container and the lifecycle helpers remain.
 *
 * Usage inside a view file:
 *
 *   static ui_page_t s_page;
 *
 *   static void build(ui_page_t *page)
 *   {
 *       ui_page_create_root(page, "WIN_DESKTOP", UI_COLOR_BG);
 *       ...
 *   }
 *
 *   UI_VIEW_DEFINE(ui_win_desktop, &s_page, build)
 */

#include "lvgl.h"
#include <stdbool.h>

typedef struct ui_page {
    lv_obj_t *root;      /* full screen container                     */
    const char *name;
    bool initialized;
} ui_page_t;

/* Create the root container for a page. */
lv_obj_t *ui_page_create_root(ui_page_t *page,
                              const char *name,
                              lv_color_t background);

/* Show or hide the page root. */
void ui_page_show(ui_page_t *page);
void ui_page_hide(ui_page_t *page);

/* Delete all LVGL objects owned by the page. */
void ui_page_destroy(ui_page_t *page);

/* --------------------------------------------------------------------- */
/* Default view lifecycle bound to a page                                 */
/* --------------------------------------------------------------------- */

/* Builds the widgets of a page, called at most once per view instance. */
typedef void (*ui_page_build_t)(ui_page_t *page);

/*
 * Generic lifecycle, safe to call at any time:
 *   init  -> run `build` once (page must call ui_page_create_root())
 *   open  -> init when needed, then show
 *   pause / close -> hide
 *   resume         -> show
 *   deinit        -> delete every widget of the page
 */
void ui_page_view_init(ui_page_t *page, ui_page_build_t build);
void ui_page_view_open(ui_page_t *page, ui_page_build_t build);
void ui_page_view_pause(ui_page_t *page);
void ui_page_view_resume(ui_page_t *page);
void ui_page_view_close(ui_page_t *page);
void ui_page_view_deinit(ui_page_t *page);

/*
 * Implement the six functions declared by the view header.
 * No trailing semicolon needed.
 *
 *   UI_VIEW_DEFINE(ui_win_desktop, &s_page, build)
 */
#define UI_VIEW_DEFINE(prefix_, page_, build_)                              \
    void prefix_##_init(void)   { ui_page_view_init((page_), (build_)); }   \
    void prefix_##_open(void)   { ui_page_view_open((page_), (build_)); }   \
    void prefix_##_pause(void)  { ui_page_view_pause((page_)); }            \
    void prefix_##_resume(void) { ui_page_view_resume((page_)); }           \
    void prefix_##_close(void)  { ui_page_view_close((page_)); }            \
    void prefix_##_deinit(void) { ui_page_view_deinit((page_)); }

#endif /* UI_PAGE_BASE_H */
