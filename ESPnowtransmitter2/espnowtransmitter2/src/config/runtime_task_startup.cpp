#include "runtime_task_startup.h"

#include "task_config.h"
#include "logging_config.h"
#include "network_config.h"

#include "../network/mqtt_task.h"

#include <Arduino.h>

namespace RuntimeTaskStartup {

namespace {
constexpr BaseType_t MQTT_TASK_CORE = 1;

void start_mqtt_task_if_enabled() {
    if (!config::features::MQTT_ENABLED) {
        return;
    }

    const BaseType_t result = xTaskCreatePinnedToCore(
        task_mqtt_loop,
        "mqtt_task",
        task_config::STACK_SIZE_MQTT,
        nullptr,
        task_config::PRIORITY_LOW,
        nullptr,
        MQTT_TASK_CORE
    );

    if (result != pdPASS) {
        LOG_ERROR("TASKS", "Failed to start MQTT task");
    } else {
        LOG_INFO("TASKS", "MQTT task started (Core %d)", MQTT_TASK_CORE);
    }
}

} // namespace

void start_runtime_tasks() {
    LOG_INFO("TASKS", "Starting MQTT transport runtime tasks");

    // Start MQTT task (lowest priority — background telemetry)
    start_mqtt_task_if_enabled();
}

} // namespace RuntimeTaskStartup
