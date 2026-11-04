/*
 * screenshot.h — LVGL screen capture helper.
 *
 * Call screenshot_request() from any task.
 * The LVGL task must call screenshot_check_and_do(disp) after
 * lv_timer_handler() so the snapshot runs on the LVGL task context.
 */
#pragma once

#include "lvgl.h"

void screenshot_request(void);
void screenshot_check_and_do(lv_display_t *disp);

/* TEMP-VERIFY: mirror every flushed band so the capture shows exactly what the
 * panel receives (the ground truth), independent of lv_snapshot_take. */
void screenshot_mirror_area(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                            const void *px_map, size_t stride);
void screenshot_mirror_begin(void);
