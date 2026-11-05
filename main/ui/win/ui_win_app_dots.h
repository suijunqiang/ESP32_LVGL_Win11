#ifndef UI_WIN_APP_DOTS_H
#define UI_WIN_APP_DOTS_H

/*
 * Content of tab 4 of the "UI" demo application: the dot-array view.
 *
 * A big preview of a point cloud above a 3 x 3 grid of small shape previews.
 * Every tile renders the same nine generator functions that the reference
 * design uses (breathe, scan, spin, swirl, lattice, shells, orbit, weave,
 * shape), but at panel resolution: the point sets are drawn into RGB565
 * canvases with a max blend, so a dot never dims a dot that is already
 * brighter.
 *
 * Tap a tile to switch the shape, tap the preview to step to the next one.
 * Only the visible page animates (win_app_ui_pause() stops it).
 */

#include "lvgl.h"

/* Build the page. Returns the page itself (the caller owns it). */
lv_obj_t *ui_dots_create(lv_obj_t *page);

/* Start / stop the animation timer (tab shown / hidden). */
void ui_dots_start(void);
void ui_dots_stop(void);

/* Recompute the tile geometry from the current page size. */
void ui_dots_relayout(void);

#endif /* UI_WIN_APP_DOTS_H */
