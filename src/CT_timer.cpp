#include "CT_timer.h"
#include "globals.h"

CT_timer::CT_timer(uint8_t channel, uint8_t pulse_pin, uint8_t ctrl_pin, uint8_t output_pin) {
    _channel = channel;
    _pulse_pin = pulse_pin;
    _ctrl_pin = ctrl_pin;
    _output_pin = output_pin;
    
    _config.output_mode = TIMER_OND;
    _config.preset_time_ms = 1000;
    _config.debounce_time_ms = 20;
    _config.reset_on_preset = false;
    
    _enabled = false;
    _output_state = false;
    _current_time_ms = 0;
    _last_process_time = millis();
    
    _last_pulse_raw = false;
    _last_pulse_change = 0;
    _pulse_active = false;
    
    _last_ctrl_raw = false;
    _last_ctrl_change = 0;
    _ctrl_active = false;
    
    _flicker_phase = false;
    
    pinMode(_pulse_pin, INPUT_PULLUP);
    pinMode(_ctrl_pin, INPUT_PULLUP);
    pinMode(_output_pin, OUTPUT);
    digitalWrite(_output_pin, LOW);
}

void CT_timer::setOutputMode(TimerOutputMode mode) {
    _config.output_mode = mode;
    reset();
}

void CT_timer::setPresetTime(uint32_t ms) {
    _config.preset_time_ms = ms;
}

void CT_timer::setDebounceTime(uint32_t ms) {
    _config.debounce_time_ms = ms;
}

void CT_timer::enable() {
    _enabled = true;
    _last_process_time = millis();
}

void CT_timer::disable() {
    _enabled = false;
    _output_state = false;
    digitalWrite(_output_pin, LOW);
}

void CT_timer::reset() {
    _current_time_ms = 0;
    _output_state = false;
    _flicker_phase = false;
    digitalWrite(_output_pin, LOW);
}

uint32_t CT_timer::getCurrentTime() {
    return _current_time_ms;
}

uint32_t CT_timer::getPresetTime() {
    return _config.preset_time_ms;
}

bool CT_timer::getOutputState() {
    return _output_state;
}

void CT_timer::process() {
    if (!_enabled) return;
    
    uint32_t now = millis();
    uint32_t dt = now - _last_process_time;
    _last_process_time = now;
    
    // Read and debounce PULSE pin
    bool pulse_raw = !digitalRead(_pulse_pin); // Active LOW -> true when pressed
    if (pulse_raw != _last_pulse_raw) {
        _last_pulse_change = now;
        _last_pulse_raw = pulse_raw;
    }
    if ((now - _last_pulse_change) >= _config.debounce_time_ms) {
        if (_pulse_active && !pulse_raw) {
            // Pulse went low (user released) - reset timer!
            reset();
        }
        _pulse_active = pulse_raw;
    }
    
    // Read and debounce CTRL pin (Pause)
    bool ctrl_raw = !digitalRead(_ctrl_pin);
    if (ctrl_raw != _last_ctrl_raw) {
        _last_ctrl_change = now;
        _last_ctrl_raw = ctrl_raw;
    }
    if ((now - _last_ctrl_change) >= _config.debounce_time_ms) {
        _ctrl_active = ctrl_raw;
    }
    
    // Stop processing timer count if pulse is absent OR ctrl (pause) is active
    if (!_pulse_active || _ctrl_active) {
        return;
    }
    
    // Accumulate time
    _current_time_ms += dt;
    
    // Evaluate logic depending on mode
    if (_config.output_mode == TIMER_OND) {
        // ON Delay
        if (_current_time_ms >= _config.preset_time_ms) {
            _output_state = true;
            if (_config.reset_on_preset) {
                // If it auto rests
                reset();
                _output_state = true; // Wait it just reset, maybe brief pulse
            }
        }
    } 
    else if (_config.output_mode == TIMER_FLK) {
        // Flicker output mode logic
        if (_current_time_ms >= _config.preset_time_ms) {
            _flicker_phase = !_flicker_phase; 
            _output_state = _flicker_phase;
            _current_time_ms = 0; // reset for next phase
        }
    }
    
    digitalWrite(_output_pin, _output_state ? HIGH : LOW);
}