/**
 * @file connection_event_processor.h
 * @brief Common Event Processor Task (IDENTICAL for Both TX and RX)
 * 
 * This task is created identically by both transmitter and receiver projects.
 * It simply processes events from the queue periodically.
 * 
 * Both projects use the exact same code - NO device-specific customization needed!
 * This is 100% code reuse.
 */

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/**
 * @brief Event processor task function
 * 
 * Should be created as a FreeRTOS task during setup.
 * Runs periodically to process connection events.
 * 
 * This function is IDENTICAL for both transmitter and receiver!
 * 
 * @param param Task parameter (typically nullptr)
 */
void connection_event_processor_task(void* param);

/**
 * @brief Helper to create the event processor task
 * 
 * Convenience function that creates the task with appropriate settings.
 * Can be called from both TX and RX projects with identical code.
 * 
 * Usage:
 *   create_connection_event_processor(3, 0);  // Priority 3, Core 0
 * 
 * @param priority Task priority (1-10, higher = more important)
 * @param core Core to run on (0 or 1)
 * @return Task handle (nullptr if creation failed)
 */
TaskHandle_t create_connection_event_processor(uint8_t priority = 3, uint8_t core = 0);
