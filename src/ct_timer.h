#ifndef CT_TIMER_H
#define CT_TIMER_H

/**
 * @file ct_timer.h
 * @brief Per-channel countdown timer (Autonics CT4S style)
 *
 * Either channel can operate as a pulse COUNTER (the original behaviour, handled
 * by CT_counter) or as a countdown TIMER. Only one of the two drives a channel's
 * output pin at any moment - the arbitration lives in counterTask().
 *
 * In TIMER mode, per channel:
 * - The PULSE pin acts as the gate. The countdown runs while the pin is
 *   asserted and pauses (retaining the remaining time) when released.
 * - The CTRL pin acts as the reset. An asserting edge clears the output and
 *   rearms the timer from any state.
 * - The output pin is OFF while counting and turns ON at time-up.
 *
 *      channel   gate (PULSE)   reset (CTRL)   output
 *      0 (CH1)   GPIO34         GPIO35         GPIO25
 *      1 (CH2)   GPIO32         GPIO33         GPIO26
 *
 * All four inputs sit behind external pull-up resistors, so they idle HIGH and
 * assert LOW (falling edge trigger). See TIMER_INPUT_ACTIVE_LOW in config.h.
 *
 * Two output modes control how the time-up output is cleared:
 * - TIMER_OUT_LATCH:     stays ON until the CTRL pin (or a web/serial reset)
 *                        clears it.
 * - TIMER_OUT_DELAY_OFF: clears automatically after delay_off_s seconds, then
 *                        re-runs immediately if the gate is still asserted.
 *
 * Channel indices are 0-based here, matching input_config_get() and
 * systemData.channel[]. Note getCounterInstance() is 1-based.
 */

#include <Arduino.h>

// =============================================================================
// ENUMERATIONS
// =============================================================================

// Operating mode of a channel
enum ChOpMode {
    CH_MODE_COUNTER,    // Pulse counter (default, uses PCNT + CT_counter)
    CH_MODE_TIMER       // Countdown timer (uses ct_timer_process)
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
    bool         output_state;  // Current output pin state
} ChTimerStatus_t;

// =============================================================================
// TIMER FUNCTIONS
// =============================================================================
// Every function takes a 0-based channel index and ignores out-of-range values.

/**
 * @brief Configure a channel's timer inputs and reset it to IDLE
 * Safe to call more than once.
 */
void ct_timer_init(uint8_t channel);

/**
 * @brief Advance a channel's countdown state machine
 * Called every counterTask tick (1 ms) while that channel is in timer mode.
 * This is the only writer of the channel's output pin in timer mode.
 */
void ct_timer_process(uint8_t channel);

/**
 * @brief Force a channel's timer back to IDLE with the output OFF
 */
void ct_timer_reset(uint8_t channel);

/**
 * @brief Set a channel's countdown time
 * @param seconds Countdown time, clamped to 0..TIMER_SETPOINT_MAX_S
 */
void ct_timer_set_setpoint(uint8_t channel, uint32_t seconds);

/**
 * @brief Set a channel's delay-off release time (TIMER_OUT_DELAY_OFF mode only)
 * @param seconds Delay-off time, clamped to 0..TIMER_SETPOINT_MAX_S
 */
void ct_timer_set_delay_off(uint8_t channel, uint32_t seconds);

/**
 * @brief Select how a channel's time-up output is released
 */
void ct_timer_set_out_mode(uint8_t channel, TimerOutMode mode);

/**
 * @brief Get a consistent snapshot of a channel's timer state
 */
ChTimerStatus_t ct_timer_get_status(uint8_t channel);

/**
 * @brief Get string name for a timer state (for debug/display)
 */
const char* ct_timer_state_to_string(TimerState state);

// =============================================================================
// CHANNEL MODE SWITCHING
// =============================================================================

/**
 * @brief Switch a channel between counter and timer operation
 *
 * COUNTER -> TIMER: pauses the channel's PCNT unit, reconfigures its two input
 *                   pins as plain GPIOs and arms the timer with the output OFF.
 * TIMER -> COUNTER: resets the timer, releases the output and rebuilds the
 *                   PCNT unit from the stored channel input config.
 */
void ch_set_mode(uint8_t channel, ChOpMode mode);

/**
 * @brief Get a channel's current operating mode
 */
ChOpMode ch_get_mode(uint8_t channel);

#endif // CT_TIMER_H
