#ifndef CT_COUNTER_H
#define CT_COUNTER_H

#include <Arduino.h>
#include "config.h"

/**
 * @file CT_counter.h
 * @brief CT4-style counter class with configurable input modes
 * 
 * Supports multiple counting modes similar to Autonics CT4 series:
 * - UP: Count up only
 * - DN: Count down only  
 * - UD-A: Input A counts up, Input B counts down
 * - UD-C: Quadrature encoder mode (rotary encoder)
 */

// Input mode enumeration
enum InputMode {
    MODE_UP,    // Count up only on pulse input
    MODE_DN,    // Count down only on pulse input
    MODE_UDA,   // Input A = count up, Input B = count down
    MODE_UDC    // Quadrature encoder (rotary encoder mode)
};

// Output mode enumeration
enum OutputMode {
    OUTPUT_N,       // N output - normal output at count match
    OUTPUT_F,       // F output - one-shot pulse at count match
    OUTPUT_C,       // C output - carry/borrow output
    OUTPUT_NONE     // No output
};

// Counter configuration structure
struct CounterConfig {
    InputMode input_mode;
    OutputMode output_mode;
    int32_t preset_value;       // Target count value (PV)
    int32_t current_value;      // Current count value (CV)
    uint16_t scaling_factor;    // Count scaling multiplier
    bool count_inhibit;         // Inhibit counting when true
    bool reset_on_preset;       // Auto-reset when preset reached
};

class CT_counter {
private:
    uint8_t _channel;           // Channel number (1 or 2)
    CounterConfig _config;      // Counter configuration
    bool _enabled;              // Counter enable state
    bool _output_state;         // Current output state

public:
    CT_counter(uint8_t channel);
    
    // Configuration methods
    void setInputMode(InputMode mode);
    void setOutputMode(OutputMode mode);
    void setPresetValue(int32_t value);
    void setScalingFactor(uint16_t factor);
    
    // Control methods
    void enable();
    void disable();
    void reset();
    void inhibit(bool state);
    void manualIncrement();
    void manualDecrement();
    
    // Getters
    int32_t getCurrentValue();
    int32_t getPresetValue();
    InputMode getInputMode();
    bool isEnabled();
    bool getOutputState();
    
    // Processing (call in loop)
    void process();
};

// =============================================================================
// GLOBAL ACCESS
// =============================================================================

/**
 * @brief Get counter instance by channel number
 * @param channel Channel number (1 or 2)
 * @return Pointer to CT_counter instance or nullptr
 */
CT_counter* getCounterInstance(uint8_t channel);

#endif // CT_COUNTER_H
