/**
 * @file timer_operation.cpp
 * @brief Timer and stopwatch functionality implementation
 */

#include "timer_operation.h"
#include "config.h"
#include "globals.h"
#include "counter_operation.h"

// =============================================================================
// TIMER ISR HANDLER
// =============================================================================

bool IRAM_ATTR timer_isr_freq_handler(void *arg) {
    int16_t current_hw_count;
    
    portENTER_CRITICAL_ISR(&pcnt_spinlock);
    
    // Read current hardware counter value
    pcnt_get_counter_value(COUNTER_CH1_PCNT_UNIT, &current_hw_count);
    int32_t current_count = (int32_t)current_hw_count + s_ch1_total_count;
    
    // Calculate frequency (100ms window = multiply by 10 for Hz)
    s_ch1_frequency_hz = (current_count - s_ch1_last_count) * 10;
    s_ch1_last_count = current_count;
    s_ch1_new_freq_available = true;
    
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
