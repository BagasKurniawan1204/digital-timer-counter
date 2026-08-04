/**
 * @file ct_timer.cpp
 * @brief Per-channel countdown timer implementation
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

#define TIMER_CH_COUNT  2

// Per-channel countdown state. Time is accumulated in milliseconds so
// pause/resume across the gate input does not lose fractions of a second;
// remaining_s is derived for display.
typedef struct {
    ChOpMode     op_mode;
    TimerState   state;
    TimerOutMode out_mode;
    bool         output_state;
    bool         initialized;

    uint32_t     setpoint_s;
    uint32_t     delay_off_s;

    uint32_t     remaining_ms;
    uint32_t     delay_ms;      // Elapsed time in the delay-off phase
    uint32_t     last_tick_ms;  // millis() at the previous process()

    // Debounced input state (asserted = electrically active, see
    // TIMER_INPUT_ACTIVE_LOW)
    bool         gate_raw, gate_asserted;
    uint32_t     gate_change_ms;
    bool         reset_raw, reset_asserted;
    uint32_t     reset_change_ms;
} TimerCh_t;

// Guards the state block against concurrent access between the counter task
// (Core 1, writer) and the web/Modbus/serial readers (Core 0). One lock covers
// both channels: the critical sections are a handful of assignments each.
static portMUX_TYPE timer_mux = portMUX_INITIALIZER_UNLOCKED;

static TimerCh_t s_tmr[TIMER_CH_COUNT] = {
    { CH_MODE_COUNTER, TIMER_IDLE, TIMER_OUT_LATCH, false, false,
      TIMER_DEFAULT_SETPOINT_S, TIMER_DEFAULT_DELAY_OFF_S,
      TIMER_DEFAULT_SETPOINT_S * 1000UL, 0, 0,
      false, false, 0, false, false, 0 },
    { CH_MODE_COUNTER, TIMER_IDLE, TIMER_OUT_LATCH, false, false,
      TIMER_DEFAULT_SETPOINT_S, TIMER_DEFAULT_DELAY_OFF_S,
      TIMER_DEFAULT_SETPOINT_S * 1000UL, 0, 0,
      false, false, 0, false, false, 0 },
};

// Pin assignment per channel, so no function below names a specific GPIO.
static const struct {
    uint8_t gate;
    uint8_t reset;
    uint8_t out;
} TMR_PINS[TIMER_CH_COUNT] = {
    { COUNTER_CH1_PULSE_PIN, COUNTER_CH1_CTRL_PIN, OUTPUT_CH1_PIN },
    { COUNTER_CH2_PULSE_PIN, COUNTER_CH2_CTRL_PIN, OUTPUT_CH2_PIN },
};

// =============================================================================
// HELPERS
// =============================================================================

static inline bool ch_valid(uint8_t channel) {
    return channel < TIMER_CH_COUNT;
}

/**
 * @brief Read a timer input and normalise it to "asserted"
 * All four input pins sit behind external pull-ups, so asserted means LOW.
 */
static inline bool read_asserted(uint8_t pin) {
#if TIMER_INPUT_ACTIVE_LOW
    return digitalRead(pin) == LOW;
#else
    return digitalRead(pin) == HIGH;
#endif
}

/**
 * @brief Drive a channel's output pin and mirror the state into shared data
 */
static void set_output(uint8_t channel, bool on) {
    TimerCh_t *t = &s_tmr[channel];

    if (t->output_state == on) {
        return;
    }
    t->output_state = on;
    digitalWrite(TMR_PINS[channel].out, on ? HIGH : LOW);

    if (dataMutex && xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        systemData.channel[channel].output_state = on;
        xSemaphoreGive(dataMutex);
    }
}

/**
 * @brief Rearm a channel's countdown from its configured setpoint
 */
static void arm_countdown(TimerCh_t *t) {
    t->remaining_ms = t->setpoint_s * 1000UL;
    t->delay_ms = 0;
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

void ct_timer_init(uint8_t channel) {
    if (!ch_valid(channel)) {
        return;
    }

    TimerCh_t *t = &s_tmr[channel];

    // CH1 is on GPIO34/35, which are input-only and have no internal pull-ups;
    // the board provides external ones, so plain INPUT is the only valid
    // configuration there. CH2's GPIO32/33 do support internal pull-ups, so
    // they are enabled as a backstop alongside the external ones.
    pinMode(TMR_PINS[channel].gate, channel == 0 ? INPUT : INPUT_PULLUP);
    pinMode(TMR_PINS[channel].reset, channel == 0 ? INPUT : INPUT_PULLUP);

    uint32_t now = millis();

    portENTER_CRITICAL(&timer_mux);
    t->state = TIMER_IDLE;
    arm_countdown(t);
    t->last_tick_ms = now;

    // Seed the debounce state from the current pin levels so a pin that is
    // already asserted at boot does not register a spurious edge.
    t->gate_raw = t->gate_asserted = read_asserted(TMR_PINS[channel].gate);
    t->reset_raw = t->reset_asserted = read_asserted(TMR_PINS[channel].reset);
    t->gate_change_ms = now;
    t->reset_change_ms = now;

    t->initialized = true;
    portEXIT_CRITICAL(&timer_mux);

    set_output(channel, false);

    Serial.printf("[CT_timer] CH%u initialized (setpoint=%lus, delay_off=%lus, mode=%s)\n",
                  (unsigned)(channel + 1),
                  (unsigned long)t->setpoint_s,
                  (unsigned long)t->delay_off_s,
                  t->out_mode == TIMER_OUT_LATCH ? "LATCH" : "DELAY-OFF");
}

// =============================================================================
// STATE MACHINE
// =============================================================================

void ct_timer_process(uint8_t channel) {
    if (!ch_valid(channel)) {
        return;
    }

    TimerCh_t *t = &s_tmr[channel];

    if (!t->initialized) {
        ct_timer_init(channel);
        return;
    }

    uint32_t now = millis();
    uint32_t dt = now - t->last_tick_ms;
    t->last_tick_ms = now;

    // Sample both inputs every tick so the debounce timers keep advancing
    bool gate_edge = debounce_input(TMR_PINS[channel].gate, &t->gate_raw,
                                    &t->gate_asserted, &t->gate_change_ms, now);
    bool reset_edge = debounce_input(TMR_PINS[channel].reset, &t->reset_raw,
                                     &t->reset_asserted, &t->reset_change_ms, now);
    (void)gate_edge;   // The gate is level-driven; the edge itself is implicit

    // The reset input wins over everything else, in any state and either
    // output mode.
    if (reset_edge) {
        ct_timer_reset(channel);
        return;
    }

    portENTER_CRITICAL(&timer_mux);
    TimerState state = t->state;
    bool want_output = t->output_state;

    switch (state) {
        case TIMER_IDLE:
            want_output = false;
            if (t->gate_asserted) {
                arm_countdown(t);
                state = TIMER_RUNNING;
            }
            break;

        case TIMER_RUNNING:
            want_output = false;
            if (!t->gate_asserted) {
                // Gate released mid-count: hold the remaining time
                state = TIMER_PAUSED;
                break;
            }
            if (t->remaining_ms <= dt) {
                t->remaining_ms = 0;
                t->delay_ms = 0;
                state = TIMER_OUT;
                want_output = true;
            } else {
                t->remaining_ms -= dt;
            }
            break;

        case TIMER_PAUSED:
            want_output = false;
            if (t->gate_asserted) {
                state = TIMER_RUNNING;
            }
            break;

        case TIMER_OUT:
            want_output = true;
            if (t->out_mode == TIMER_OUT_DELAY_OFF) {
                t->delay_ms += dt;
                if (t->delay_ms >= (t->delay_off_s * 1000UL)) {
                    // Release the output and rearm. If the gate is still held
                    // the next tick starts a fresh countdown immediately.
                    arm_countdown(t);
                    state = TIMER_IDLE;
                    want_output = false;
                }
            }
            // TIMER_OUT_LATCH holds until the reset input or a manual reset
            break;
    }

    bool state_changed = (state != t->state);
    t->state = state;
    uint32_t remaining_ms = t->remaining_ms;
    portEXIT_CRITICAL(&timer_mux);

    set_output(channel, want_output);

    if (state_changed) {
        Serial.printf("[CT_timer] CH%u state -> %s (remaining=%lus)\n",
                      (unsigned)(channel + 1),
                      ct_timer_state_to_string(state),
                      (unsigned long)((remaining_ms + 999UL) / 1000UL));
    }
}

void ct_timer_reset(uint8_t channel) {
    if (!ch_valid(channel)) {
        return;
    }

    TimerCh_t *t = &s_tmr[channel];

    portENTER_CRITICAL(&timer_mux);
    t->state = TIMER_IDLE;
    arm_countdown(t);
    t->last_tick_ms = millis();
    portEXIT_CRITICAL(&timer_mux);

    set_output(channel, false);

    Serial.printf("[CT_timer] CH%u reset - output OFF, countdown rearmed\n",
                  (unsigned)(channel + 1));
}

// =============================================================================
// CONFIGURATION
// =============================================================================

void ct_timer_set_setpoint(uint8_t channel, uint32_t seconds) {
    if (!ch_valid(channel)) {
        return;
    }

    if (seconds > TIMER_SETPOINT_MAX_S) {
        seconds = TIMER_SETPOINT_MAX_S;
    }

    TimerCh_t *t = &s_tmr[channel];

    portENTER_CRITICAL(&timer_mux);
    t->setpoint_s = seconds;
    // A setpoint change only takes effect on the next arm, except when the
    // timer is sitting idle - there it should show the new value right away.
    if (t->state == TIMER_IDLE) {
        arm_countdown(t);
    }
    portEXIT_CRITICAL(&timer_mux);

    Serial.printf("[CT_timer] CH%u setpoint set to %lu s\n",
                  (unsigned)(channel + 1), (unsigned long)seconds);
}

void ct_timer_set_delay_off(uint8_t channel, uint32_t seconds) {
    if (!ch_valid(channel)) {
        return;
    }

    if (seconds > TIMER_SETPOINT_MAX_S) {
        seconds = TIMER_SETPOINT_MAX_S;
    }

    portENTER_CRITICAL(&timer_mux);
    s_tmr[channel].delay_off_s = seconds;
    portEXIT_CRITICAL(&timer_mux);

    Serial.printf("[CT_timer] CH%u delay-off set to %lu s\n",
                  (unsigned)(channel + 1), (unsigned long)seconds);
}

void ct_timer_set_out_mode(uint8_t channel, TimerOutMode mode) {
    if (!ch_valid(channel)) {
        return;
    }

    portENTER_CRITICAL(&timer_mux);
    s_tmr[channel].out_mode = mode;
    s_tmr[channel].delay_ms = 0;
    portEXIT_CRITICAL(&timer_mux);

    Serial.printf("[CT_timer] CH%u output mode set to %s\n",
                  (unsigned)(channel + 1),
                  mode == TIMER_OUT_LATCH ? "LATCH" : "DELAY-OFF");
}

ChTimerStatus_t ct_timer_get_status(uint8_t channel) {
    ChTimerStatus_t status = {TIMER_IDLE, 0, 0, 0, TIMER_OUT_LATCH, false};

    if (!ch_valid(channel)) {
        return status;
    }

    const TimerCh_t *t = &s_tmr[channel];

    portENTER_CRITICAL(&timer_mux);
    status.state = t->state;
    // Round up so a partially elapsed second still reads as 1, and only
    // shows 0 once the countdown has genuinely finished.
    status.remaining_s = (t->remaining_ms + 999UL) / 1000UL;
    status.setpoint_s = t->setpoint_s;
    status.delay_off_s = t->delay_off_s;
    status.out_mode = t->out_mode;
    status.output_state = t->output_state;
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
// CHANNEL MODE SWITCHING
// =============================================================================

// The PCNT helpers are separate named functions per channel rather than an
// array, so the handover picks them with a small switch.
static void pcnt_ch_pause(uint8_t channel) {
    if (channel == 0) pcnt_ch1_pause();
    else              pcnt_ch2_pause();
}

static void pcnt_ch_reset(uint8_t channel) {
    if (channel == 0) pcnt_ch1_reset();
    else              pcnt_ch2_reset();
}

static void pcnt_ch_resume(uint8_t channel) {
    if (channel == 0) pcnt_ch1_resume();
    else              pcnt_ch2_resume();
}

void ch_set_mode(uint8_t channel, ChOpMode mode) {
    if (!ch_valid(channel)) {
        return;
    }

    TimerCh_t *t = &s_tmr[channel];

    if (mode == t->op_mode && t->initialized) {
        return;
    }

    // getCounterInstance() is 1-based
    CT_counter* ctr = getCounterInstance(channel + 1);

    if (mode == CH_MODE_TIMER) {
        // Hand the channel's input pins over from PCNT to the timer. The PCNT
        // glitch filter goes away with it, hence the software debounce.
        pcnt_ch_pause(channel);
        t->op_mode = CH_MODE_TIMER;
        ct_timer_init(channel);

        if (ctr) {
            ctr->disable();
        }

        Serial.printf("[CT_timer] CH%u switched to TIMER mode\n",
                      (unsigned)(channel + 1));
    } else {
        // Release the output before the counter takes over the pin again
        t->op_mode = CH_MODE_COUNTER;
        ct_timer_reset(channel);

        // Rebuild the PCNT unit from the stored channel config
        ChannelInputConfig_t cfg = input_config_get(channel);
        input_config_set_mode(channel, &cfg);
        pcnt_ch_reset(channel);
        pcnt_ch_resume(channel);

        if (ctr) {
            ctr->enable();
        }

        Serial.printf("[CT_timer] CH%u switched to COUNTER mode\n",
                      (unsigned)(channel + 1));
    }
}

ChOpMode ch_get_mode(uint8_t channel) {
    if (!ch_valid(channel)) {
        return CH_MODE_COUNTER;
    }
    return s_tmr[channel].op_mode;
}
