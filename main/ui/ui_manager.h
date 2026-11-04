#ifndef UI_MANAGER_H
#define UI_MANAGER_H

/*
 * Layer 1 - UI manager framework.
 *
 * Owns the navigation stack and drives the view lifecycle:
 *
 *   push   : current.pause -> new.init (once) -> stack push -> new.open
 *   pop    : old.close -> old.deinit -> back.resume
 *   switch : close + deinit every view on the stack, then a fresh push
 *
 * Pushing the view onto the stack *before* open() guarantees that lifecycle
 * callbacks always observe the correct active view (ui_current_view()).
 *
 * The framework contains no view specific code: views are described by
 * ui_view_t (ui_views.h) and published through ui_manager_register().
 */

#include "ui_views.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Register a view descriptor. Call before ui_manager_init(). */
bool ui_manager_register(const ui_view_t *view);

/* Boot the manager. ui_manager_init() pushes the Win11 desktop as root. */
bool ui_manager_init(void);

/* Boot with any view as the root. */
bool ui_manager_init_with(view_id_t root_id);

/* Close and deinit every view on the stack. */
void ui_manager_deinit(void);

/* Navigation. */
void ui_push_view(view_id_t id);
void ui_pop_view(void);
void ui_switch_view(view_id_t id);

/* Active view on top of the stack, or VIEW_NONE when the stack is empty. */
view_id_t ui_current_view(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
