#ifndef UI_H
#define UI_H

#include <TFT_eSPI.h>

// Initialize the display and draw static text
void ui_init();

// Update the display with current counter values and frequency
void ui_update_counter(int32_t counter1_current, int32_t counter1_preset, int32_t counter1_frequency,
					   int32_t counter2_current, int32_t counter2_preset, int32_t counter2_frequency);

// Calibration and touch handling (optional)
void ui_calibrate_touch();
bool ui_get_touch(uint16_t *x, uint16_t *y);

extern TFT_eSPI tft;

#endif // UI_H