#ifndef PCNT_CONFIGURATION
#define PCNT_CONFIGURATION

#include <Arduino.h>
#include "driver/pcnt.h"
#include "esp_log.h"
extern portMUX_TYPE pcnt_spinlock;

// ==================== DUAL CHANNEL CONFIGURATION ====================

// Channel 1 Configuration
#define CHANNEL1_PIN            34
#define CHANNEL1_PCNT_UNIT      PCNT_UNIT_0

// Channel 2 Configuration  
#define CHANNEL2_PIN            35
#define CHANNEL2_PCNT_UNIT      PCNT_UNIT_1

// PCNT Limits
#define PCNT_HIGH_LIMIT         32767
#define PCNT_LOW_LIMIT          -32768

static const char* TAG = "dual_pcnt";

// Channel 1 variables
static volatile int32_t s_ch1_total_count = 0;
static volatile uint32_t s_ch1_error_count = 0;

// Channel 2 variables
static volatile int32_t s_ch2_total_count = 0;
static volatile uint32_t s_ch2_error_count = 0;

static volatile int32_t s_last_pcnt_count = 0;


/**
 * @brief PCNT interrupt service routine for Channel 1 overflow handling
 */
void IRAM_ATTR pcnt_ch1_isr_handler(void *arg) {
    uint32_t status;
    esp_err_t ret = pcnt_get_event_status(CHANNEL1_PCNT_UNIT, &status);

    if (ret != ESP_OK) {
        ESP_DRAM_LOGE(TAG, "CH1: Failed to get PCNT event status: %d", ret);
        portENTER_CRITICAL_ISR(&pcnt_spinlock);
        s_ch1_error_count++;
        portEXIT_CRITICAL_ISR(&pcnt_spinlock);
        return;
    }

    portENTER_CRITICAL_ISR(&pcnt_spinlock);
    {
        if ((status & PCNT_EVT_H_LIM) && (status & PCNT_EVT_L_LIM)) {
            ESP_DRAM_LOGW(TAG, "CH1: Simultaneous high/low limit - resetting");
            s_ch1_total_count = 0;
            s_ch1_error_count++;
        } else if (status & PCNT_EVT_H_LIM) {
            s_ch1_total_count += (int32_t)PCNT_HIGH_LIMIT + 1;
        } else if (status & PCNT_EVT_L_LIM) {
            s_ch1_total_count += (int32_t)PCNT_LOW_LIMIT - 1;
        }
    }
    portEXIT_CRITICAL_ISR(&pcnt_spinlock);
    
    pcnt_counter_clear(CHANNEL1_PCNT_UNIT);
}

/**
 * @brief PCNT interrupt service routine for Channel 2 overflow handling
 */
void IRAM_ATTR pcnt_ch2_isr_handler(void *arg) {
    uint32_t status;
    esp_err_t ret = pcnt_get_event_status(CHANNEL2_PCNT_UNIT, &status);

    if (ret != ESP_OK) {
        ESP_DRAM_LOGE(TAG, "CH2: Failed to get PCNT event status: %d", ret);
        portENTER_CRITICAL_ISR(&pcnt_spinlock);
        s_ch2_error_count++;
        portEXIT_CRITICAL_ISR(&pcnt_spinlock);
        return;
    }

    portENTER_CRITICAL_ISR(&pcnt_spinlock);
    {
        if ((status & PCNT_EVT_H_LIM) && (status & PCNT_EVT_L_LIM)) {
            ESP_DRAM_LOGW(TAG, "CH2: Simultaneous high/low limit - resetting");
            s_ch2_total_count = 0;
            s_ch2_error_count++;
        } else if (status & PCNT_EVT_H_LIM) {
            s_ch2_total_count += (int32_t)PCNT_HIGH_LIMIT + 1;
        } else if (status & PCNT_EVT_L_LIM) {
            s_ch2_total_count += (int32_t)PCNT_LOW_LIMIT - 1;
        }
    }
    portEXIT_CRITICAL_ISR(&pcnt_spinlock);
    
    pcnt_counter_clear(CHANNEL2_PCNT_UNIT);
}

/**
 * @brief Initialize Pulse Counter for both channels
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t pcnt_init() {
    esp_err_t ret;
        // ========== Channel 1 Configuration (A on GPIO34, B on GPIO35 as ctrl) ==========
    pcnt_config_t pcnt_config_ch1 = {
        .pulse_gpio_num = CHANNEL1_PIN,     // A -> 34
        .ctrl_gpio_num  = CHANNEL2_PIN,     // B -> 35 (dipakai sebagai direction/control)
        // When ctrl pin is LOW -> use these modes (lctrl_mode)
        .lctrl_mode     = PCNT_MODE_KEEP,   // coba KEEP / REVERSE sesuai hasil tes
        // When ctrl pin is HIGH -> use these modes (hctrl_mode)
        .hctrl_mode     = PCNT_MODE_REVERSE,// ini biasanya membalikkan counting ketika B = HIGH
        // On pulse edges: increment on posedge, decrement on negedge
        .pos_mode       = PCNT_COUNT_INC,
        .neg_mode       = PCNT_COUNT_DEC,
        .counter_h_lim  = PCNT_HIGH_LIMIT,
        .counter_l_lim  = PCNT_LOW_LIMIT,
        .unit           = CHANNEL1_PCNT_UNIT,
        .channel        = PCNT_CHANNEL_0,
    };
    
    ret = pcnt_unit_config(&pcnt_config_ch1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CH1: PCNT unit config failed: %d", ret);
        return ret;
    }
    
    pcnt_counter_pause(CHANNEL1_PCNT_UNIT);
    pcnt_counter_clear(CHANNEL1_PCNT_UNIT);
    pcnt_event_enable(CHANNEL1_PCNT_UNIT, PCNT_EVT_H_LIM);
    pcnt_event_enable(CHANNEL1_PCNT_UNIT, PCNT_EVT_L_LIM);
    
    // ========== Channel 2 Configuration (Pin 34) ==========
    pcnt_config_t pcnt_config_ch2 = {
        .pulse_gpio_num = CHANNEL2_PIN,
        .ctrl_gpio_num = PCNT_PIN_NOT_USED,
        .lctrl_mode = PCNT_MODE_KEEP,
        .hctrl_mode = PCNT_MODE_KEEP,
        .pos_mode = PCNT_COUNT_INC,
        .neg_mode = PCNT_COUNT_INC,
        .counter_h_lim = PCNT_HIGH_LIMIT,
        .counter_l_lim = PCNT_LOW_LIMIT,
        .unit = CHANNEL2_PCNT_UNIT,
        .channel = PCNT_CHANNEL_0,
    };
    
    ret = pcnt_unit_config(&pcnt_config_ch2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CH2: PCNT unit config failed: %d", ret);
        return ret;
    }
    
    pcnt_counter_pause(CHANNEL2_PCNT_UNIT);
    pcnt_counter_clear(CHANNEL2_PCNT_UNIT);
    pcnt_event_enable(CHANNEL2_PCNT_UNIT, PCNT_EVT_H_LIM);
    pcnt_event_enable(CHANNEL2_PCNT_UNIT, PCNT_EVT_L_LIM);
    
    // ========== ISR Installation ==========
    ret = pcnt_isr_service_install(0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PCNT ISR service install failed: %d", ret);
        return ret;
    }
    
    pcnt_isr_handler_add(CHANNEL1_PCNT_UNIT, pcnt_ch1_isr_handler, NULL);
    pcnt_isr_handler_add(CHANNEL2_PCNT_UNIT, pcnt_ch2_isr_handler, NULL);
    
    pcnt_intr_enable(CHANNEL1_PCNT_UNIT);
    pcnt_intr_enable(CHANNEL2_PCNT_UNIT);

    pcnt_set_filter_value(CHANNEL1_PCNT_UNIT, 100);
    pcnt_set_filter_value(CHANNEL2_PCNT_UNIT, 10);
    pcnt_filter_enable(CHANNEL1_PCNT_UNIT);
    pcnt_filter_enable(CHANNEL2_PCNT_UNIT);

    ESP_LOGI(TAG, "PCNT initialized: CH1=GPIO%d, CH2=GPIO%d", CHANNEL1_PIN, CHANNEL2_PIN);
    return ESP_OK;
}

/**
 * @brief Get total count for Channel 1
 */
int32_t pcnt_ch1_get_count() {
    int16_t current_count;
    pcnt_get_counter_value(CHANNEL1_PCNT_UNIT, &current_count);
    
    portENTER_CRITICAL(&pcnt_spinlock);
    int32_t total_count = current_count + s_ch1_total_count;
    portEXIT_CRITICAL(&pcnt_spinlock);
    
    return total_count;
}

/**
 * @brief Get total count for Channel 2
 */
int32_t pcnt_ch2_get_count() {
    int16_t current_count;
    pcnt_get_counter_value(CHANNEL2_PCNT_UNIT, &current_count);
    
    portENTER_CRITICAL(&pcnt_spinlock);
    int32_t total_count = current_count + s_ch2_total_count;
    portEXIT_CRITICAL(&pcnt_spinlock);
    
    return total_count;
}

/**
 * @brief Get total encoder count with overflow handling (backwards compatibility - returns CH1)
 */
int32_t pcnt_get_total_value() {
    return pcnt_ch1_get_count();
}

/**
 * @brief Get error count for Channel 1
 */
uint32_t pcnt_ch1_get_error_count() {
    portENTER_CRITICAL(&pcnt_spinlock);
    uint32_t errors = s_ch1_error_count;
    portEXIT_CRITICAL(&pcnt_spinlock);
    return errors;
}

/**
 * @brief Get error count for Channel 2
 */
uint32_t pcnt_ch2_get_error_count() {
    portENTER_CRITICAL(&pcnt_spinlock);
    uint32_t errors = s_ch2_error_count;
    portEXIT_CRITICAL(&pcnt_spinlock);
    return errors;
}

/**
 * @brief Get error count (backwards compatibility - returns CH1)
 */
uint32_t pcnt_get_error_count() {
    return pcnt_ch1_get_error_count();
}

/**
 * @brief Reset Channel 1 count and error statistics
 */
void pcnt_ch1_reset() {
    portENTER_CRITICAL(&pcnt_spinlock);
    s_ch1_total_count = 0;
    s_ch1_error_count = 0;
    portEXIT_CRITICAL(&pcnt_spinlock);
    
    pcnt_counter_clear(CHANNEL1_PCNT_UNIT);
}

/**
 * @brief Reset Channel 2 count and error statistics
 */
void pcnt_ch2_reset() {
    portENTER_CRITICAL(&pcnt_spinlock);
    s_ch2_total_count = 0;
    s_ch2_error_count = 0;
    portEXIT_CRITICAL(&pcnt_spinlock);
    
    pcnt_counter_clear(CHANNEL2_PCNT_UNIT);
}

/**
 * @brief Reset both channels
 */
void pcnt_reset() {
    pcnt_ch1_reset();
    pcnt_ch2_reset();
}

/**
 * @brief Resume counting on Channel 1
 */
void pcnt_ch1_resume() {
    pcnt_counter_resume(CHANNEL1_PCNT_UNIT);
}

/**
 * @brief Resume counting on Channel 2
 */
void pcnt_ch2_resume() {
    pcnt_counter_resume(CHANNEL2_PCNT_UNIT);
}

/**
 * @brief Resume both channels
 */
void pcnt_resume() {
    pcnt_ch1_resume();
    //pcnt_ch2_resume();
}

/**
 * @brief Pause counting on Channel 1
 */
void pcnt_ch1_pause() {
    pcnt_counter_pause(CHANNEL1_PCNT_UNIT);
}

/**
 * @brief Pause counting on Channel 2
 */
void pcnt_ch2_pause() {
    pcnt_counter_pause(CHANNEL2_PCNT_UNIT);
}

/**
 * @brief Pause both channels
 */
void pcnt_pause() {
    pcnt_ch1_pause();
    pcnt_ch2_pause();
}

#endif // PCNT_CONFIGURATION