#ifndef TIMER_OPERATION
#define TIMER_OPERATION
#include <Arduino.h>
#include "driver/timer.h"
#include "pcnt-configuration.h"

// ==================== DUAL CHANNEL FREQUENCY MEASUREMENT ====================

// Channel 1 variables
static volatile int32_t s_ch1_current_count = 0;
static volatile int32_t s_ch1_last_count = 0;
static volatile int32_t s_ch1_frequency_hz = 0;
static volatile bool s_ch1_new_frequency = false;

// Channel 2 variables
static volatile int32_t s_ch2_current_count = 0;
static volatile int32_t s_ch2_last_count = 0;
static volatile int32_t s_ch2_frequency_hz = 0;
static volatile bool s_ch2_new_frequency = false;

// Global control
bool continuous_mode = false;

/**
 * @brief Timer ISR for dual channel frequency measurement
 * @note Triggers every 100ms to calculate frequency for both channels
 */
bool IRAM_ATTR timer_isr_handler(void *arg) {
    int16_t ch1_hw_count, ch2_hw_count;

    portENTER_CRITICAL_ISR(&pcnt_spinlock);
    
    // Read Channel 1
    pcnt_get_counter_value(CHANNEL1_PCNT_UNIT, &ch1_hw_count);
    int32_t ch1_current = (int32_t)ch1_hw_count + s_ch1_total_count;
    
    // Read Channel 2
    pcnt_get_counter_value(CHANNEL2_PCNT_UNIT, &ch2_hw_count);
    int32_t ch2_current = (int32_t)ch2_hw_count + s_ch2_total_count;

    // Calculate frequency for Channel 1 (100ms window = multiply by 10)
    s_ch1_frequency_hz = (ch1_current - s_ch1_last_count) * 10;
    s_ch1_last_count = ch1_current;
    s_ch1_new_frequency = true;
    
    // Calculate frequency for Channel 2 (100ms window = multiply by 10)
    s_ch2_frequency_hz = (ch2_current - s_ch2_last_count) * 10;
    s_ch2_last_count = ch2_current;
    s_ch2_new_frequency = true;
    
    portEXIT_CRITICAL_ISR(&pcnt_spinlock);

    return false;
}

/**
 * @brief Setup hardware timer for frequency measurement
 * @note Timer triggers every 100ms to measure frequency
 */
void setup_timer() {
    timer_config_t config = {
        .alarm_en = TIMER_ALARM_EN,
        .counter_en = TIMER_PAUSE,
        .intr_type = TIMER_INTR_LEVEL,
        .counter_dir = TIMER_COUNT_UP,
        .auto_reload = TIMER_AUTORELOAD_EN,
        .divider = 80,  // 80MHz / 80 = 1MHz (1us ticks)
    };

    timer_init(TIMER_GROUP_0, TIMER_0, &config);
    
    // 100000 us = 100 ms = 0.1 second window
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, 100000);
    
    timer_isr_callback_add(TIMER_GROUP_0, TIMER_0, timer_isr_handler, NULL, 0);
    timer_start(TIMER_GROUP_0, TIMER_0);
}

// ==================== HELPER FUNCTIONS ====================

/**
 * @brief Get Channel 1 frequency
 */
int32_t get_ch1_frequency() {
    portENTER_CRITICAL(&pcnt_spinlock);
    int32_t freq = s_ch1_frequency_hz;
    portEXIT_CRITICAL(&pcnt_spinlock);
    return freq;
}

/**
 * @brief Get Channel 2 frequency
 */
int32_t get_ch2_frequency() {
    portENTER_CRITICAL(&pcnt_spinlock);
    int32_t freq = s_ch2_frequency_hz;
    portEXIT_CRITICAL(&pcnt_spinlock);
    return freq;
}

/**
 * @brief Check if new Channel 1 frequency is available
 */
bool ch1_has_new_frequency() {
    portENTER_CRITICAL(&pcnt_spinlock);
    bool available = s_ch1_new_frequency;
    s_ch1_new_frequency = false;
    portEXIT_CRITICAL(&pcnt_spinlock);
    return available;
}

/**
 * @brief Check if new Channel 2 frequency is available
 */
bool ch2_has_new_frequency() {
    portENTER_CRITICAL(&pcnt_spinlock);
    bool available = s_ch2_new_frequency;
    s_ch2_new_frequency = false;
    portEXIT_CRITICAL(&pcnt_spinlock);
    return available;
}

/**
 * @brief Reset frequency measurements for both channels
 */
void reset_frequency_measurement() {
    portENTER_CRITICAL(&pcnt_spinlock);
    s_ch1_last_count = 0;
    s_ch1_frequency_hz = 0;
    s_ch1_new_frequency = false;
    
    s_ch2_last_count = 0;
    s_ch2_frequency_hz = 0;
    s_ch2_new_frequency = false;
    portEXIT_CRITICAL(&pcnt_spinlock);
}


#endif // TIMER_OPERATION