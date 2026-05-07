/**
 * @file rtos_tasks.cpp
 * @brief FreeRTOS task implementations
 */

#include "rtos_tasks.h"
#include "config.h"
#include "globals.h"
#include "counter_operation.h"
#include "timer_operation.h"
#include "modbus_rtu.h"
#include "web_server.h"
#include "CT_counter.h"

// =============================================================================
// GLOBAL DEFINITIONS
// =============================================================================

// Task handles
TaskHandle_t counterTaskHandle = NULL;
TaskHandle_t timerTaskHandle = NULL;
TaskHandle_t inputTaskHandle = NULL;
TaskHandle_t modbusTaskHandle = NULL;
TaskHandle_t webTaskHandle = NULL;

// Queues
QueueHandle_t pcntEventQueue = NULL;
QueueHandle_t dataUpdateQueue = NULL;

// Mutexes
SemaphoreHandle_t dataMutex = NULL;

// Event groups
EventGroupHandle_t systemEvents = NULL;

// Shared system data
SystemData_t systemData = {0};

// =============================================================================
// INITIALIZATION
// =============================================================================

void rtos_init() {
    Serial.println("RTOS: Initializing resources...");
    
    // Create mutex for data protection
    dataMutex = xSemaphoreCreateMutex();
    if (dataMutex == NULL) {
        Serial.println("RTOS ERROR: Failed to create dataMutex");
    }
    
    // Create event group
    systemEvents = xEventGroupCreate();
    if (systemEvents == NULL) {
        Serial.println("RTOS ERROR: Failed to create systemEvents");
    }
    
    // Create PCNT event queue (for ISR to task communication)
    pcntEventQueue = xQueueCreate(QUEUE_SIZE_COUNTER_EVENT, sizeof(PcntEvent_t));
    if (pcntEventQueue == NULL) {
        Serial.println("RTOS ERROR: Failed to create pcntEventQueue");
    }
    
    // Create data update queue
    dataUpdateQueue = xQueueCreate(QUEUE_SIZE_DATA_UPDATE, sizeof(uint8_t));
    if (dataUpdateQueue == NULL) {
        Serial.println("RTOS ERROR: Failed to create dataUpdateQueue");
    }
    
    // Initialize shared data structure
    for (int i = 0; i < 2; i++) {
        systemData.channel[i].current_value = 0;
        systemData.channel[i].preset_value = 0;
        systemData.channel[i].frequency_hz = 0;
        systemData.channel[i].input_mode = 0;  // MODE_UP
        systemData.channel[i].enabled = false;
        systemData.channel[i].preset_reached = false;
        systemData.channel[i].output_state = false;
        systemData.channel[i].error_count = 0;
    }
    
    systemData.timer.elapsed_ms = 0;
    systemData.timer.running = false;
    systemData.timer.enabled = false;
    systemData.uptime_seconds = 0;
    
    Serial.println("RTOS: Resources initialized");
}

void rtos_start_tasks() {
    Serial.println("RTOS: Starting tasks...");
    
    // =========================================================================
    // CORE 1 TASKS (Real-time processing)
    // =========================================================================
    
    xTaskCreatePinnedToCore(
        counterTask,
        "Counter",
        STACK_SIZE_COUNTER,
        NULL,
        PRIORITY_COUNTER,
        &counterTaskHandle,
        CORE_REALTIME
    );
    
    xTaskCreatePinnedToCore(
        timerTask,
        "Timer",
        STACK_SIZE_TIMER,
        NULL,
        PRIORITY_TIMER,
        &timerTaskHandle,
        CORE_REALTIME
    );
    
    xTaskCreatePinnedToCore(
        inputTask,
        "Input",
        STACK_SIZE_INPUT,
        NULL,
        PRIORITY_INPUT,
        &inputTaskHandle,
        CORE_REALTIME
    );
    
    // =========================================================================
    // CORE 0 TASKS (Communication)
    // =========================================================================
    
    xTaskCreatePinnedToCore(
        modbusTask,
        "Modbus",
        STACK_SIZE_MODBUS,
        NULL,
        PRIORITY_MODBUS,
        &modbusTaskHandle,
        CORE_COMMS
    );
    
    xTaskCreatePinnedToCore(
        webTask,
        "WebServer",
        STACK_SIZE_WEBSERVER,
        NULL,
        PRIORITY_WEBSERVER,
        &webTaskHandle,
        CORE_COMMS
    );
    
    Serial.println("RTOS: All tasks started");
    Serial.printf("RTOS: Counter task on Core %d\n", CORE_REALTIME);
    Serial.printf("RTOS: Communication tasks on Core %d\n", CORE_COMMS);
}

// =============================================================================
// TASK IMPLEMENTATIONS
// =============================================================================

/**
 * @brief Counter processing task
 * Handles PCNT events, frequency calculation, preset comparison
 */
void counterTask(void *pvParameters) {
    PcntEvent_t event;
    TickType_t lastWakeTime = xTaskGetTickCount();
    TickType_t lastFreqCalc = xTaskGetTickCount();
    int32_t lastCount[2] = {0, 0};
    
    Serial.println("Counter Task: Started");
    
    for (;;) {
        // Check for PCNT overflow events from ISR
        while (xQueueReceive(pcntEventQueue, &event, 0) == pdTRUE) {
            if (event.channel < 2) {
                if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    // Overflow events are already handled by ISR updating s_chX_total_count
                    // This queue is for notification purposes
                    xSemaphoreGive(dataMutex);
                }
            }
        }
        
        // Update current count values
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            // Read current counts from PCNT hardware (raw, used for frequency or raw tracking)
            // The CT_counter process will update systemData with the scaled/adjusted value
            xSemaphoreGive(dataMutex);
        }
        
        // Calculate frequency every 100ms

        if ((xTaskGetTickCount() - lastFreqCalc) >= pdMS_TO_TICKS(100)) {
            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                for (int ch = 0; ch < 2; ch++) {
                    int32_t currentCount = systemData.channel[ch].current_value;
                    int32_t delta = currentCount - lastCount[ch];
                    systemData.channel[ch].frequency_hz = delta * 10; // 100ms window
                    lastCount[ch] = currentCount;
                }
                xEventGroupSetBits(systemEvents, EVENT_COUNTER_UPDATED);
                xSemaphoreGive(dataMutex);
            }
            lastFreqCalc = xTaskGetTickCount();
        }
        
        // Process CT_counter instances (handles preset comparison & output)
        CT_counter* ctr1 = getCounterInstance(1);
        CT_counter* ctr2 = getCounterInstance(2);
        if (ctr1) ctr1->process();
        if (ctr2) ctr2->process();
        
        // Run at 1ms interval
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(TASK_INTERVAL_COUNTER));
    }
}

/**
 * @brief Timer processing task
 */
void timerTask(void *pvParameters) {
    TickType_t lastWakeTime = xTaskGetTickCount();
    
    Serial.println("Timer Task: Started");
    
    for (;;) {
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (systemData.timer.enabled && systemData.timer.running) {
                // Get elapsed time from hardware timer
                uint64_t elapsed_us = 0;
                stopwatch_get_elapsed(&elapsed_us);
                systemData.timer.elapsed_ms = (uint32_t)(elapsed_us / 1000ULL);
                
                // Also update the global for backwards compatibility
                elapsed_ms = systemData.timer.elapsed_ms;
                
                xEventGroupSetBits(systemEvents, EVENT_TIMER_UPDATED);
            }
            xSemaphoreGive(dataMutex);
        }
        
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(10)); // 10ms interval
    }
}

/**
 * @brief Input handling task (sensor debounce)
 */
void inputTask(void *pvParameters) {
    TickType_t lastWakeTime = xTaskGetTickCount();
    
    bool lastState = LOW;
    uint32_t lastChangeTime = 0;
    bool handledHigh = false;
    
    Serial.println("Input Task: Started");
    
    for (;;) {
        // Only process when timer is enabled
        bool timerEnabled = false;
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            timerEnabled = systemData.timer.enabled;
            xSemaphoreGive(dataMutex);
        }
        
        if (timerEnabled) {
            bool raw = digitalRead(SENSOR_INPUT_PIN);
            uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
            
            if (raw != lastState) {
                lastChangeTime = now;
                lastState = raw;
            } else if ((now - lastChangeTime) >= SENSOR_DEBOUNCE_MS) {
                if (raw == HIGH && !handledHigh) {
                    handledHigh = true;
                    
                    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                        if (!systemData.timer.running) {
                            stopwatch_start();
                            systemData.timer.running = true;
                            Serial.println("Input: Timer STARTED");
                        } else {
                            stopwatch_pause();
                            systemData.timer.running = false;
                            Serial.println("Input: Timer PAUSED");
                        }
                        xSemaphoreGive(dataMutex);
                    }
                } else if (raw == LOW && handledHigh) {
                    handledHigh = false;
                }
            }
        }
        
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(5)); // 5ms interval
    }
}

/**
 * @brief Modbus communication task
 */
void modbusTask(void *pvParameters) {
    TickType_t lastWakeTime = xTaskGetTickCount();
    
    Serial.println("Modbus Task: Started");
    
    for (;;) {
        // Process Modbus requests
        mb.task();
        
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(TASK_INTERVAL_MODBUS));
    }
}

/**
 * @brief Web server task
 */
void webTask(void *pvParameters) {
    TickType_t lastWakeTime = xTaskGetTickCount();
    TickType_t lastWsUpdate = xTaskGetTickCount();
    
    Serial.println("WebServer Task: Started");
    
    for (;;) {
        // Cleanup disconnected WebSocket clients
        ws.cleanupClients();
        
        // Send WebSocket updates at configured interval
        if ((xTaskGetTickCount() - lastWsUpdate) >= pdMS_TO_TICKS(WS_UPDATE_INTERVAL_MS)) {
            // Check if there's new data to send
            EventBits_t bits = xEventGroupGetBits(systemEvents);
            if (bits & (EVENT_COUNTER_UPDATED | EVENT_TIMER_UPDATED)) {
                sendRealtimeData();
                // Clear the bits we handled
                xEventGroupClearBits(systemEvents, EVENT_COUNTER_UPDATED | EVENT_TIMER_UPDATED);
            }
            lastWsUpdate = xTaskGetTickCount();
        }
        
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(TASK_INTERVAL_WEBSERVER));
    }
}

// =============================================================================
// HELPER FUNCTIONS (Thread-safe data access)
// =============================================================================

ChannelData_t getChannelData(uint8_t channel) {
    ChannelData_t data = {0};
    if (channel < 2 && xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        data = systemData.channel[channel];
        xSemaphoreGive(dataMutex);
    }
    return data;
}

void updateChannelCount(uint8_t channel, int32_t count) {
    if (channel < 2 && xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        systemData.channel[channel].current_value = count;
        xSemaphoreGive(dataMutex);
    }
}

TimerData_t getTimerData() {
    TimerData_t data = {0};
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        data = systemData.timer;
        xSemaphoreGive(dataMutex);
    }
    return data;
}
