#ifndef TIMER_OPERATION
#define TIMER_OPERATION
#include <Arduino.h>
#include "driver/timer.h"
#include "pcnt-configuration.h"

// frequency measurement variables
static volatile int32_t s_current_count_ch1 = 0;
static volatile int32_t s_last_count_ch1 = 0;
static volatile int32_t s_frequency_hz_ch1 = 0;
static volatile bool s_new_frequency_available = false;
bool continuous_mode = false;
volatile uint32_t elapsed_ms = 0;
bool stopwatch_running = false;

// Global enable flags
bool counter_enabled = false;
bool timer_enabled = false;

// Timer interrupt handler for frequency measurement
bool IRAM_ATTR timer_isr_handler_pcnt_ch1(void *arg) {
    int16_t current_hw_count;

    portENTER_CRITICAL_ISR(&pcnt_spinlock);
    pcnt_get_counter_value(PCNT_UNIT_0, &current_hw_count); // read inside critical
    int32_t current_count = (int32_t)current_hw_count + s_ch1_total_count;

    // Calculate frequency (100ms window)
    s_frequency_hz_ch1 = (current_count - s_last_count_ch1) * 10; // window = 100ms
    s_last_count_ch1 = current_count;
    s_new_frequency_available = true;
    portEXIT_CRITICAL_ISR(&pcnt_spinlock);

    return false;
}

void freq_timer_init() {
    timer_config_t config = {
        .alarm_en = TIMER_ALARM_EN,
        .counter_en = TIMER_PAUSE,
        .intr_type = TIMER_INTR_LEVEL,
        .counter_dir = TIMER_COUNT_UP,
        .auto_reload = TIMER_AUTORELOAD_EN,
        .divider = 80,  // 80MHz / 80 = 1MHz (1us ticks)
    };

    timer_init(TIMER_GROUP_0, TIMER_0, &config);
    
    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, 100000);
    
    timer_isr_callback_add(TIMER_GROUP_0, TIMER_0, timer_isr_handler_pcnt_ch1, NULL, 0);
    timer_start(TIMER_GROUP_0, TIMER_0);
}

void stopwatch_timer_init(){
    timer_config_t config = {
        .alarm_en = TIMER_ALARM_EN,
        .counter_en = TIMER_PAUSE,
        .intr_type = TIMER_INTR_LEVEL,
        .counter_dir = TIMER_COUNT_UP,
        .auto_reload = TIMER_AUTORELOAD_EN,
        .divider = 80,  // 80MHz / 80 = 1MHz (1us ticks)
    };
    timer_init(TIMER_GROUP_1, TIMER_0, &config);
}

void stopwatch_start(){
    timer_set_counter_value(TIMER_GROUP_1, TIMER_0, 0);
    timer_start(TIMER_GROUP_1, TIMER_0);
}
void stopwatch_stop(){
    timer_pause(TIMER_GROUP_1, TIMER_0);
    timer_set_counter_value(TIMER_GROUP_1, TIMER_0, 0);
}
void stopwatch_pause(){
    timer_pause(TIMER_GROUP_1, TIMER_0);
}
void stopwatch_reset(){
    timer_set_counter_value(TIMER_GROUP_1, TIMER_0, 0);
}
void stopwatch_get_elapsed(uint64_t *elapsed_us){
    timer_get_counter_value(TIMER_GROUP_1, TIMER_0, elapsed_us);
}


#endif // TIMER_OPERATION