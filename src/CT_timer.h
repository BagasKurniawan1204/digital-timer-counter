#ifndef CT_TIMER_H
#define CT_TIMER_H

#include <Arduino.h>

/**
 * @brief CT4-style Timer class with configurable logic
 * Support OND (Signal ON Delay) and FLK (Flicker) modes.
 */

enum TimerOutputMode {
    TIMER_OND,      // Signal ON Delay
    TIMER_FLK       // Flicker
};

struct TimerConfig {
    TimerOutputMode output_mode;
    uint32_t preset_time_ms;
    uint32_t debounce_time_ms;
    bool reset_on_preset;       
};

class CT_timer {
private:
    uint8_t _channel;
    TimerConfig _config;
    
    // Hardware pins
    uint8_t _pulse_pin;
    uint8_t _ctrl_pin;
    uint8_t _output_pin;
    
    bool _enabled;
    bool _output_state;
    
    uint32_t _current_time_ms;
    uint32_t _last_process_time;
    
    // Debounce & State tracking
    bool _last_pulse_raw;
    uint32_t _last_pulse_change;
    bool _pulse_active;
    
    bool _last_ctrl_raw;
    uint32_t _last_ctrl_change;
    bool _ctrl_active;

    // Flicker tracking
    bool _flicker_phase; // true = ON time counting, false = OFF time counting

public:
    CT_timer(uint8_t channel, uint8_t pulse_pin, uint8_t ctrl_pin, uint8_t output_pin);
    
    void setOutputMode(TimerOutputMode mode);
    void setPresetTime(uint32_t ms);
    void setDebounceTime(uint32_t ms);
    
    void enable();
    void disable();
    void reset();
    
    uint32_t getCurrentTime();
    uint32_t getPresetTime();
    bool getOutputState();
    
    void process();
};

#endif // CT_TIMER_H