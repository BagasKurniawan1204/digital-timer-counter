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
//Increase counting for channel 1 every 1s (1Hz) for testing purposes. This is a temporary ISR for testing and should be removed in production.
bool IRAM_ATTR timer_isr_counter_handler(void *arg) {
    
    portENTER_CRITICAL_ISR(&pcnt_spinlock);

    s_ch1_total_count = s_ch1_total_count + 1; // Increment total count for channel 1
    //s_ch2_total_count = s_ch2_total_count + 1; // Increment total count for channel 2

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
// COUNTER TIMER FUNCTIONS
// =============================================================================

void counter_timer_init() {
    timer_config_t config = {
        .alarm_en = TIMER_ALARM_EN,
        .counter_en = TIMER_PAUSE,
        .intr_type = TIMER_INTR_LEVEL,
        .counter_dir = TIMER_COUNT_UP,
        .auto_reload = TIMER_AUTORELOAD_EN,
        .divider = TIMER_DIVIDER,  // 80MHz / 80 = 1MHz (1us ticks)
    };
    
    esp_err_t ret = timer_init(COUNTER_TIMER_GROUP, COUNTER_TIMER_IDX, &config);
    if (ret != ESP_OK) {
        Serial.printf("Counter timer init failed: %d\n", ret);
        return;
    }
    
    timer_set_alarm_value(COUNTER_TIMER_GROUP, COUNTER_TIMER_IDX, COUNTER_MEASURE_INTERVAL_US);
    
    // Add ISR callback
    timer_isr_callback_add(COUNTER_TIMER_GROUP, COUNTER_TIMER_IDX, timer_isr_counter_handler, NULL, 0);
    
    Serial.println("Counter timer initialized");
}

void counter_timer_start() {
    timer_start(COUNTER_TIMER_GROUP, COUNTER_TIMER_IDX);
}

void counter_timer_stop() {
    timer_pause(COUNTER_TIMER_GROUP, COUNTER_TIMER_IDX);
    timer_set_counter_value(COUNTER_TIMER_GROUP, COUNTER_TIMER_IDX, 0);
}

void counter_timer_pause() {
    timer_pause(COUNTER_TIMER_GROUP, COUNTER_TIMER_IDX);
}

void counter_timer_resume() {
    timer_start(COUNTER_TIMER_GROUP, COUNTER_TIMER_IDX);
}

void counter_reset() {
    timer_set_counter_value(COUNTER_TIMER_GROUP, COUNTER_TIMER_IDX, 0);
}

void counter_get_elapsed(uint64_t *elapsed_us) {
    timer_get_counter_value(COUNTER_TIMER_GROUP, COUNTER_TIMER_IDX, elapsed_us);
}
