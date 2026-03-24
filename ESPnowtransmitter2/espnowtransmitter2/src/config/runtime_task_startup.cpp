#include "runtime_task_startup.h"

#include "task_config.h"
#include "logging_config.h"
#include "network_config.h"

#include "../espnow/transmission_task.h"
#include "../espnow/heartbeat_manager.h"
#include "../espnow/data_sender.h"
#include "../espnow/discovery_task.h"
#include "../network/mqtt_task.h"

#include <Arduino.h>

namespace RuntimeTaskStartup {

namespace {
constexpr uint8_t TRANSMISSION_TASK_CORE = 1;

void start_mqtt_task_if_enabled() {
    if (!config::features::MQTT_ENABLED) {
        return;
    }

    const BaseType_t result = xTaskCreate(
        task_mqtt_loop,
        "mqtt_task",
        task_config::STACK_SIZE_MQTT,
        nullptr,
        task_config::PRIORITY_LOW,
        nullptr
    );

    if (result != pdPASS) {
        LOG_ERROR("TASKS", "Failed to start MQTT task");
    }
}

} // namespace

void start_runtime_tasks() {
    LOG_DEBUG("ESPNOW", "Starting ESP-NOW tasks...");

    // Background transmission task (Priority 2 — LOW, Core 1)
    // Reads from EnhancedCache and transmits via ESP-NOW (non-blocking)
    TransmissionTask::instance().start(task_config::PRIORITY_LOW, TRANSMISSION_TASK_CORE);
    LOG_INFO("ESPNOW", "Background transmission task started (Priority 2, Core 1)");

    HeartbeatManager::instance().init();
    LOG_INFO("HEARTBEAT", "Heartbeat manager initialized (10s interval, ACK-based)");

#if CONFIG_CAN_ENABLED
    LOG_INFO("MAIN", "===== PHASE 4a: REAL BATTERY DATA =====");
    LOG_INFO("MAIN", "Using CAN bus data from datalayer");
    DataSender::instance().start();
    LOG_INFO("MAIN", "✓ Data sender started (real battery data)");
#else
    LOG_INFO("MAIN", "Using simulated test data (CAN disabled)");
    DataSender::instance().start();
#endif

    // Start discovery task (periodic announcements until receiver connects)
    DiscoveryTask::instance().start();

    // Start MQTT task (lowest priority — background telemetry)
    start_mqtt_task_if_enabled();
}

} // namespace RuntimeTaskStartup
