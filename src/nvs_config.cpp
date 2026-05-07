/**
 * @file nvs_config.cpp
 * @brief Non-volatile storage implementation
 */

#include "nvs_config.h"
#include "config.h"
#include "input_config.h"
#include <Preferences.h>

// =============================================================================
// MODULE VARIABLES
// =============================================================================
static Preferences prefs;
static bool nvs_initialized = false;

// =============================================================================
// INITIALIZATION
// =============================================================================

bool nvs_config_init() {
    if (nvs_initialized) {
        return true;
    }
    
    // Open preferences in RW mode
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        Serial.println("[NVS] ERROR: Failed to initialize preferences");
        return false;
    }
    
    nvs_initialized = true;
    Serial.println("[NVS] Initialized successfully");
    
    // Check if first run (no config version stored)
    if (!prefs.isKey("cfg_ver")) {
        Serial.println("[NVS] First run detected, saving defaults");
        StoredConfig_t defaults;
        nvs_config_get_defaults(&defaults);
        nvs_config_save(&defaults);
    }
    
    return true;
}

// =============================================================================
// DEFAULT CONFIGURATION
// =============================================================================

void nvs_config_get_defaults(StoredConfig_t* config) {
    if (!config) return;
    
    // Channel 1 defaults
    config->ch1_input_mode = MODE_UP;
    config->ch1_edge_mode = EDGE_RISING;
    config->ch1_preset_value = 10000;
    config->ch1_filter = 100;
    
    // Channel 2 defaults
    config->ch2_input_mode = MODE_UP;
    config->ch2_edge_mode = EDGE_RISING;
    config->ch2_preset_value = 10000;
    config->ch2_filter = 100;
    
    // Modbus defaults
    config->modbus_address = MODBUS_SLAVE_ID;
    config->modbus_baud = MODBUS_BAUD_RATE;
    
    config->config_version = CONFIG_VERSION;
}

// =============================================================================
// LOAD CONFIGURATION
// =============================================================================

bool nvs_config_load(StoredConfig_t* config) {
    if (!config) return false;
    
    if (!nvs_initialized) {
        nvs_config_init();
    }
    
    // Check stored version
    uint8_t stored_version = prefs.getUChar("cfg_ver", 0);
    if (stored_version != CONFIG_VERSION) {
        Serial.printf("[NVS] Config version mismatch (stored=%d, current=%d), using defaults\n",
                      stored_version, CONFIG_VERSION);
        nvs_config_get_defaults(config);
        return false;
    }
    
    // Load Channel 1
    config->ch1_input_mode = (InputMode)prefs.getUChar(NVS_CH1_MODE, MODE_UP);
    config->ch1_edge_mode = (EdgeMode)prefs.getUChar(NVS_CH1_EDGE, EDGE_RISING);
    config->ch1_preset_value = prefs.getInt(NVS_CH1_PRESET, 10000);
    config->ch1_filter = prefs.getUShort(NVS_CH1_FILTER, 100);
    
    // Load Channel 2
    config->ch2_input_mode = (InputMode)prefs.getUChar(NVS_CH2_MODE, MODE_UP);
    config->ch2_edge_mode = (EdgeMode)prefs.getUChar(NVS_CH2_EDGE, EDGE_RISING);
    config->ch2_preset_value = prefs.getInt(NVS_CH2_PRESET, 10000);
    config->ch2_filter = prefs.getUShort(NVS_CH2_FILTER, 100);
    
    // Load Modbus
    config->modbus_address = prefs.getUChar(NVS_MODBUS_ADDR, MODBUS_SLAVE_ID);
    config->modbus_baud = prefs.getUInt(NVS_MODBUS_BAUD, MODBUS_BAUD_RATE);
    
    config->config_version = stored_version;
    
    Serial.println("[NVS] Configuration loaded successfully");
    Serial.printf("  CH1: mode=%s, edge=%d, preset=%ld\n", 
                  input_mode_to_string(config->ch1_input_mode),
                  config->ch1_edge_mode, 
                  (long)config->ch1_preset_value);
    Serial.printf("  CH2: mode=%s, edge=%d, preset=%ld\n",
                  input_mode_to_string(config->ch2_input_mode),
                  config->ch2_edge_mode,
                  (long)config->ch2_preset_value);
    
    return true;
}

// =============================================================================
// SAVE CONFIGURATION
// =============================================================================

bool nvs_config_save(const StoredConfig_t* config) {
    if (!config) return false;
    
    if (!nvs_initialized) {
        nvs_config_init();
    }
    
    // Save Channel 1
    prefs.putUChar(NVS_CH1_MODE, (uint8_t)config->ch1_input_mode);
    prefs.putUChar(NVS_CH1_EDGE, (uint8_t)config->ch1_edge_mode);
    prefs.putInt(NVS_CH1_PRESET, config->ch1_preset_value);
    prefs.putUShort(NVS_CH1_FILTER, config->ch1_filter);
    
    // Save Channel 2
    prefs.putUChar(NVS_CH2_MODE, (uint8_t)config->ch2_input_mode);
    prefs.putUChar(NVS_CH2_EDGE, (uint8_t)config->ch2_edge_mode);
    prefs.putInt(NVS_CH2_PRESET, config->ch2_preset_value);
    prefs.putUShort(NVS_CH2_FILTER, config->ch2_filter);
    
    // Save Modbus
    prefs.putUChar(NVS_MODBUS_ADDR, config->modbus_address);
    prefs.putUInt(NVS_MODBUS_BAUD, config->modbus_baud);
    
    // Save version
    prefs.putUChar("cfg_ver", CONFIG_VERSION);
    
    Serial.println("[NVS] Configuration saved");
    return true;
}

// =============================================================================
// SINGLE VALUE SAVE/LOAD
// =============================================================================

bool nvs_save_input_mode(uint8_t channel, InputMode mode) {
    if (!nvs_initialized) nvs_config_init();
    
    const char* key = (channel == 0) ? NVS_CH1_MODE : NVS_CH2_MODE;
    prefs.putUChar(key, (uint8_t)mode);
    
    Serial.printf("[NVS] Saved CH%d mode: %s\n", channel + 1, input_mode_to_string(mode));
    return true;
}

bool nvs_save_edge_mode(uint8_t channel, EdgeMode edge) {
    if (!nvs_initialized) nvs_config_init();
    
    const char* key = (channel == 0) ? NVS_CH1_EDGE : NVS_CH2_EDGE;
    prefs.putUChar(key, (uint8_t)edge);
    
    Serial.printf("[NVS] Saved CH%d edge: %d\n", channel + 1, edge);
    return true;
}

bool nvs_save_preset(uint8_t channel, int32_t preset) {
    if (!nvs_initialized) nvs_config_init();
    
    const char* key = (channel == 0) ? NVS_CH1_PRESET : NVS_CH2_PRESET;
    prefs.putInt(key, preset);
    
    Serial.printf("[NVS] Saved CH%d preset: %ld\n", channel + 1, (long)preset);
    return true;
}

bool nvs_load_input_mode(uint8_t channel, InputMode* mode) {
    if (!mode) return false;
    if (!nvs_initialized) nvs_config_init();
    
    const char* key = (channel == 0) ? NVS_CH1_MODE : NVS_CH2_MODE;
    
    if (!prefs.isKey(key)) {
        *mode = MODE_UP; // Default
        return false;
    }
    
    *mode = (InputMode)prefs.getUChar(key, MODE_UP);
    return true;
}

// =============================================================================
// RESET TO DEFAULTS
// =============================================================================

bool nvs_config_reset() {
    if (!nvs_initialized) nvs_config_init();
    
    // Clear all keys in namespace
    prefs.clear();
    
    // Save defaults
    StoredConfig_t defaults;
    nvs_config_get_defaults(&defaults);
    nvs_config_save(&defaults);
    
    Serial.println("[NVS] Configuration reset to defaults");
    return true;
}

// =============================================================================
// APPLY CONFIGURATION
// =============================================================================

void nvs_config_apply(const StoredConfig_t* config) {
    if (!config) return;
    
    Serial.println("[NVS] Applying loaded configuration...");
    
    // Apply Channel 1 input config
    ChannelInputConfig_t ch1_cfg;
    ch1_cfg.input_mode = config->ch1_input_mode;
    ch1_cfg.edge_mode = config->ch1_edge_mode;
    ch1_cfg.filter_value = config->ch1_filter;
    ch1_cfg.invert_ina = false;
    ch1_cfg.invert_inb = false;
    input_config_set_mode(0, &ch1_cfg);
    
    // Apply Channel 2 input config
    ChannelInputConfig_t ch2_cfg;
    ch2_cfg.input_mode = config->ch2_input_mode;
    ch2_cfg.edge_mode = config->ch2_edge_mode;
    ch2_cfg.filter_value = config->ch2_filter;
    ch2_cfg.invert_ina = false;
    ch2_cfg.invert_inb = false;
    input_config_set_mode(1, &ch2_cfg);
    
    Serial.println("[NVS] Configuration applied");
}
