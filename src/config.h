#ifndef CONFIG_H
#define CONFIG_H

/**
 * @file config.h
 * @brief Centralized configuration for ESP32 Timer & Counter device
 * 
 * All pin definitions, constants, and hardware configuration should be
 * defined here. This is the SINGLE SOURCE OF TRUTH for all hardware settings.
 */

// =============================================================================
// COUNTER CHANNEL 1 - High Speed Counter Input
// =============================================================================
#define COUNTER_CH1_PULSE_PIN       34      // Input A (pulse input)
#define COUNTER_CH1_CTRL_PIN        35      // Input B (direction/control)
#define COUNTER_CH1_PCNT_UNIT       PCNT_UNIT_0

// =============================================================================
// COUNTER CHANNEL 2 - High Speed Counter Input
// =============================================================================
#define COUNTER_CH2_PULSE_PIN       32      // Input A (pulse input)
#define COUNTER_CH2_CTRL_PIN        33      // Input B (direction/control)
#define COUNTER_CH2_PCNT_UNIT       PCNT_UNIT_1

// =============================================================================
// PCNT CONFIGURATION
// =============================================================================
#define PCNT_HIGH_LIMIT             32767
#define PCNT_LOW_LIMIT              -32768
#define PCNT_FILTER_VALUE           100     // Filter value in APB clock cycles (12.5ns per cycle at 80MHz)

// =============================================================================
// MODBUS RTU - RS485 Communication
// =============================================================================
#define MODBUS_RX_PIN               16      // RS485 RX
#define MODBUS_TX_PIN               17      // RS485 TX
#define MODBUS_SLAVE_ID             1
#define MODBUS_BAUD_RATE            9600

// =============================================================================
// SENSOR INPUT
// =============================================================================
#define SENSOR_INPUT_PIN            15      // External sensor/trigger input (Changed from 14 to avoid TFT_DC conflict)
#define SENSOR_DEBOUNCE_MS          20      // Debounce time in milliseconds

// =============================================================================
// TIMER CONFIGURATION
// =============================================================================
#define FREQ_TIMER_GROUP            TIMER_GROUP_0
#define FREQ_TIMER_IDX              TIMER_0
#define FREQ_MEASURE_INTERVAL_US    100000  // 100ms = 10Hz frequency measurement

#define STOPWATCH_TIMER_GROUP       TIMER_GROUP_1
#define STOPWATCH_TIMER_IDX         TIMER_0

// Timer prescaler: 80MHz / 80 = 1MHz (1us ticks)
#define TIMER_DIVIDER               80

// =============================================================================
// WEB SERVER / WIFI AP CONFIGURATION
// =============================================================================
#define WIFI_AP_SSID                "HSC_TIMER_AP"
#define WIFI_AP_PASSWORD            "PENSJOSS"
#define WEB_SERVER_PORT             80

// =============================================================================
// WEBSOCKET CONFIGURATION
// =============================================================================
#define WS_UPDATE_INTERVAL_MS       100     // 10Hz WebSocket update rate
#define WS_PATH                     "/ws"

// =============================================================================
// OUTPUT PINS (for future CT4-like output features)
// =============================================================================
#define OUTPUT_CH1_PIN              25      // Output channel 1 (comparison output)
#define OUTPUT_CH2_PIN              26      // Output channel 2 (comparison output)

// =============================================================================
// FREERTOS TASK CONFIGURATION
// =============================================================================
// Core assignments
#define CORE_COMMS                  0       // Communication tasks (WiFi, Modbus)
#define CORE_REALTIME               1       // Real-time tasks (Counter, Timer)

// Task priorities (higher number = higher priority)
#define PRIORITY_COUNTER            5       // Highest - real-time counting
#define PRIORITY_TIMER              4       // High - stopwatch timing
#define PRIORITY_INPUT              4       // High - sensor input handling
#define PRIORITY_MODBUS             3       // Medium - Modbus communication
#define PRIORITY_WEBSERVER          2       // Medium-low - Web requests
#define PRIORITY_STORAGE            1       // Low - NVS storage operations

// Task stack sizes (in words, not bytes)
#define STACK_SIZE_COUNTER          2048
#define STACK_SIZE_TIMER            2048
#define STACK_SIZE_INPUT            2048
#define STACK_SIZE_MODBUS           3072
#define STACK_SIZE_WEBSERVER        4096
#define STACK_SIZE_STORAGE          2048

// Queue sizes
#define QUEUE_SIZE_COUNTER_EVENT    10
#define QUEUE_SIZE_DATA_UPDATE      5

// Task intervals (ms)
#define TASK_INTERVAL_COUNTER       1       // 1ms counter processing
#define TASK_INTERVAL_MODBUS        10      // 10ms Modbus polling
#define TASK_INTERVAL_WEBSERVER     50      // 50ms WebSocket updates

// =============================================================================
// MODBUS REGISTER MAP
// =============================================================================
// Holding Registers (Read/Write where applicable)
#define MB_REG_CH1_COUNT_LO         100     // Channel 1 Count Lower Word (RO)
#define MB_REG_CH1_COUNT_HI         101     // Channel 1 Count Upper Word (RO)
#define MB_REG_CH1_FREQUENCY        102     // Channel 1 Frequency Hz (RO)
#define MB_REG_ELAPSED_TIMER        103     // Elapsed Timer ms (RO)
#define MB_REG_TIMER_ENABLE         104     // Timer Enable (RW)
#define MB_REG_TIMER_RESET          105     // Timer Reset (WO)
#define MB_REG_COUNT                6       // Total registers starting from 100

#endif // CONFIG_H
