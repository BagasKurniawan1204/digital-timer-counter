#ifndef PCNT_CONFIGURATION
#define PCNT_CONFIGURATION

#include <Arduino.h>
#include "driver/pcnt.h"
#include "esp_log.h"
extern portMUX_TYPE pcnt_spinlock;


// Configuration
#define ENCODER_SIGNAL_PIN      34  // GPIO34 for encoder channel A
#define ENCODER_CTRL_PIN        PCNT_PIN_NOT_USED  // No control signal
#define PCNT_HIGH_LIMIT         32767
#define PCNT_LOW_LIMIT          -32768

static const char* TAG = "encoder_pcnt";

static volatile int32_t s_total_encoder_count = 0;
static volatile uint32_t s_error_count = 0;

/**
 * @brief PCNT interrupt service routine for overflow handling
 * @note Called when counter reaches high/low limits
 */
void IRAM_ATTR pcnt_isr_handler(void *arg) {
    uint32_t status;
    esp_err_t ret = pcnt_get_event_status(PCNT_UNIT_0, &status);

    if (ret != ESP_OK) {
        ESP_DRAM_LOGE(TAG, "Failed to get PCNT event status: %d", ret);
        portENTER_CRITICAL_ISR(&pcnt_spinlock);
        s_error_count++;
        portEXIT_CRITICAL_ISR(&pcnt_spinlock);
        return;
    }
    
    int16_t current_count;
    pcnt_get_counter_value(PCNT_UNIT_0, &current_count);

    portENTER_CRITICAL_ISR(&pcnt_spinlock);
    {
        if ((status & PCNT_EVT_H_LIM) && (status & PCNT_EVT_L_LIM)) {
            // Critical system error
            ESP_DRAM_LOGW(TAG, "Simultaneous high/low limit trigger - resetting");
            s_total_encoder_count = 0;
            s_error_count++;
        } else if (status & PCNT_EVT_H_LIM) {
            s_total_encoder_count += (int32_t)PCNT_HIGH_LIMIT + 1;
        } else if (status & PCNT_EVT_L_LIM) {
            s_total_encoder_count += (int32_t)PCNT_LOW_LIMIT - 1;
        }
    }
    portEXIT_CRITICAL_ISR(&pcnt_spinlock);
    
    pcnt_counter_clear(PCNT_UNIT_0);
}

/**
 * @brief Initialize Pulse Counter for encoder reading
 * @return esp_err_t ESP_OK on success, error code on failure
 */
esp_err_t pcnt_init() {
    pcnt_config_t pcnt_config = {
        .pulse_gpio_num = ENCODER_SIGNAL_PIN,
        .ctrl_gpio_num = ENCODER_CTRL_PIN,
        .lctrl_mode = PCNT_MODE_KEEP,
        .hctrl_mode = PCNT_MODE_KEEP,
        .pos_mode = PCNT_COUNT_INC,
        .neg_mode = PCNT_COUNT_INC,
        .counter_h_lim = PCNT_HIGH_LIMIT,
        .counter_l_lim = PCNT_LOW_LIMIT,
        .unit = PCNT_UNIT_0,
        .channel = PCNT_CHANNEL_0,
    };
    
    esp_err_t ret = pcnt_unit_config(&pcnt_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PCNT unit config failed: %d", ret);
        return ret;
    }
    pcnt_set_filter_value(PCNT_UNIT_0, 1000); // 1 APB cycle = 12.5ns minimum pulse width
    pcnt_filter_enable(PCNT_UNIT_0);
    pcnt_counter_pause(PCNT_UNIT_0);
    pcnt_counter_clear(PCNT_UNIT_0);

    // Event configuration
    pcnt_event_enable(PCNT_UNIT_0, PCNT_EVT_H_LIM);
    pcnt_event_enable(PCNT_UNIT_0, PCNT_EVT_L_LIM);
    
    // ISR installation
    ret = pcnt_isr_service_install(0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "PCNT ISR service install failed: %d", ret);
        return ret;
    }
    
    pcnt_isr_handler_add(PCNT_UNIT_0, pcnt_isr_handler, NULL);
    pcnt_intr_enable(PCNT_UNIT_0);
//    pcnt_counter_resume(PCNT_UNIT_0);

    ESP_LOGI(TAG, "PCNT initialized successfully on GPIO %d", ENCODER_SIGNAL_PIN);
    return ESP_OK;
}

/**
 * @brief Get total encoder count with overflow handling
 * @return int32_t Total encoder count since initialization
 */
int32_t pcnt_get_total_value() {
    int16_t current_count;
    pcnt_get_counter_value(PCNT_UNIT_0, &current_count);
    
    portENTER_CRITICAL(&pcnt_spinlock);
    int32_t total_count = current_count + s_total_encoder_count;
    portEXIT_CRITICAL(&pcnt_spinlock);
    
    return total_count;
}

/**
 * @brief Get error count for monitoring reliability
 * @return uint32_t Number of errors detected since initialization
 */
uint32_t pcnt_get_error_count() {
    portENTER_CRITICAL(&pcnt_spinlock);
    uint32_t errors = s_error_count;
    portEXIT_CRITICAL(&pcnt_spinlock);
    return errors;
}

/**
 * @brief Reset encoder count and error statistics
 */
void pcnt_reset() {
    portENTER_CRITICAL(&pcnt_spinlock);
    s_total_encoder_count = 0;
    s_error_count = 0;
    portEXIT_CRITICAL(&pcnt_spinlock);
    
    pcnt_counter_clear(PCNT_UNIT_0);
}

void pcnt_resume() {
    pcnt_counter_resume(PCNT_UNIT_0);
}

void pcnt_pause() {
    pcnt_counter_pause(PCNT_UNIT_0);
}

#endif // PCNT_CONFIGURATION