#ifndef COUNTER_OPERATION_H
#define COUNTER_OPERATION_H

/**
 * @file counter_operation.h
 * @brief PCNT-based high speed counter declarations
 * 
 * Provides dual-channel pulse counting with:
 * - Overflow handling via ISR
 * - Configurable input modes
 * - Thread-safe count access
 */

#include <Arduino.h>
#include "driver/pcnt.h"
#include "config.h"
#include "globals.h"

// =============================================================================
// FUNCTION DECLARATIONS
// =============================================================================

/**
 * @brief ISR handler for Channel 1 overflow events
 */
void IRAM_ATTR pcnt_ch1_isr_handler(void *arg);

/**
 * @brief ISR handler for Channel 2 overflow events
 */
void IRAM_ATTR pcnt_ch2_isr_handler(void *arg);

/**
 * @brief Initialize both PCNT channels
 * @return ESP_OK on success, error code on failure
 */
esp_err_t pcnt_init();

/**
 * @brief Get total count for Channel 1 (with overflow handling)
 * @return Current total count value
 */
int32_t pcnt_ch1_get_count();

/**
 * @brief Get total count for Channel 2 (with overflow handling)
 * @return Current total count value
 */
int32_t pcnt_ch2_get_count();

/**
 * @brief Reset Channel 1 counter to zero
 */
void pcnt_ch1_reset();

/**
 * @brief Reset Channel 2 counter to zero
 */
void pcnt_ch2_reset();

/**
 * @brief Resume counting on Channel 1
 */
void pcnt_ch1_resume();

/**
 * @brief Resume counting on Channel 2
 */
void pcnt_ch2_resume();

/**
 * @brief Pause counting on Channel 1
 */
void pcnt_ch1_pause();

/**
 * @brief Pause counting on Channel 2
 */
void pcnt_ch2_pause();

/**
 * @brief Manually increment Channel 1 counter
 * @param value Amount to increment by
 */
void pcnt_ch1_manual_increment(int32_t value);

/**
 * @brief Manually decrement Channel 1 counter
 * @param value Amount to decrement by
 */
void pcnt_ch1_manual_decrement(int32_t value);

/**
 * @brief Manually increment Channel 2 counter
 * @param value Amount to increment by
 */
void pcnt_ch2_manual_increment(int32_t value);

/**
 * @brief Manually decrement Channel 2 counter
 * @param value Amount to decrement by
 */
void pcnt_ch2_manual_decrement(int32_t value);

// =============================================================================
// CONVENIENCE FUNCTIONS (operate on both channels)
// =============================================================================

/**
 * @brief Get total count (Channel 1 - backwards compatibility)
 */
int32_t pcnt_get_total_value();

/**
 * @brief Get error count (Channel 1 - backwards compatibility)
 */
uint32_t pcnt_get_error_count();

/**
 * @brief Reset both channels
 */
void pcnt_reset();

/**
 * @brief Resume both channels
 */
void pcnt_resume();

/**
 * @brief Pause both channels
 */
void pcnt_pause();

#endif // COUNTER_OPERATION_H