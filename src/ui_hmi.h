#ifndef UI_HMI_H
#define UI_HMI_H

/**
 * @file ui_hmi.h
 * @brief Touchscreen HMI for the TFT panel
 *
 * Turns the display into a standalone operator interface exposing everything
 * the web page offers: channel settings, the CH1 countdown timer settings, the
 * manual actions (counter/timer reset, relay ON/OFF/AUTO) and the system
 * actions (save to flash, factory reset, touch calibration).
 *
 * The whole HMI runs from loop() on Core 1, which is already the only writer of
 * the TFT, so no extra task or display mutex is needed. Every setting is
 * applied through the same functions the web handlers use, so changes made here
 * are picked up by the web UI automatically on the next state push.
 *
 * Screen map:
 *   HOME --[MENU]--> MENU --+-> CH1 CFG --+-> numeric keypad
 *    ^             ^        |             +-> option list
 *    |             |        +-> CH2 CFG --+
 *    |             |        +-> ACTIONS
 *    |             |        +-> SYSTEM ----> confirm (factory reset)
 *    +---[BACK]----+
 */

#include <Arduino.h>

/**
 * @brief Load the touch calibration and show the home screen
 *
 * Runs the corner-tap calibration wizard when NVS holds no calibration (first
 * boot) and stores the result. Must be called after ui_init().
 */
void ui_hmi_init();

/**
 * @brief Poll the touchscreen and service the active screen
 * Call once per loop() iteration.
 */
void ui_hmi_process();

/**
 * @brief True while the home screen is showing
 *
 * main.cpp uses this to decide whether the periodic counter/timer value
 * refresh should paint, so it cannot draw over a menu screen.
 */
bool ui_hmi_is_home();

#endif // UI_HMI_H
