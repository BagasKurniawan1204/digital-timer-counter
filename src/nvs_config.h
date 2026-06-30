/**
 * @file nvs_config.h
 * @brief Non-volatile storage for configuration persistence
 * 
 * Stores counter/timer configuration in ESP32 NVS (flash) so settings
 * persist across power cycles. Includes input modes, preset values,
 * edge settings, and other user-configurable parameters.
 */

#ifndef NVS_CONFIG_H
#define NVS_CONFIG_H

#include <Arduino.h>
#include "config.h"
#include "CT_counter.h"
#include "CT_timer.h"
#include "input_config.h"

// =============================================================================
// NVS NAMESPACE AND KEYS
// =============================================================================
#define NVS_NAMESPACE       "ct_config"

// Channel 1 keys
#define NVS_CH1_FUNC        "ch1_func"
#define NVS_CH1_MODE        "ch1_mode"
#define NVS_CH1_OUT         "ch1_out"
#define NVS_CH1_EDGE        "ch1_edge"
#define NVS_CH1_PRESET      "ch1_preset"
#define NVS_CH1_FILTER      "ch1_filter"
#define NVS_CH1_TMODE       "ch1_tmode"
#define NVS_CH1_TPRESET     "ch1_tpre"

// Channel 2 keys
#define NVS_CH2_FUNC        "ch2_func"
#define NVS_CH2_MODE        "ch2_mode"
#define NVS_CH2_OUT         "ch2_out"
#define NVS_CH2_EDGE        "ch2_edge"
#define NVS_CH2_PRESET      "ch2_preset"
#define NVS_CH2_FILTER      "ch2_filter"
#define NVS_CH2_TMODE       "ch2_tmode"
#define NVS_CH2_TPRESET     "ch2_tpre"

// System keys
#define NVS_WIFI_SSID       "wifi_ssid"
#define NVS_WIFI_PASS       "wifi_pass"
#define NVS_MODBUS_ADDR     "mb_addr"
#define NVS_MODBUS_BAUD     "mb_baud"

// =============================================================================
// STORED CONFIGURATION STRUCTURE
// =============================================================================
typedef struct {
    // Channel 1 config
    ChannelFunction ch1_function;
    InputMode ch1_input_mode;
    OutputMode ch1_output_mode;
    EdgeMode ch1_edge_mode;
    int32_t ch1_preset_value;
    uint16_t ch1_filter;
    TimerOutputMode ch1_timer_mode;
    uint32_t ch1_timer_preset;
    
    // Channel 2 config
    ChannelFunction ch2_function;
    InputMode ch2_input_mode;
    OutputMode ch2_output_mode;
    EdgeMode ch2_edge_mode;
    int32_t ch2_preset_value;
    uint16_t ch2_filter;
    TimerOutputMode ch2_timer_mode;
    uint32_t ch2_timer_preset;
    
    // Modbus config
    uint8_t modbus_address;
    uint32_t modbus_baud;
    
    // Version for config migration
    uint8_t config_version;
} StoredConfig_t;

// Current config version (increment when structure changes)
#define CONFIG_VERSION 2

// =============================================================================
// FUNCTION DECLARATIONS
// =============================================================================

/**
 * @brief Initialize NVS storage
 * @return true on success
 */
bool nvs_config_init();

/**
 * @brief Load all configuration from NVS
 * @param config Pointer to config structure to fill
 * @return true if config loaded, false if using defaults
 */
bool nvs_config_load(StoredConfig_t* config);

/**
 * @brief Save all configuration to NVS
 * @param config Pointer to config structure to save
 * @return true on success
 */
bool nvs_config_save(const StoredConfig_t* config);

/**
 * @brief Save single channel input mode
 * @param channel Channel number (0 or 1)
 * @param mode Input mode to save
 * @return true on success
 */
bool nvs_save_input_mode(uint8_t channel, InputMode mode);

/**
 * @brief Save single channel edge mode
 * @param channel Channel number (0 or 1)
 * @param edge Edge mode to save
 * @return true on success
 */
bool nvs_save_edge_mode(uint8_t channel, EdgeMode edge);

/**
 * @brief Save single channel preset value
 * @param channel Channel number (0 or 1)
 * @param preset Preset value to save
 * @return true on success
 */
bool nvs_save_preset(uint8_t channel, int32_t preset);

/**
 * @brief Load channel input mode from NVS
 * @param channel Channel number (0 or 1)
 * @param mode Pointer to receive mode
 * @return true if found, false if using default
 */
bool nvs_load_input_mode(uint8_t channel, InputMode* mode);

/**
 * @brief Reset all NVS configuration to defaults
 * @return true on success
 */
bool nvs_config_reset();

/**
 * @brief Get default configuration values
 * @param config Pointer to config structure to fill with defaults
 */
void nvs_config_get_defaults(StoredConfig_t* config);

/**
 * @brief Apply loaded config to system
 * @param config Configuration to apply
 */
void nvs_config_apply(const StoredConfig_t* config);

#endif // NVS_CONFIG_H
