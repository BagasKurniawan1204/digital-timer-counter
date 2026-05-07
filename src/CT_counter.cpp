/**
 * @file CT_counter.cpp
 * @brief CT4-style counter class implementation
 * 
 * Provides high-level counter operations:
 * - Preset value comparison with output trigger
 * - Multiple input modes (UP, DN, UDA, UDC)
 * - Output modes (N, F, C) for different trigger behaviors
 * - Scaling factor support
 */

#include "CT_counter.h"
#include "counter_operation.h"
#include "input_config.h"
#include "nvs_config.h"
#include "globals.h"
#include "rtos_tasks.h"

// =============================================================================
// STATIC INSTANCES (for global access)
// =============================================================================
static CT_counter* g_counter_instances[2] = {nullptr, nullptr};

// =============================================================================
// CONSTRUCTOR
// =============================================================================

CT_counter::CT_counter(uint8_t channel) : _channel(channel) {
    // Validate channel (1 or 2)
    if (_channel < 1) _channel = 1;
    if (_channel > 2) _channel = 2;
    
    // Initialize with defaults
    _config.input_mode = MODE_UP;
    _config.output_mode = OUTPUT_N;
    _config.preset_value = 10000;
    _config.current_value = 0;
    _config.scaling_factor = 1;
    _config.count_inhibit = false;
    _config.reset_on_preset = false;
    
    _enabled = false;
    _output_state = false;
    
    // Register global instance
    g_counter_instances[_channel - 1] = this;
    
    Serial.printf("[CT_counter] Channel %d initialized\n", _channel);
}

// =============================================================================
// CONFIGURATION METHODS
// =============================================================================

void CT_counter::setInputMode(InputMode mode) {
    _config.input_mode = mode;
    
    // Apply to hardware
    ChannelInputConfig_t hw_config = input_config_get(_channel - 1);
    hw_config.input_mode = mode;
    input_config_set_mode(_channel - 1, &hw_config);
    
    // Save to NVS
    nvs_save_input_mode(_channel - 1, mode);
    
    // Update RTOS shared data
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        systemData.channel[_channel - 1].input_mode = mode;
        xSemaphoreGive(dataMutex);
    }
    
    Serial.printf("[CT_counter] CH%d input mode set to %s\n", 
                  _channel, input_mode_to_string(mode));
}

void CT_counter::setOutputMode(OutputMode mode) {
    _config.output_mode = mode;
    
    const char* mode_names[] = {"N (Normal)", "F (One-shot)", "C (Carry)", "None"};
    Serial.printf("[CT_counter] CH%d output mode set to %s\n", 
                  _channel, mode_names[mode]);
}

void CT_counter::setPresetValue(int32_t value) {
    _config.preset_value = value;
    
    // Update RTOS shared data
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        systemData.channel[_channel - 1].preset_value = value;
        xSemaphoreGive(dataMutex);
    }
    
    // Save to NVS
    nvs_save_preset(_channel - 1, value);
    
    Serial.printf("[CT_counter] CH%d preset value set to %ld\n", 
                  _channel, (long)value);
}

void CT_counter::setScalingFactor(uint16_t factor) {
    if (factor == 0) factor = 1;  // Prevent division by zero
    _config.scaling_factor = factor;
    
    Serial.printf("[CT_counter] CH%d scaling factor set to %u\n", 
                  _channel, factor);
}

// =============================================================================
// CONTROL METHODS
// =============================================================================

void CT_counter::enable() {
    _enabled = true;
    _output_state = false;
    
    // Resume PCNT counting
    pcnt_resume();
    
    // Update RTOS shared data
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        systemData.channel[_channel - 1].enabled = true;
        xSemaphoreGive(dataMutex);
    }
    
    Serial.printf("[CT_counter] CH%d enabled\n", _channel);
}

void CT_counter::disable() {
    _enabled = false;
    
    // Update RTOS shared data
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        systemData.channel[_channel - 1].enabled = false;
        xSemaphoreGive(dataMutex);
    }
    
    Serial.printf("[CT_counter] CH%d disabled\n", _channel);
}

void CT_counter::reset() {
    // Reset the hardware counter and software total
    if (_channel == 1) {
        pcnt_ch1_reset();
    } else {
        pcnt_ch2_reset();
    }
    
    _config.current_value = 0;
    _output_state = false;
    
    // Turn off output pin
    uint8_t output_pin = (_channel == 1) ? OUTPUT_CH1_PIN : OUTPUT_CH2_PIN;
    digitalWrite(output_pin, LOW);
    
    // Update RTOS shared data
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        systemData.channel[_channel - 1].current_value = 0;
        systemData.channel[_channel - 1].output_state = false;
        xSemaphoreGive(dataMutex);
    }
    
    Serial.printf("[CT_counter] CH%d reset\n", _channel);
}

void CT_counter::inhibit(bool state) {
    _config.count_inhibit = state;
    
    if (state) {
        pcnt_pause();
    } else if (_enabled) {
        pcnt_resume();
    }
    
    Serial.printf("[CT_counter] CH%d count inhibit: %s\n", 
                  _channel, state ? "ON" : "OFF");
}

void CT_counter::manualIncrement() {
    if (_channel == 1) {
        pcnt_ch1_manual_increment(1);
    } else {
        pcnt_ch2_manual_increment(1);
    }
    process(); // Re-evaluate output state
}

void CT_counter::manualDecrement() {
    if (_channel == 1) {
        pcnt_ch1_manual_decrement(1);
    } else {
        pcnt_ch2_manual_decrement(1);
    }
    process(); // Re-evaluate output state
}

// =============================================================================
// GETTER METHODS
// =============================================================================

int32_t CT_counter::getCurrentValue() {
    // Get raw hardware count
    int32_t raw_count;
    if (_channel == 1) {
        raw_count = pcnt_ch1_get_count();
    } else {
        raw_count = pcnt_ch2_get_count();
    }
    
    // Apply scaling factor
    _config.current_value = (raw_count / _config.scaling_factor);
    
    // In Count Down mode, the value starts at PRESET and decrements (raw_count decreases into negative)
    // So if raw_count is 0, it shows preset. If -1, preset-1...
    if (_config.input_mode == MODE_DN) {
        _config.current_value += _config.preset_value;
    }
    
    return _config.current_value;
}

int32_t CT_counter::getPresetValue() {
    return _config.preset_value;
}

InputMode CT_counter::getInputMode() {
    return _config.input_mode;
}

bool CT_counter::isEnabled() {
    return _enabled;
}

bool CT_counter::getOutputState() {
    return _output_state;
}

// =============================================================================
// PROCESSING (call periodically)
// =============================================================================

void CT_counter::process() {
    if (_config.count_inhibit) {
        return;
    }
    
    // Get current scaled count
    int32_t current = getCurrentValue();
    int32_t preset = _config.preset_value;
    
    uint8_t output_pin = (_channel == 1) ? OUTPUT_CH1_PIN : OUTPUT_CH2_PIN;
    bool previous_output = _output_state;
    
    // Check preset condition based on counting direction
    bool preset_reached = false;
    
    switch (_config.input_mode) {
        case MODE_UP:
        case MODE_UDA:
        case MODE_UDC:
            // Counting up - trigger when count >= preset
            preset_reached = (current >= preset);
            break;
            
        case MODE_DN:
            // Counting down - trigger when count <= 0 (or negative)
            preset_reached = (current <= 0);
            break;
    }
    
    // Handle output based on output mode
    switch (_config.output_mode) {
        case OUTPUT_N:
            // Normal mode: output ON when preset reached, OFF when below
            _output_state = preset_reached;
            digitalWrite(output_pin, _output_state ? HIGH : LOW);
            if (preset_reached && !previous_output) {
                xEventGroupSetBits(systemEvents, EVENT_PRESET_REACHED);
                Serial.printf("[CT_counter] CH%d Preset reached! CV=%ld PV=%ld\n", 
                              _channel, (long)current, (long)preset);
            }
            break;
            
        case OUTPUT_F:
            // One-shot mode: pulse HIGH briefly when preset is first reached
            if (preset_reached && !previous_output) {
                _output_state = true;
                digitalWrite(output_pin, HIGH);
                xEventGroupSetBits(systemEvents, EVENT_PRESET_REACHED);
                Serial.printf("[CT_counter] CH%d Preset reached (F)! CV=%ld PV=%ld\n", 
                              _channel, (long)current, (long)preset);
            } else if (_output_state) {
                // Clear the one-shot after one cycle
                _output_state = false;
                digitalWrite(output_pin, LOW);
            }
            break;
            
        case OUTPUT_C:
            // Carry mode: pulse on overflow/underflow
            // This is handled by PCNT overflow interrupts
            // Just maintain current state here
            break;
            
        case OUTPUT_NONE:
            // No output
            _output_state = false;
            digitalWrite(output_pin, LOW);
            break;
    }
    
    // Auto-reset if configured and preset reached
    if (preset_reached && _config.reset_on_preset) {
        reset();
    }
    
    // Update RTOS shared data
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        systemData.channel[_channel - 1].current_value = current;
        systemData.channel[_channel - 1].output_state = _output_state;
        xSemaphoreGive(dataMutex);
    }
}

// =============================================================================
// GLOBAL ACCESS FUNCTIONS
// =============================================================================

CT_counter* getCounterInstance(uint8_t channel) {
    if (channel < 1 || channel > 2) return nullptr;
    return g_counter_instances[channel - 1];
}
