#include "ui_page_base.h"

lv_obj_t *ui_page_create_root(ui_page_t *page,
                              const char *name,
                              lv_color_t background)
{
    if (page == NULL) {
        return NULL;
    }

    if (page->root != NULL) {
        return page->root;
    }

    page->name = name;
    page->root = lv_obj_create(lv_scr_act());

    lv_obj_set_size(page->root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(page->root, 0, 0);
    lv_obj_set_style_bg_color(page->root, background, 0);
    lv_obj_set_style_border_width(page->root, 0, 0);
    lv_obj_set_style_radius(page->root, 0, 0);
    lv_obj_set_style_pad_all(page->root, 0, 0);
    lv_obj_set_style_pad_row(page->root, 0, 0);
    lv_obj_set_scrollable(page->root, false);

    page->initialized = true;
    return page->root;
}

void ui_page_show(ui_page_t *page)
{
    if (page != NULL && page->root != NULL) {
        lv_obj_set_hidden(page->root, false);
    }
}

void ui_page_hide(ui_page_t *page)
{
    if (page != NULL && page->root != NULL) {
        lv_obj_set_hidden(page->root, true);
    }
}

void ui_page_destroy(ui_page_t *page)
{
    if (page == NULL) {
        return;
    }

    if (page->root != NULL) {
        lv_obj_del(page->root);
    }

    page->root = NULL;
    page->name = NULL;
    page->initialized = false;
}

/* --------------------------------------------------------------------- */
/* Default view lifecycle bound to a page                                 */
/* --------------------------------------------------------------------- */

void ui_page_view_init(ui_page_t *page, ui_page_build_t build)
{
    if (page == NULL || page->initialized) {
        return;
    }

    if (build != NULL) {
        build(page);
    }

    /* ui_page_create_root() sets the flag; enforce it for safety. */
    page->initialized = (page->root != NULL);
}

void ui_page_view_open(ui_page_t *page, ui_page_build_t build)
{
    ui_page_view_init(page, build);
    ui_page_show(page);
}

void ui_page_view_pause(ui_page_t *page)
{
    ui_page_hide(page);
}

void ui_page_view_resume(ui_page_t *page)
{
    ui_page_show(page);
}

void ui_page_view_close(ui_page_t *page)
{
    ui_page_hide(page);
}

void ui_page_view_deinit(ui_page_t *page)
{
    ui_page_destroy(page);
}
