#pragma once

/**
 * @brief FreeRTOS task wrapper for MQTT operations
 * 
 * Manages MQTT connection, reconnection, and periodic publishing
 * in a low-priority background task.
 */
void task_mqtt_loop(void* parameter);
