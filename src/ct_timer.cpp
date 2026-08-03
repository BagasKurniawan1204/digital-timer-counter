/**
 * @file ct_timer.cpp
 * @brief Channel 1 countdown timer implementation
 */

#include "ct_timer.h"
#include "config.h"
#include "globals.h"
#include "counter_operation.h"
#include "input_config.h"
#include "CT_counter.h"
#include "rtos_tasks.h"

// =============================================================================
// MODULE STATE
// =============================================================================

// Guards the state block against concurrent access between the counter task
// (Core 1, writer) and the web/Modbus/serial readers (Core 0).
static portMUX_TYPE timer_mux = portMUX_INITIALIZER_UNLOCKED;

static Ch1Mode      s_ch1_mode      = CH1_MODE_COUNTER;
static TimerState   s_state         = TIMER_IDLE;
static TimerOutMode s_out_mode      = TIMER_OUT_LATCH;
static bool         s_output_state  = false;
static bool         s_initialized   = false;

static uint32_t s_setpoint_s   = TIMER_DEFAULT_SETPOINT_S;
static uint32_t s_delay_off_s  = TIMER_DEFAULT_DELAY_OFF_S;

// Time is accumulated in milliseconds so pause/resume across the gate input
// does not lose fractions of a second. remaining_s is derived for display.
static uint32_t s_remaining_ms = TIMER_DEFAULT_SETPOINT_S * 1000UL;
static uint32_t s_delay_ms     = 0;     // Elapsed time in the delay-off phase
static uint32_t s_last_tick_ms = 0;     // millis() at the previous process()

// Debounced input state (asserted = electrically active, see CH1_TIMER_ACTIVE_LOW)
static bool     s_gate_asserted   = false;
static bool     s_gate_raw        = false;
static uint32_t s_gate_change_ms  = 0;
static bool     s_reset_asserted  = false;
static bool     s_reset_raw       = false;
static uint32_t s_reset_change_ms = 0;

// =============================================================================
// HELPERS
// =============================================================================

/**
 * @brief Read a CH1 timer input and normalise it to "asserted"
 * GPIO34/35 sit behind external pull-ups, so asserted means LOW.
 */
static inline bool read_asserted(uint8_t pin) {
#if CH1_TIMER_ACTIVE_LOW
    return digitalRead(pin) == LOW;
#else
    return digitalRead(pin) == HIGH;
#endif
}

/**
 * @brief Drive OUTPUT_CH1_PIN and mirror the state into shared system data
 */
static void set_output(bool on) {
    if (s_output_state == on) {
        return;
    }
    s_output_state = on;
    digitalWrite(OUTPUT_CH1_PIN, on ? HIGH : LOW);

    if (dataMutex && xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        systemData.channel[0].output_state = on;
        xSemaphoreGive(dataMutex);
    }
}

/**
 * @brief Rearm the countdown from the configured setpoint
 */
static void arm_countdown() {
    s_remaining_ms = s_setpoint_s * 1000UL;
    s_delay_ms = 0;
}

/**
 * @brief Debounce a single input
 * @param pin        GPIO to sample
 * @param raw        Last raw sample (updated)
 * @param stable     Debounced state (updated)
 * @param change_ms  Timestamp of the last raw change (updated)
 * @param now        Current millis()
 * @return true if the debounced state just transitioned to asserted
 */
static bool debounce_input(uint8_t pin, bool* raw, bool* stable,
                           uint32_t* change_ms, uint32_t now) {
    bool sample = read_asserted(pin);

    if (sample != *raw) {
        *raw = sample;
        *change_ms = now;
        return false;
    }

    if ((now - *change_ms) < TIMER_INPUT_DEBOUNCE_MS) {
        return false;
    }

    if (sample == *stable) {
        return false;
    }

    *stable = sample;
    return sample;   // Asserting edge only
}

// =============================================================================
// INITIALIZATION
// =============================================================================

void ct_timer_init() {
    // GPIO34/35 are input-only and have no internal pull-ups; the board
    // provides external ones, so plain INPUT is the only valid configuration.
    pinMode(COUNTER_CH1_PULSE_PIN, INPUT);
    pinMode(COUNTER_CH1_CTRL_PIN, INPUT);

    uint32_t now = millis();

    portENTER_CRITICAL(&timer_mux);
    s_state = TIMER_IDLE;
    arm_countdown();
    s_last_tick_ms = now;

    // Seed the debounce state from the current pin levels so a pin that is
    // already asserted at boot does not register a spurious edge.
    s_gate_raw = s_gate_asserted = read_asserted(COUNTER_CH1_PULSE_PIN);
    s_reset_raw = s_reset_asserted = read_asserted(COUNTER_CH1_CTRL_PIN);
    s_gate_change_ms = now;
    s_reset_change_ms = now;

    s_initialized = true;
    portEXIT_CRITICAL(&timer_mux);

    set_output(false);

    Serial.printf("[CT_timer] Initialized (setpoint=%lus, delay_off=%lus, mode=%s)\n",
                  (unsigned long)s_setpoint_s,
                  (unsigned long)s_delay_off_s,
                  s_out_mode == TIMER_OUT_LATCH ? "LATCH" : "DELAY-OFF");
}

// =============================================================================
// STATE MACHINE
// =============================================================================

void ct_timer_process() {
    if (!s_initialized) {
        ct_timer_init();
        return;
    }

    uint32_t now = millis();
    uint32_t dt = now - s_last_tick_ms;
    s_last_tick_ms = now;

    // Sample both inputs every tick so the debounce timers keep advancing
    bool gate_edge = debounce_input(COUNTER_CH1_PULSE_PIN, &s_gate_raw,
                                    &s_gate_asserted, &s_gate_change_ms, now);
    bool reset_edge = debounce_input(COUNTER_CH1_CTRL_PIN, &s_reset_raw,
                                     &s_reset_asserted, &s_reset_change_ms, now);
    (void)gate_edge;   // The gate is level-driven; the edge itself is implicit

    // The reset input wins over everything else, in any state and either
    // output mode.
    if (reset_edge) {
        ct_timer_reset();
        return;
    }

    portENTER_CRITICAL(&timer_mux);
    TimerState state = s_state;
    bool want_output = s_output_state;

    switch (state) {
        case TIMER_IDLE:
            want_output = false;
            if (s_gate_asserted) {
                arm_countdown();
                state = TIMER_RUNNING;
            }
            break;

        case TIMER_RUNNING:
            want_output = false;
            if (!s_gate_asserted) {
                // Gate released mid-count: hold the remaining time
                state = TIMER_PAUSED;
                break;
            }
            if (s_remaining_ms <= dt) {
                s_remaining_ms = 0;
                s_delay_ms = 0;
                state = TIMER_OUT;
                want_output = true;
            } else {
                s_remaining_ms -= dt;
            }
            break;

        case TIMER_PAUSED:
            want_output = false;
            if (s_gate_asserted) {
                state = TIMER_RUNNING;
            }
            break;

        case TIMER_OUT:
            want_output = true;
            if (s_out_mode == TIMER_OUT_DELAY_OFF) {
                s_delay_ms += dt;
                if (s_delay_ms >= (s_delay_off_s * 1000UL)) {
                    // Release the output and rearm. If the gate is still held
                    // the next tick starts a fresh countdown immediately.
                    arm_countdown();
                    state = TIMER_IDLE;
                    want_output = false;
                }
            }
            // TIMER_OUT_LATCH holds until the reset input or a manual reset
            break;
    }

    bool state_changed = (state != s_state);
    s_state = state;
    portEXIT_CRITICAL(&timer_mux);

    set_output(want_output);

    if (state_changed) {
        Serial.printf("[CT_timer] State -> %s (remaining=%lus)\n",
                      ct_timer_state_to_string(state),
                      (unsigned long)((s_remaining_ms + 999UL) / 1000UL));
    }
}

void ct_timer_reset() {
    portENTER_CRITICAL(&timer_mux);
    s_state = TIMER_IDLE;
    arm_countdown();
    s_last_tick_ms = millis();
    portEXIT_CRITICAL(&timer_mux);

    set_output(false);

    Serial.println("[CT_timer] Reset - output OFF, countdown rearmed");
}

// =============================================================================
// CONFIGURATION
// =============================================================================

void ct_timer_set_setpoint(uint32_t seconds) {
    if (seconds > TIMER_SETPOINT_MAX_S) {
        seconds = TIMER_SETPOINT_MAX_S;
    }

    portENTER_CRITICAL(&timer_mux);
    s_setpoint_s = seconds;
    // A setpoint change only takes effect on the next arm, except when the
    // timer is sitting idle - there it should show the new value right away.
    if (s_state == TIMER_IDLE) {
        arm_countdown();
    }
    portEXIT_CRITICAL(&timer_mux);

    Serial.printf("[CT_timer] Setpoint set to %lu s\n", (unsigned long)seconds);
}

void ct_timer_set_delay_off(uint32_t seconds) {
    if (seconds > TIMER_SETPOINT_MAX_S) {
        seconds = TIMER_SETPOINT_MAX_S;
    }

    portENTER_CRITICAL(&timer_mux);
    s_delay_off_s = seconds;
    portEXIT_CRITICAL(&timer_mux);

    Serial.printf("[CT_timer] Delay-off set to %lu s\n", (unsigned long)seconds);
}

void ct_timer_set_out_mode(TimerOutMode mode) {
    portENTER_CRITICAL(&timer_mux);
    s_out_mode = mode;
    s_delay_ms = 0;
    portEXIT_CRITICAL(&timer_mux);

    Serial.printf("[CT_timer] Output mode set to %s\n",
                  mode == TIMER_OUT_LATCH ? "LATCH" : "DELAY-OFF");
}

Ch1TimerStatus_t ct_timer_get_status() {
    Ch1TimerStatus_t status;

    portENTER_CRITICAL(&timer_mux);
    status.state = s_state;
    // Round up so a partially elapsed second still reads as 1, and only
    // shows 0 once the countdown has genuinely finished.
    status.remaining_s = (s_remaining_ms + 999UL) / 1000UL;
    status.setpoint_s = s_setpoint_s;
    status.delay_off_s = s_delay_off_s;
    status.out_mode = s_out_mode;
    status.output_state = s_output_state;
    portEXIT_CRITICAL(&timer_mux);

    return status;
}

const char* ct_timer_state_to_string(TimerState state) {
    switch (state) {
        case TIMER_IDLE:    return "IDLE";
        case TIMER_RUNNING: return "RUNNING";
        case TIMER_PAUSED:  return "PAUSED";
        case TIMER_OUT:     return "OUT";
        default:            return "UNKNOWN";
    }
}

// =============================================================================
// CHANNEL 1 MODE SWITCHING
// =============================================================================

void ch1_set_mode(Ch1Mode mode) {
    if (mode == s_ch1_mode && s_initialized) {
        return;
    }

    if (mode == CH1_MODE_TIMER) {
        // Hand the CH1 input pins over from PCNT to the timer. The PCNT
        // glitch filter goes away with it, hence the software debounce.
        pcnt_ch1_pause();
        s_ch1_mode = CH1_MODE_TIMER;
        ct_timer_init();

        CT_counter* ctr1 = getCounterInstance(1);
        if (ctr1) {
            ctr1->disable();
        }

        Serial.println("[CT_timer] CH1 switched to TIMER mode");
    } else {
        // Release the output before the counter takes over the pin again
        s_ch1_mode = CH1_MODE_COUNTER;
        ct_timer_reset();

        // Rebuild the PCNT unit from the stored channel config
        ChannelInputConfig_t cfg = input_config_get(0);
        input_config_set_mode(0, &cfg);
        pcnt_ch1_reset();
        pcnt_ch1_resume();

        CT_counter* ctr1 = getCounterInstance(1);
        if (ctr1) {
            ctr1->enable();
        }

        Serial.println("[CT_timer] CH1 switched to COUNTER mode");
    }
}

Ch1Mode ch1_get_mode() {
    return s_ch1_mode;
}
