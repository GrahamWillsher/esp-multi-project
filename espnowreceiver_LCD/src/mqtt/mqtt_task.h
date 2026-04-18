#ifndef MQTT_TASK_H
#define MQTT_TASK_H

/**
 * @brief FreeRTOS task for MQTT client connectivity
 * Subscribes to battery emulator static spec topics
 * @param parameter Unused task parameter
 */
void task_mqtt_client(void* parameter);

#endif // MQTT_TASK_H
