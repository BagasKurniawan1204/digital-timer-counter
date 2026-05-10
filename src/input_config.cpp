/**
 * @file input_config.cpp
 * @brief Flexible input mode configuration implementation
 * 
 * Handles PCNT reconfiguration for different input modes at runtime.
 */

#include "input_config.h"
#include "config.h"
#include "globals.h"

// =============================================================================
// INTERNAL STATE
// =============================================================================

// Store current configuration for each channel
static ChannelInputConfig_t s_channel_config[2] = {
    {MODE_UP, EDGE_RISING, PCNT_FILTER_VALUE, false, false},  // Channel 0 default
    {MODE_UP, EDGE_RISING, PCNT_FILTER_VALUE, false, false}   // Channel 1 default
};

// Pin mappings per channel
static const int s_pulse_pins[2] = {COUNTER_CH1_PULSE_PIN, COUNTER_CH2_PULSE_PIN};
static const int s_ctrl_pins[2] = {COUNTER_CH1_CTRL_PIN, COUNTER_CH2_CTRL_PIN};
static const pcnt_unit_t s_pcnt_units[2] = {COUNTER_CH1_PCNT_UNIT, COUNTER_CH2_PCNT_UNIT};

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

/**
 * @brief Configure PCNT for MODE_UP (count up only)
 */
static esp_err_t configure_mode_up(uint8_t channel, const ChannelInputConfig_t* config) {
    pcnt_config_t pcnt_config = {
        .pulse_gpio_num = s_pulse_pins[channel],
        .ctrl_gpio_num = PCNT_PIN_NOT_USED,     // No control pin needed
        .lctrl_mode = PCNT_MODE_KEEP,
        .hctrl_mode = PCNT_MODE_KEEP,
        .pos_mode = PCNT_COUNT_INC,             // Rising edge = increment
        .neg_mode = PCNT_COUNT_DIS,             // Falling edge = disabled
        .counter_h_lim = PCNT_HIGH_LIMIT,
        .counter_l_lim = PCNT_LOW_LIMIT,
        .unit = s_pcnt_units[channel],
        .channel = PCNT_CHANNEL_0,
    };
    
    // Handle edge mode
    if (config->edge_mode == EDGE_BOTH) {
        pcnt_config.neg_mode = PCNT_COUNT_INC;  // Both edges increment
    } else if (config->edge_mode == EDGE_FALLING) {
        pcnt_config.pos_mode = PCNT_COUNT_DIS;
        pcnt_config.neg_mode = PCNT_COUNT_INC;
    }
    
    return pcnt_unit_config(&pcnt_config);
}

/**
 * @brief Configure PCNT for MODE_DN (count down only)
 */
static esp_err_t configure_mode_dn(uint8_t channel, const ChannelInputConfig_t* config) {
    pcnt_config_t pcnt_config = {
        .pulse_gpio_num = s_pulse_pins[channel],
        .ctrl_gpio_num = PCNT_PIN_NOT_USED,     // No control pin needed
        .lctrl_mode = PCNT_MODE_KEEP,
        .hctrl_mode = PCNT_MODE_KEEP,
        .pos_mode = PCNT_COUNT_DEC,             // Rising edge = decrement
        .neg_mode = PCNT_COUNT_DIS,             // Falling edge = disabled
        .counter_h_lim = PCNT_HIGH_LIMIT,
        .counter_l_lim = PCNT_LOW_LIMIT,
        .unit = s_pcnt_units[channel],
        .channel = PCNT_CHANNEL_0,
    };
    
    // Handle edge mode
    if (config->edge_mode == EDGE_BOTH) {
        pcnt_config.neg_mode = PCNT_COUNT_DEC;  // Both edges decrement
    } else if (config->edge_mode == EDGE_FALLING) {
        pcnt_config.pos_mode = PCNT_COUNT_DIS;
        pcnt_config.neg_mode = PCNT_COUNT_DEC;
    }
    
    return pcnt_unit_config(&pcnt_config);
}

/**
 * @brief Configure PCNT for MODE_UDA (INA up, INB down - separate inputs)
 * Uses two PCNT channels on the same unit:
 * - Channel 0: INA as pulse input, counts UP
 * - Channel 1: INB as pulse input, counts DOWN
 */
static esp_err_t configure_mode_uda(uint8_t channel, const ChannelInputConfig_t* config) {
    esp_err_t ret;
    
    // Channel 0: INA counts UP
    pcnt_config_t pcnt_config_up = {
        .pulse_gpio_num = s_pulse_pins[channel],    // INA
        .ctrl_gpio_num = PCNT_PIN_NOT_USED,
        .lctrl_mode = PCNT_MODE_KEEP,
        .hctrl_mode = PCNT_MODE_KEEP,
        .pos_mode = PCNT_COUNT_INC,                 // Rising edge = increment
        .neg_mode = PCNT_COUNT_DIS,
        .counter_h_lim = PCNT_HIGH_LIMIT,
        .counter_l_lim = PCNT_LOW_LIMIT,
        .unit = s_pcnt_units[channel],
        .channel = PCNT_CHANNEL_0,
    };
    
    ret = pcnt_unit_config(&pcnt_config_up);
    if (ret != ESP_OK) return ret;
    
    // Channel 1: INB counts DOWN
    pcnt_config_t pcnt_config_dn = {
        .pulse_gpio_num = s_ctrl_pins[channel],     // INB (using ctrl pin as second input)
        .ctrl_gpio_num = PCNT_PIN_NOT_USED,
        .lctrl_mode = PCNT_MODE_KEEP,
        .hctrl_mode = PCNT_MODE_KEEP,
        .pos_mode = PCNT_COUNT_DEC,                 // Rising edge = decrement
        .neg_mode = PCNT_COUNT_DIS,
        .counter_h_lim = PCNT_HIGH_LIMIT,
        .counter_l_lim = PCNT_LOW_LIMIT,
        .unit = s_pcnt_units[channel],
        .channel = PCNT_CHANNEL_1,                  // Use channel 1 on same unit
    };
    
    return pcnt_unit_config(&pcnt_config_dn);
}

/**
 * @brief Configure PCNT for MODE_UDC (Quadrature encoder)
 * A leads B = CW (increment)
 * B leads A = CCW (decrement)
 * 
 * Uses control pin to determine direction based on phase relationship.
 */
static esp_err_t configure_mode_udc(uint8_t channel, const ChannelInputConfig_t* config) {
    esp_err_t ret;
    
    // Configure Channel A: Pulse=INA, Control=INB
    // When INB is LOW and INA rises: count UP
    // When INB is HIGH and INA rises: count DOWN
    pcnt_config_t pcnt_config_a = {
        .pulse_gpio_num = s_pulse_pins[channel],    // INA (Encoder A)
        .ctrl_gpio_num = s_ctrl_pins[channel],      // INB (Encoder B)
        .lctrl_mode = PCNT_MODE_KEEP,               // When B=LOW: keep counting direction
        .hctrl_mode = PCNT_MODE_REVERSE,            // When B=HIGH: reverse direction
        .pos_mode = PCNT_COUNT_INC,                 // A rising edge: increment (or dec if reversed)
        .neg_mode = PCNT_COUNT_DEC,                 // A falling edge: decrement (or inc if reversed)
        .counter_h_lim = PCNT_HIGH_LIMIT,
        .counter_l_lim = PCNT_LOW_LIMIT,
        .unit = s_pcnt_units[channel],
        .channel = PCNT_CHANNEL_0,
    };
    
    ret = pcnt_unit_config(&pcnt_config_a);
    if (ret != ESP_OK) return ret;
    
    // Configure Channel B: Pulse=INB, Control=INA (for 4x resolution)
    // This gives full quadrature decoding
    pcnt_config_t pcnt_config_b = {
        .pulse_gpio_num = s_ctrl_pins[channel],     // INB (Encoder B)
        .ctrl_gpio_num = s_pulse_pins[channel],     // INA (Encoder A)
        .lctrl_mode = PCNT_MODE_REVERSE,            // When A=LOW: reverse direction
        .hctrl_mode = PCNT_MODE_KEEP,               // When A=HIGH: keep counting direction
        .pos_mode = PCNT_COUNT_INC,                 // B rising edge
        .neg_mode = PCNT_COUNT_DEC,                 // B falling edge
        .counter_h_lim = PCNT_HIGH_LIMIT,
        .counter_l_lim = PCNT_LOW_LIMIT,
        .unit = s_pcnt_units[channel],
        .channel = PCNT_CHANNEL_1,
    };
    
    return pcnt_unit_config(&pcnt_config_b);
}

// =============================================================================
// PUBLIC FUNCTIONS
// =============================================================================

esp_err_t input_config_set_mode(uint8_t channel, const ChannelInputConfig_t* config) {
    if (channel > 1 || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret;
    pcnt_unit_t unit = s_pcnt_units[channel];
    
    Serial.printf("Input Config: CH%d setting mode %s\n", 
                  channel + 1, input_mode_to_string(config->input_mode));
    
    // Pause counter during reconfiguration
    pcnt_counter_pause(unit);
    
    // Remove existing ISR handler
    pcnt_isr_handler_remove(unit);
    
    // Clear any pending interrupts
    pcnt_intr_disable(unit);
    
    // Configure based on mode
    switch (config->input_mode) {
        case MODE_UP:
            ret = configure_mode_up(channel, config);
            break;
        case MODE_DN:
            ret = configure_mode_dn(channel, config);
            break;
        case MODE_UDA:
            ret = configure_mode_uda(channel, config);
            break;
        case MODE_UDC:
            ret = configure_mode_udc(channel, config);
            break;
        default:
            ret = ESP_ERR_INVALID_ARG;
            break;
    }
    
    if (ret != ESP_OK) {
        Serial.printf("Input Config: Failed to configure mode: %d\n", ret);
        return ret;
    }
    
    // Re-enable events
    pcnt_event_enable(unit, PCNT_EVT_H_LIM);
    pcnt_event_enable(unit, PCNT_EVT_L_LIM);
    
    // Set filter
    pcnt_set_filter_value(unit, config->filter_value);
    pcnt_filter_enable(unit);
    
    // Re-add ISR handler
    extern void IRAM_ATTR pcnt_ch1_isr_handler(void *arg);
    extern void IRAM_ATTR pcnt_ch2_isr_handler(void *arg);
    
    if (channel == 0) {
        pcnt_isr_handler_add(unit, pcnt_ch1_isr_handler, NULL);
    } else {
        pcnt_isr_handler_add(unit, pcnt_ch2_isr_handler, NULL);
    }
    
    // Re-enable interrupts
    pcnt_intr_enable(unit);
    
    // Clear counter
    pcnt_counter_clear(unit);
    
    // Store configuration
    s_channel_config[channel] = *config;

    // Resume counter
    if (counter_enabled) {
        pcnt_counter_resume(unit);
    }
    
    Serial.printf("Input Config: CH%d mode set to %s successfully\n", 
                  channel + 1, input_mode_to_string(config->input_mode));
    
    return ESP_OK;
}

ChannelInputConfig_t input_config_get(uint8_t channel) {
    if (channel > 1) {
        ChannelInputConfig_t empty = {MODE_UP, EDGE_RISING, 0, false, false};
        return empty;
    }
    return s_channel_config[channel];
}

void input_config_init(uint8_t channel, InputMode mode) {
    if (channel > 1) return;
    
    ChannelInputConfig_t config = {
        .input_mode = mode,
        .edge_mode = EDGE_RISING,
        .filter_value = PCNT_FILTER_VALUE,
        .invert_ina = false,
        .invert_inb = false
    };
    
    s_channel_config[channel] = config;
}

const char* input_mode_to_string(InputMode mode) {
    switch (mode) {
        case MODE_UP:  return "UP";
        case MODE_DN:  return "DN";
        case MODE_UDA: return "UD-A";
        case MODE_UDC: return "UD-C";
        default:       return "UNKNOWN";
    }
}
