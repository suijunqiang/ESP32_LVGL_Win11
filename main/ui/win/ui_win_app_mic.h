#ifndef UI_WIN_APP_MIC_H
#define UI_WIN_APP_MIC_H

/*
 * Content of tab 5 of the "UI" demo application: the microphone monitor.
 *
 * A neon audio-workstation view: a microphone icon inside two rings of radial
 * spectrum bars, a rotating sonar arc, expanding ripple rings and a live
 * waveform strip with an input-level readout. Everything is drawn into two
 * RGB565 canvases (no images, no external fonts), driven by a simulated
 * level so the effect runs without a real audio input.
 *
 * Only the visible page animates (win_app_ui_pause() stops it).
 */

#include "lvgl.h"

lv_obj_t *ui_mic_create(lv_obj_t *page);
void ui_mic_start(void);
void ui_mic_stop(void);
void ui_mic_relayout(void);

#endif /* UI_WIN_APP_MIC_H */
