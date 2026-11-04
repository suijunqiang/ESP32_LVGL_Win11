#ifndef UI_VIEWS_H
#define UI_VIEWS_H

/*
 * Layer 2 - application domain: view ids, view descriptor, view registry.
 *
 * The UI framework is split into four layers:
 *
 *   ui_manager.[ch]    framework : view stack + lifecycle engine
 *                      (knows nothing about concrete views)
 *   ui_views.[ch]      domain    : view ids, ui_view_t descriptor, registry
 *   ui_page_base.[ch]  page base : shared page container + default lifecycle
 *   ui/win folder      examples  : one file per view, widgets only
 */

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Every view of the application. VIEW_NONE is never a valid navigation target. */
typedef enum {
    VIEW_NONE = -1,
    VIEW_WIN_DESKTOP = 0,
    VIEW_MAX
} view_id_t;

/* A single lifecycle phase. Every phase of a view is optional (may be NULL). */
typedef void (*ui_view_cb_t)(void);

/*
 * Immutable descriptor of a view.
 *
 * The manager keeps a pointer to this object and stores its own runtime
 * state (created flag, stack position) separately, so the descriptor must
 * stay valid - a `static const` object - for the whole lifetime of the app.
 */
typedef struct {
    view_id_t id;
    const char *name;      /* used for logging, e.g. "VIEW_WIN_DESKTOP" */
    ui_view_cb_t init;     /* build widgets, called once          */
    ui_view_cb_t open;     /* view became the active view         */
    ui_view_cb_t pause;    /* covered by a view pushed on top     */
    ui_view_cb_t resume;   /* revealed again by a pop             */
    ui_view_cb_t close;    /* leaving the stack for good          */
    ui_view_cb_t deinit;   /* release widgets, called once        */
} ui_view_t;

/*
 * Declare the six lifecycle functions implemented by "<prefix>_<phase>".
 * Their definitions live in the view's own .c file.
 */
#define UI_VIEW_DECLARE(prefix_)   \
    void prefix_##_init(void);     \
    void prefix_##_open(void);     \
    void prefix_##_pause(void);    \
    void prefix_##_resume(void);   \
    void prefix_##_close(void);    \
    void prefix_##_deinit(void)

UI_VIEW_DECLARE(ui_win_desktop);

/*
 * Publish every view of the application to the manager (see ui_views.c).
 * Must be called once, before ui_manager_init().
 */
void ui_views_register_all(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
