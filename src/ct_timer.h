#ifndef CT_TIMER_H
#define CT_TIMER_H

/**
 * @file ct_timer.h
 * @brief Channel 1 countdown timer (Autonics CT4S style)
 *
 * Channel 1 can operate either as a pulse COUNTER (the original behaviour,
 * handled by CT_counter) or as a countdown TIMER. Only one of the two drives
 * OUTPUT_CH1_PIN at any moment - the arbitration lives in counterTask().
 *
 * In TIMER mode:
 * - COUNTER_CH1_PULSE_PIN acts as the gate. The countdown runs while the pin
 *   is asserted and pauses (retaining the remaining time) when released.
 * - COUNTER_CH1_CTRL_PIN acts as the reset. An asserting edge clears the
 *   output and rearms the timer from any state.
 * - OUTPUT_CH1_PIN is OFF while counting and turns ON at time-up.
 *
 * Both inputs are on GPIO34/35, which are input-only pins wired to external
 * pull-up resistors, so they idle HIGH and assert LOW (falling edge trigger).
 * See CH1_TIMER_ACTIVE_LOW in config.h.
 *
 * Two output modes control how the time-up output is cleared:
 * - TIMER_OUT_LATCH:     stays ON until the CTRL pin (or a web/serial reset)
 *                        clears it.
 * - TIMER_OUT_DELAY_OFF: clears automatically after delay_off_s seconds, then
 *                        re-runs immediately if the gate is still asserted.
 */

#include <Arduino.h>

// =============================================================================
// ENUMERATIONS
// =============================================================================

// Operating mode of Channel 1
enum Ch1Mode {
    CH1_MODE_COUNTER,   // Pulse counter (default, uses PCNT + CT_counter)
    CH1_MODE_TIMER      // Countdown timer (uses ct_timer_process)
};

// How the time-up output is released
enum TimerOutMode {
    TIMER_OUT_LATCH,        // Output holds until CTRL pin / manual reset
    TIMER_OUT_DELAY_OFF     // Output releases automatically after delay_off_s
};

// Countdown state machine states
enum TimerState {
    TIMER_IDLE,         // Armed, waiting for gate. remaining = setpoint
    TIMER_RUNNING,      // Counting down, gate asserted, output OFF
    TIMER_PAUSED,       // Gate released mid-count, remaining retained
    TIMER_OUT           // Time-up reached, output ON
};

// =============================================================================
// STATUS SNAPSHOT
// =============================================================================
typedef struct {
    TimerState   state;
    uint32_t     remaining_s;   // Seconds left in the countdown
    uint32_t     setpoint_s;    // Configured countdown time
    uint32_t     delay_off_s;   // Configured delay-off time
    TimerOutMode out_mode;      // Latch or delay-off
    bool         output_state;  // Current OUTPUT_CH1_PIN state
} Ch1TimerStatus_t;

// =============================================================================
// TIMER FUNCTIONS
// =============================================================================

/**
 * @brief Configure the CH1 timer inputs and reset to IDLE
 * Safe to call more than once.
 */
void ct_timer_init();

/**
 * @brief Advance the countdown state machine
 * Called every counterTask tick (1 ms) while CH1 is in timer mode.
 * This is the only writer of OUTPUT_CH1_PIN in timer mode.
 */
void ct_timer_process();

/**
 * @brief Force the timer back to IDLE with the output OFF
 */
void ct_timer_reset();

/**
 * @brief Set the countdown time
 * @param seconds Countdown time, clamped to 0..TIMER_SETPOINT_MAX_S
 */
void ct_timer_set_setpoint(uint32_t seconds);

/**
 * @brief Set the delay-off release time (TIMER_OUT_DELAY_OFF mode only)
 * @param seconds Delay-off time, clamped to 0..TIMER_SETPOINT_MAX_S
 */
void ct_timer_set_delay_off(uint32_t seconds);

/**
 * @brief Select how the time-up output is released
 */
void ct_timer_set_out_mode(TimerOutMode mode);

/**
 * @brief Get a consistent snapshot of the timer state
 */
Ch1TimerStatus_t ct_timer_get_status();

/**
 * @brief Get string name for a timer state (for debug/display)
 */
const char* ct_timer_state_to_string(TimerState state);

// =============================================================================
// CHANNEL 1 MODE SWITCHING
// =============================================================================

/**
 * @brief Switch Channel 1 between counter and timer operation
 *
 * COUNTER -> TIMER: pauses PCNT unit 0, reconfigures GPIO34/35 as plain
 *                   inputs and arms the timer with the output OFF.
 * TIMER -> COUNTER: resets the timer, releases the output and rebuilds the
 *                   PCNT unit from the stored channel input config.
 */
void ch1_set_mode(Ch1Mode mode);

/**
 * @brief Get the current Channel 1 operating mode
 */
Ch1Mode ch1_get_mode();

#endif // CT_TIMER_H
