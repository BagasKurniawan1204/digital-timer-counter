#ifndef UI_H
#define UI_H

#include <TFT_eSPI.h>
#include "ct_timer.h"

// Initialize the display and draw static text
void ui_init();

// Repaint the home screen's static labels (header + row labels), clearing the
// screen first. Used by the HMI to restore HOME after a menu screen without
// re-running tft.init().
void ui_draw_home_static();

// Update the display with current counter values and frequency.
// When ch1_mode is CH1_MODE_TIMER the Channel 1 panel shows the countdown
// (remaining / setpoint / state) instead of count / preset / frequency, and
// the OUT1 indicator follows the timer output.
void ui_update_counter(int32_t counter1_current, int32_t counter1_preset, int32_t counter1_frequency,
					   int32_t counter2_current, int32_t counter2_preset, int32_t counter2_frequency,
					   Ch1Mode ch1_mode);

// Run the corner-tap calibration wizard. Leaves the screen blank and returns
// the 5 calibration values in calData so the caller can persist them.
void ui_calibrate_touch(uint16_t calData[5]);
bool ui_get_touch(uint16_t *x, uint16_t *y);

extern TFT_eSPI tft;

#endif // UI_H