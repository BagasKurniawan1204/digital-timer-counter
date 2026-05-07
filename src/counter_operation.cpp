/**
 * @file counter_operation.cpp
 * @brief PCNT-based high speed counter implementation
 */

#include "counter_operation.h"
#include "rtos_tasks.h"

// =============================================================================
// ISR HANDLERS
// =============================================================================

void IRAM_ATTR pcnt_ch1_isr_handler(void *arg) {
    uint32_t status = 0;
    pcnt_get_event_status(COUNTER_CH1_PCNT_UNIT, &status);
    
    int32_t delta = 0;
    
    portENTER_CRITICAL_ISR(&pcnt_spinlock);
    
    if (status & PCNT_EVT_H_LIM) {
        // Counter hit high limit, add to total
        s_ch1_total_count += (int32_t)PCNT_HIGH_LIMIT;
        delta = PCNT_HIGH_LIMIT;
    } 
    else if (status & PCNT_EVT_L_LIM) {
        // Counter hit low limit, subtract from total
        s_ch1_total_count += (int32_t)PCNT_LOW_LIMIT;
        delta = PCNT_LOW_LIMIT;
    }
    
    // Clear counter after updating total (counter auto-resets on limit event)
    pcnt_counter_clear(COUNTER_CH1_PCNT_UNIT);
    
    portEXIT_CRITICAL_ISR(&pcnt_spinlock);
    
    // Notify RTOS task of overflow event (non-blocking)
    if (pcntEventQueue != NULL && delta != 0) {
        PcntEvent_t event = {.channel = 0, .overflow_delta = delta};
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(pcntEventQueue, &event, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

void IRAM_ATTR pcnt_ch2_isr_handler(void *arg) {
    uint32_t status = 0;
    pcnt_get_event_status(COUNTER_CH2_PCNT_UNIT, &status);
    
    int32_t delta = 0;
    
    portENTER_CRITICAL_ISR(&pcnt_spinlock);
    
    if (status & PCNT_EVT_H_LIM) {
        s_ch2_total_count += (int32_t)PCNT_HIGH_LIMIT;
        delta = PCNT_HIGH_LIMIT;
    } 
    else if (status & PCNT_EVT_L_LIM) {
        s_ch2_total_count += (int32_t)PCNT_LOW_LIMIT;
        delta = PCNT_LOW_LIMIT;
    }
    
    pcnt_counter_clear(COUNTER_CH2_PCNT_UNIT);
    
    portEXIT_CRITICAL_ISR(&pcnt_spinlock);
    
    // Notify RTOS task of overflow event (non-blocking)
    if (pcntEventQueue != NULL && delta != 0) {
        PcntEvent_t event = {.channel = 1, .overflow_delta = delta};
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(pcntEventQueue, &event, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

// =============================================================================
// INITIALIZATION
// =============================================================================

esp_err_t pcnt_init() {
    esp_err_t ret;
    
    // ========== Channel 1 Configuration ==========
    pcnt_config_t pcnt_config_ch1 = {
        .pulse_gpio_num = COUNTER_CH1_PULSE_PIN,
        .ctrl_gpio_num  = COUNTER_CH1_CTRL_PIN,
        .lctrl_mode     = PCNT_MODE_KEEP,       // Keep counting direction when ctrl is LOW
        .hctrl_mode     = PCNT_MODE_REVERSE,    // Reverse direction when ctrl is HIGH
        .pos_mode       = PCNT_COUNT_INC,       // Increment on positive edge
        .neg_mode       = PCNT_COUNT_DEC,       // Decrement on negative edge
        .counter_h_lim  = PCNT_HIGH_LIMIT,
        .counter_l_lim  = PCNT_LOW_LIMIT,
        .unit           = COUNTER_CH1_PCNT_UNIT,
        .channel        = PCNT_CHANNEL_0,
    };
    
    ret = pcnt_unit_config(&pcnt_config_ch1);
    if (ret != ESP_OK) {
        Serial.printf("CH1: PCNT config failed: %d\n", ret);
        return ret;
    }
    
    pcnt_counter_pause(COUNTER_CH1_PCNT_UNIT);
    pcnt_counter_clear(COUNTER_CH1_PCNT_UNIT);
    pcnt_event_enable(COUNTER_CH1_PCNT_UNIT, PCNT_EVT_H_LIM);
    pcnt_event_enable(COUNTER_CH1_PCNT_UNIT, PCNT_EVT_L_LIM);
    
    // ========== Channel 2 Configuration ==========
    pcnt_config_t pcnt_config_ch2 = {
        .pulse_gpio_num = COUNTER_CH2_PULSE_PIN,
        .ctrl_gpio_num  = COUNTER_CH2_CTRL_PIN,
        .lctrl_mode     = PCNT_MODE_KEEP,
        .hctrl_mode     = PCNT_MODE_REVERSE,
        .pos_mode       = PCNT_COUNT_INC,
        .neg_mode       = PCNT_COUNT_DEC,
        .counter_h_lim  = PCNT_HIGH_LIMIT,
        .counter_l_lim  = PCNT_LOW_LIMIT,
        .unit           = COUNTER_CH2_PCNT_UNIT,
        .channel        = PCNT_CHANNEL_0,
    };
    
    ret = pcnt_unit_config(&pcnt_config_ch2);
    if (ret != ESP_OK) {
        Serial.printf("CH2: PCNT config failed: %d\n", ret);
        return ret;
    }
    
    pcnt_counter_pause(COUNTER_CH2_PCNT_UNIT);
    pcnt_counter_clear(COUNTER_CH2_PCNT_UNIT);
    pcnt_event_enable(COUNTER_CH2_PCNT_UNIT, PCNT_EVT_H_LIM);
    pcnt_event_enable(COUNTER_CH2_PCNT_UNIT, PCNT_EVT_L_LIM);
    
    // ========== ISR Installation ==========
    ret = pcnt_isr_service_install(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE means ISR service already installed (OK)
        Serial.printf("PCNT ISR service install failed: %d\n", ret);
        return ret;
    }
    
    pcnt_isr_handler_add(COUNTER_CH1_PCNT_UNIT, pcnt_ch1_isr_handler, NULL);
    pcnt_isr_handler_add(COUNTER_CH2_PCNT_UNIT, pcnt_ch2_isr_handler, NULL);
    
    pcnt_intr_enable(COUNTER_CH1_PCNT_UNIT);
    pcnt_intr_enable(COUNTER_CH2_PCNT_UNIT);
    
    // ========== Filter Configuration ==========
    pcnt_set_filter_value(COUNTER_CH1_PCNT_UNIT, PCNT_FILTER_VALUE);
    pcnt_set_filter_value(COUNTER_CH2_PCNT_UNIT, PCNT_FILTER_VALUE);
    pcnt_filter_enable(COUNTER_CH1_PCNT_UNIT);
    pcnt_filter_enable(COUNTER_CH2_PCNT_UNIT);
    
    Serial.printf("PCNT initialized: CH1=GPIO%d/%d, CH2=GPIO%d/%d\n",
                  COUNTER_CH1_PULSE_PIN, COUNTER_CH1_CTRL_PIN,
                  COUNTER_CH2_PULSE_PIN, COUNTER_CH2_CTRL_PIN);
    
    return ESP_OK;
}

// =============================================================================
// CHANNEL 1 FUNCTIONS
// =============================================================================

int32_t pcnt_ch1_get_count() {
    int16_t current_count;
    pcnt_get_counter_value(COUNTER_CH1_PCNT_UNIT, &current_count);
    
    portENTER_CRITICAL(&pcnt_spinlock);
    int32_t total_count = (int32_t)current_count + s_ch1_total_count;
    portEXIT_CRITICAL(&pcnt_spinlock);
    
    return total_count;
}

void pcnt_ch1_reset() {
    portENTER_CRITICAL(&pcnt_spinlock);
    s_ch1_total_count = 0;
    s_ch1_error_count = 0;
    portEXIT_CRITICAL(&pcnt_spinlock);
    pcnt_counter_clear(COUNTER_CH1_PCNT_UNIT);
}

void pcnt_ch1_resume() {
    pcnt_counter_resume(COUNTER_CH1_PCNT_UNIT);
}

void pcnt_ch1_pause() {
    pcnt_counter_pause(COUNTER_CH1_PCNT_UNIT);
}

void pcnt_ch1_manual_increment(int32_t value) {
    portENTER_CRITICAL(&pcnt_spinlock);
    s_ch1_total_count += value;
    portEXIT_CRITICAL(&pcnt_spinlock);
}

void pcnt_ch1_manual_decrement(int32_t value) {
    portENTER_CRITICAL(&pcnt_spinlock);
    s_ch1_total_count -= value;
    portEXIT_CRITICAL(&pcnt_spinlock);
}

// =============================================================================
// CHANNEL 2 FUNCTIONS
// =============================================================================

int32_t pcnt_ch2_get_count() {
    int16_t current_count;
    pcnt_get_counter_value(COUNTER_CH2_PCNT_UNIT, &current_count);
    
    portENTER_CRITICAL(&pcnt_spinlock);
    int32_t total_count = (int32_t)current_count + s_ch2_total_count;
    portEXIT_CRITICAL(&pcnt_spinlock);
    
    return total_count;
}

void pcnt_ch2_reset() {
    portENTER_CRITICAL(&pcnt_spinlock);
    s_ch2_total_count = 0;
    s_ch2_error_count = 0;
    portEXIT_CRITICAL(&pcnt_spinlock);
    pcnt_counter_clear(COUNTER_CH2_PCNT_UNIT);
}

void pcnt_ch2_resume() {
    pcnt_counter_resume(COUNTER_CH2_PCNT_UNIT);
}

void pcnt_ch2_pause() {
    pcnt_counter_pause(COUNTER_CH2_PCNT_UNIT);
}

void pcnt_ch2_manual_increment(int32_t value) {
    portENTER_CRITICAL(&pcnt_spinlock);
    s_ch2_total_count += value;
    portEXIT_CRITICAL(&pcnt_spinlock);
}

void pcnt_ch2_manual_decrement(int32_t value) {
    portENTER_CRITICAL(&pcnt_spinlock);
    s_ch2_total_count -= value;
    portEXIT_CRITICAL(&pcnt_spinlock);
}

// =============================================================================
// CONVENIENCE FUNCTIONS (Backwards Compatibility)
// =============================================================================

int32_t pcnt_get_total_value() {
    return pcnt_ch1_get_count();
}

uint32_t pcnt_get_error_count() {
    portENTER_CRITICAL(&pcnt_spinlock);
    uint32_t errors = s_ch1_error_count;
    portEXIT_CRITICAL(&pcnt_spinlock);
    return errors;
}

void pcnt_reset() {
    pcnt_ch1_reset();
    pcnt_ch2_reset();
}

void pcnt_resume() {
    pcnt_ch1_resume();
    pcnt_ch2_resume();
}

void pcnt_pause() {
    pcnt_ch1_pause();
    pcnt_ch2_pause();
}