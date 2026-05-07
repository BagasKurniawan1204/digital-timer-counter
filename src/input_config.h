#ifndef INPUT_CONFIG_H
#define INPUT_CONFIG_H

/**
 * @file input_config.h
 * @brief Flexible input mode configuration for PCNT channels
 * 
 * Supports CT4-style input modes:
 * - MODE_UP:  Count up only on INA pulses
 * - MODE_DN:  Count down only on INA pulses
 * - MODE_UDA: INA counts up, INB counts down (separate inputs)
 * - MODE_UDC: Quadrature encoder mode (A/B phase detection)
 */

#include <Arduino.h>
#include "driver/pcnt.h"
#include "CT_counter.h"

// =============================================================================
// EDGE COUNTING OPTIONS
// =============================================================================
enum EdgeMode {
    EDGE_RISING,        // Count on rising edge only (1x)
    EDGE_FALLING,       // Count on falling edge only (1x)
    EDGE_BOTH           // Count on both edges (2x)
};

// =============================================================================
// CHANNEL CONFIGURATION STRUCTURE
// =============================================================================
typedef struct {
    InputMode input_mode;       // UP, DN, UDA, UDC
    EdgeMode edge_mode;         // Rising, falling, or both edges
    uint16_t filter_value;      // Glitch filter (0-1023 APB cycles)
    bool invert_ina;            // Invert INA signal
    bool invert_inb;            // Invert INB signal
} ChannelInputConfig_t;

// =============================================================================
// FUNCTION DECLARATIONS
// =============================================================================

/**
 * @brief Configure PCNT channel with specified input mode
 * @param channel Channel number (0 or 1)
 * @param config Input configuration
 * @return ESP_OK on success
 */
esp_err_t input_config_set_mode(uint8_t channel, const ChannelInputConfig_t* config);

/**
 * @brief Get current input configuration for channel
 * @param channel Channel number (0 or 1)
 * @return Current configuration
 */
ChannelInputConfig_t input_config_get(uint8_t channel);

/**
 * @brief Initialize input configuration with defaults
 * @param channel Channel number (0 or 1)
 * @param mode Initial input mode
 */
void input_config_init(uint8_t channel, InputMode mode);

/**
 * @brief Get string name for input mode (for debug/display)
 */
const char* input_mode_to_string(InputMode mode);

#endif // INPUT_CONFIG_H
