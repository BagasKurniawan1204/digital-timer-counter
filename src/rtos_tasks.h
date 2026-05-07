#ifndef RTOS_TASKS_H
#define RTOS_TASKS_H

/**
 * @file rtos_tasks.h
 * @brief FreeRTOS task declarations and inter-task communication
 * 
 * Architecture:
 * - Core 0 (PRO): Communication tasks (WiFi, WebServer, Modbus)
 * - Core 1 (APP): Real-time tasks (Counter processing, Timer, Input)
 * 
 * Data flow:
 * - Counter/Timer tasks update shared data with mutex protection
 * - Communication tasks read shared data for transmission
 * - Event queues for ISR-to-task communication
 */

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>

// =============================================================================
// TASK HANDLES
// =============================================================================
extern TaskHandle_t counterTaskHandle;
extern TaskHandle_t timerTaskHandle;
extern TaskHandle_t inputTaskHandle;
extern TaskHandle_t modbusTaskHandle;
extern TaskHandle_t webTaskHandle;

// =============================================================================
// QUEUES
// =============================================================================
extern QueueHandle_t pcntEventQueue;        // PCNT overflow events from ISR
extern QueueHandle_t dataUpdateQueue;       // Data updates to communication tasks

// =============================================================================
// MUTEXES / SEMAPHORES
// =============================================================================
extern SemaphoreHandle_t dataMutex;         // Protects shared counter/timer data

// =============================================================================
// EVENT GROUPS
// =============================================================================
extern EventGroupHandle_t systemEvents;

// Event bits
#define EVENT_COUNTER_UPDATED   (1 << 0)
#define EVENT_TIMER_UPDATED     (1 << 1)
#define EVENT_CONFIG_CHANGED    (1 << 2)
#define EVENT_PRESET_REACHED    (1 << 3)

// =============================================================================
// SHARED DATA STRUCTURE
// =============================================================================

/**
 * @brief Counter channel data (thread-safe access via dataMutex)
 */
typedef struct {
    int32_t current_value;          // Current count value
    int32_t preset_value;           // Preset/target value
    int32_t frequency_hz;           // Calculated frequency
    uint8_t input_mode;             // InputMode enum value
    bool enabled;                   // Channel enabled
    bool preset_reached;            // CV == PV flag
    bool output_state;              // Output pin state
    uint32_t error_count;           // Error counter
} ChannelData_t;

/**
 * @brief Timer data structure
 */
typedef struct {
    uint32_t elapsed_ms;            // Elapsed time in milliseconds
    bool running;                   // Timer running state
    bool enabled;                   // Timer enabled
} TimerData_t;

/**
 * @brief System shared data (protected by dataMutex)
 */
typedef struct {
    ChannelData_t channel[2];       // Channel 0 and Channel 1
    TimerData_t timer;              // Stopwatch timer
    uint32_t uptime_seconds;        // System uptime
} SystemData_t;

extern SystemData_t systemData;

// =============================================================================
// PCNT EVENT STRUCTURE (for ISR queue)
// =============================================================================
typedef struct {
    uint8_t channel;                // Which channel (0 or 1)
    int32_t overflow_delta;         // Amount to add (PCNT_HIGH_LIMIT or PCNT_LOW_LIMIT)
} PcntEvent_t;

// =============================================================================
// FUNCTION DECLARATIONS
// =============================================================================

/**
 * @brief Initialize all RTOS resources (queues, mutexes, event groups)
 * Call before creating tasks
 */
void rtos_init();

/**
 * @brief Create and start all RTOS tasks
 * Call after rtos_init() and hardware initialization
 */
void rtos_start_tasks();

/**
 * @brief Counter processing task (runs on Core 1)
 * Handles PCNT events, frequency calculation, preset comparison
 */
void counterTask(void *pvParameters);

/**
 * @brief Timer processing task (runs on Core 1)
 * Handles stopwatch updates
 */
void timerTask(void *pvParameters);

/**
 * @brief Input handling task (runs on Core 1)
 * Handles sensor input with debouncing
 */
void inputTask(void *pvParameters);

/**
 * @brief Modbus communication task (runs on Core 0)
 * Handles Modbus RTU polling
 */
void modbusTask(void *pvParameters);

/**
 * @brief Web server task (runs on Core 0)
 * Handles WebSocket updates and client cleanup
 */
void webTask(void *pvParameters);

// =============================================================================
// HELPER FUNCTIONS (Thread-safe data access)
// =============================================================================

/**
 * @brief Get channel data (thread-safe copy)
 */
ChannelData_t getChannelData(uint8_t channel);

/**
 * @brief Update channel count (thread-safe)
 */
void updateChannelCount(uint8_t channel, int32_t count);

/**
 * @brief Get timer data (thread-safe copy)
 */
TimerData_t getTimerData();

#endif // RTOS_TASKS_H
