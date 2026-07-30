/**
 * @file timer_operation.cpp
 * @brief Timer and stopwatch functionality implementation
 */

#include "timer_operation.h"
#include "config.h"
#include "globals.h"
#include "counter_operation.h"

namespace {
enum class TriggerTimerState : uint8_t { Idle, Timing, RelayOn };
portMUX_TYPE s_trigger_timer_mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool s_pulse_pending = false;
volatile bool s_ctrl_pending = false;
volatile unsigned long s_pulse_ms = 0;
volatile unsigned long s_ctrl_ms = 0;
TriggerTimerConfig s_trigger_config = {5000UL, 2000UL, TIMER_OFF_BY_DELAY};
TriggerTimerState s_trigger_state = TriggerTimerState::Idle;
unsigned long s_timer_start_ms = 0;
unsigned long s_relay_start_ms = 0;
bool s_relay_on = false;

void set_timer_relay(bool on) {
    s_relay_on = on;
    digitalWrite(TIMER_RELAY_PIN, on ? HIGH : LOW);
}

void IRAM_ATTR timer_pulse_isr() {
    portENTER_CRITICAL_ISR(&s_trigger_timer_mux);
    s_pulse_ms = millis();
    s_pulse_pending = true;
    portEXIT_CRITICAL_ISR(&s_trigger_timer_mux);
}

void IRAM_ATTR timer_ctrl_isr() {
    portENTER_CRITICAL_ISR(&s_trigger_timer_mux);
    s_ctrl_ms = millis();
    s_ctrl_pending = true;
    portEXIT_CRITICAL_ISR(&s_trigger_timer_mux);
}
}  // namespace

// =============================================================================
// TIMER ISR HANDLER
// =============================================================================

// ISR for frequency measurement timer (100ms interval)
bool IRAM_ATTR timer_isr_freq_handler(void *arg) {
    int16_t current_hw_count_ch1;
    int16_t current_hw_count_ch2;
    
    portENTER_CRITICAL_ISR(&pcnt_spinlock);

    //Channel 1 calculations
    pcnt_get_counter_value(COUNTER_CH1_PCNT_UNIT, &current_hw_count_ch1);
    int32_t current_count = (int32_t)current_hw_count_ch1 + s_ch1_total_count;
    s_ch1_frequency_hz = (current_count - s_ch1_last_count) * 10; // 100ms window = multiply by 10 for Hz
    s_ch1_last_count = current_count;
    
    //Channel 2 calculations
    pcnt_get_counter_value(COUNTER_CH2_PCNT_UNIT, &current_hw_count_ch2);
    current_count = (int32_t)current_hw_count_ch2 + s_ch2_total_count;
    s_ch2_frequency_hz = (current_count - s_ch2_last_count) * 10; // 100ms window = multiply by 10 for Hz
    s_ch2_last_count = current_count;
    
    portEXIT_CRITICAL_ISR(&pcnt_spinlock);
    
    return false; // Don't yield to higher priority task
}

// =============================================================================
// FREQUENCY TIMER INITIALIZATION
// =============================================================================

void freq_timer_init() {
    timer_config_t config = {
        .alarm_en = TIMER_ALARM_EN,
        .counter_en = TIMER_PAUSE,
        .intr_type = TIMER_INTR_LEVEL,
        .counter_dir = TIMER_COUNT_UP,
        .auto_reload = TIMER_AUTORELOAD_EN,
        .divider = TIMER_DIVIDER,  // 80MHz / 80 = 1MHz (1us ticks)
    };
    
    esp_err_t ret = timer_init(FREQ_TIMER_GROUP, FREQ_TIMER_IDX, &config);
    if (ret != ESP_OK) {
        Serial.printf("Frequency timer init failed: %d\n", ret);
        return;
    }
    
    // Set alarm for 100ms (100000 us)
    timer_set_alarm_value(FREQ_TIMER_GROUP, FREQ_TIMER_IDX, FREQ_MEASURE_INTERVAL_US);
    
    // Add ISR callback
    timer_isr_callback_add(FREQ_TIMER_GROUP, FREQ_TIMER_IDX, timer_isr_freq_handler, NULL, 0);
    
    // Start the timer
    timer_start(FREQ_TIMER_GROUP, FREQ_TIMER_IDX);
    
    Serial.println("Frequency measurement timer initialized");
}

// =============================================================================
// STOPWATCH TIMER FUNCTIONS
// =============================================================================

void stopwatch_timer_init() {
    timer_config_t config = {
        .alarm_en = TIMER_ALARM_DIS,    // No alarm for stopwatch
        .counter_en = TIMER_PAUSE,
        .intr_type = TIMER_INTR_LEVEL,
        .counter_dir = TIMER_COUNT_UP,
        .auto_reload = TIMER_AUTORELOAD_DIS,
        .divider = TIMER_DIVIDER,  // 80MHz / 80 = 1MHz (1us ticks)
    };
    
    esp_err_t ret = timer_init(STOPWATCH_TIMER_GROUP, STOPWATCH_TIMER_IDX, &config);
    if (ret != ESP_OK) {
        Serial.printf("Stopwatch timer init failed: %d\n", ret);
        return;
    }
    
    Serial.println("Stopwatch timer initialized");
}

void stopwatch_start() {
    timer_set_counter_value(STOPWATCH_TIMER_GROUP, STOPWATCH_TIMER_IDX, 0);
    timer_start(STOPWATCH_TIMER_GROUP, STOPWATCH_TIMER_IDX);
}

void stopwatch_stop() {
    timer_pause(STOPWATCH_TIMER_GROUP, STOPWATCH_TIMER_IDX);
    timer_set_counter_value(STOPWATCH_TIMER_GROUP, STOPWATCH_TIMER_IDX, 0);
}

void stopwatch_pause() {
    timer_pause(STOPWATCH_TIMER_GROUP, STOPWATCH_TIMER_IDX);
}

void stopwatch_resume() {
    timer_start(STOPWATCH_TIMER_GROUP, STOPWATCH_TIMER_IDX);
}

void stopwatch_reset() {
    timer_set_counter_value(STOPWATCH_TIMER_GROUP, STOPWATCH_TIMER_IDX, 0);
}

void stopwatch_get_elapsed(uint64_t *elapsed_us) {
    timer_get_counter_value(STOPWATCH_TIMER_GROUP, STOPWATCH_TIMER_IDX, elapsed_us);
}

void trigger_timer_init() {
    pinMode(TIMER_RELAY_PIN, OUTPUT);
    set_timer_relay(false);
    // GPIO34/35 require external pull-up/down resistors.
    pinMode(COUNTER_CH1_PULSE_PIN, INPUT);
    pinMode(COUNTER_CH1_CTRL_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(COUNTER_CH1_PULSE_PIN), timer_pulse_isr, RISING);
    attachInterrupt(digitalPinToInterrupt(COUNTER_CH1_CTRL_PIN), timer_ctrl_isr, RISING);
    Serial.printf("Trigger timer: PULSE=GPIO%d CTRL=GPIO%d relay=GPIO%d\n",
                  COUNTER_CH1_PULSE_PIN, COUNTER_CH1_CTRL_PIN, TIMER_RELAY_PIN);
}

void trigger_timer_set_config(const TriggerTimerConfig &config) {
    TriggerTimerConfig safe = config;
    if (safe.durationMs < 100UL) safe.durationMs = 100UL;
    if (safe.delayOffMs < 100UL) safe.delayOffMs = 100UL;
    if (safe.offMode != TIMER_OFF_BY_CTRL && safe.offMode != TIMER_OFF_BY_DELAY) safe.offMode = TIMER_OFF_BY_DELAY;
    portENTER_CRITICAL(&s_trigger_timer_mux);
    s_trigger_config = safe;
    portEXIT_CRITICAL(&s_trigger_timer_mux);
}

TriggerTimerConfig trigger_timer_get_config() {
    TriggerTimerConfig config;
    portENTER_CRITICAL(&s_trigger_timer_mux);
    config = s_trigger_config;
    portEXIT_CRITICAL(&s_trigger_timer_mux);
    return config;
}

TriggerTimerStatus trigger_timer_get_status() {
    TriggerTimerStatus status;
    const unsigned long now = millis();
    portENTER_CRITICAL(&s_trigger_timer_mux);
    status.running = s_trigger_state == TriggerTimerState::Timing;
    status.relayOn = s_relay_on;
    status.elapsedMs = status.running ? now - s_timer_start_ms : 0;
    portEXIT_CRITICAL(&s_trigger_timer_mux);
    return status;
}

void trigger_timer_process() {
    bool pulse;
    bool ctrl;
    unsigned long pulseMs;
    unsigned long ctrlMs;
    TriggerTimerConfig config;
    portENTER_CRITICAL(&s_trigger_timer_mux);
    pulse = s_pulse_pending;
    ctrl = s_ctrl_pending;
    pulseMs = s_pulse_ms;
    ctrlMs = s_ctrl_ms;
    s_pulse_pending = false;
    s_ctrl_pending = false;
    config = s_trigger_config;
    portEXIT_CRITICAL(&s_trigger_timer_mux);

    if (!timer_enabled) {
        s_trigger_state = TriggerTimerState::Idle;
        set_timer_relay(false);
        return;
    }
    if (pulse) {
        s_trigger_state = TriggerTimerState::Timing;
        s_timer_start_ms = pulseMs;
        set_timer_relay(false);
    }
    if (config.offMode == TIMER_OFF_BY_CTRL && ctrl &&
        s_trigger_state != TriggerTimerState::Idle && ctrlMs >= s_timer_start_ms) {
        s_trigger_state = TriggerTimerState::Idle;
        set_timer_relay(false);
        return;
    }
    const unsigned long now = millis();
    if (s_trigger_state == TriggerTimerState::Timing && now - s_timer_start_ms >= config.durationMs) {
        s_trigger_state = TriggerTimerState::RelayOn;
        s_relay_start_ms = now;
        set_timer_relay(true);
    }
    if (s_trigger_state == TriggerTimerState::RelayOn && config.offMode == TIMER_OFF_BY_DELAY &&
        now - s_relay_start_ms >= config.delayOffMs) {
        s_trigger_state = TriggerTimerState::Idle;
        set_timer_relay(false);
    }

    // trigger_timer_process runs after CT_counter::process(), so the timer
    // remains the owner of TIMER_RELAY_PIN even while the counter updates.
    set_timer_relay(s_relay_on);
}
