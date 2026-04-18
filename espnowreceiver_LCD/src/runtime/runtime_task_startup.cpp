#include "runtime/runtime_task_startup.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp32common/config/timing_config.h>
#include <espnow_discovery.h>

#include "common_lcd.h"
#include "espnow/espnow_runtime.h"
#include "espnow/rx_state_machine.h"
#include <esp32common/espnow/common.h>
#include "helpers.h"
#include "logging_config.h"
#include "mqtt/mqtt_task.h"
#include "runtime/display_update_queue.h"
#include "task_config.h"
#include "ui/runtime/ui_runtime.h"

namespace {

constexpr uint32_t kLvglTaskPeriodMs = 33;

void task_lvgl(void* /*param*/) {
    DisplayUpdateQueue::snapshot_t snapshot{};

    for (;;) {
        bool has_snapshot = false;
        while (DisplayUpdateQueue::try_dequeue(snapshot)) {
            has_snapshot = true;
        }

        if (xSemaphoreTake(RTOS::lvgl_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (has_snapshot) {
                UI::Runtime::set_state(snapshot.soc_percent, snapshot.power_w);
            }
            UI::Runtime::set_link_connected(ESPNowRuntime::is_connected());
            UI::Runtime::tick(millis());
            xSemaphoreGive(RTOS::lvgl_mutex);
        }

        smart_delay(kLvglTaskPeriodMs);
    }
}

}  // namespace

namespace RuntimeTaskStartup {

bool create_runtime_primitives() {
    if (RTOS::lvgl_mutex == nullptr) {
        RTOS::lvgl_mutex = xSemaphoreCreateMutex();
    }

    if (RTOS::lvgl_mutex == nullptr) {
        handle_error(ErrorSeverity::FATAL, "RTOS", "Failed to create LVGL mutex");
        return false;
    }

    if (!ESPNowRuntime::prepare_runtime()) {
        handle_error(ErrorSeverity::FATAL, "RTOS", "Failed to prepare ESP-NOW runtime");
        return false;
    }

    const bool queue_ok = DisplayUpdateQueue::init(8);
    if (!queue_ok) {
        handle_error(ErrorSeverity::FATAL, "RTOS", "Failed to create display update queue");
        return false;
    }

    LOG_DEBUG("RTOS", "Runtime primitives created");
    return true;
}

bool start_runtime_tasks() {
    if (RTOS::lvgl_task == nullptr) {
        const BaseType_t rc = xTaskCreatePinnedToCore(
            task_lvgl,
            "task_lvgl",
            TaskConfig::DISPLAY_RENDERER_STACK,
            nullptr,
            TaskConfig::DISPLAY_RENDERER_PRIORITY,
            &RTOS::lvgl_task,
            TaskConfig::WORKER_CORE);

        if (rc != pdPASS) {
            handle_error(ErrorSeverity::FATAL, "RTOS", "Failed to create LVGL task");
            return false;
        }
    }

    if (RTOS::espnow_worker_task == nullptr) {
        const BaseType_t rc = xTaskCreatePinnedToCore(
            ESPNowRuntime::task_worker,
            "task_espnow",
            TaskConfig::ESPNOW_WORKER_STACK,
            nullptr,
            TaskConfig::ESPNOW_WORKER_PRIORITY,
            &RTOS::espnow_worker_task,
            TaskConfig::WORKER_CORE);
        if (rc != pdPASS) {
            handle_error(ErrorSeverity::FATAL, "RTOS", "Failed to create ESP-NOW worker task");
            return false;
        }
    }

    if (RTOS::mqtt_client_task == nullptr) {
        const BaseType_t rc = xTaskCreatePinnedToCore(
            task_mqtt_client,
            "task_mqtt",
            TaskConfig::MQTT_CLIENT_STACK,
            nullptr,
            TaskConfig::MQTT_CLIENT_PRIORITY,
            &RTOS::mqtt_client_task,
            TaskConfig::WORKER_CORE);
        if (rc != pdPASS) {
            handle_error(ErrorSeverity::FATAL, "RTOS", "Failed to create MQTT task");
            return false;
        }
    }

    // Start periodic ESP-NOW discovery announcements early (receiver_2 behavior).
    // This lets the receiver advertise presence before state-machine CONNECTING
    // timeout can elapse on quiet channels.
    EspnowDiscovery::instance().start(
        []() -> bool {
            const auto state = RxStateMachine::instance().connection_state();
            return state == RxStateMachine::ConnectionState::CONNECTED ||
                   state == RxStateMachine::ConnectionState::ACTIVE ||
                   state == RxStateMachine::ConnectionState::STALE;
        },
        TimingConfig::ANNOUNCEMENT_INTERVAL_MS,
        TaskConfig::ANNOUNCEMENT_PRIORITY,
        TaskConfig::ANNOUNCEMENT_TASK_STACK);

    if (!EspnowDiscovery::instance().is_running()) {
        handle_error(ErrorSeverity::FATAL, "RTOS", "Failed to start discovery task");
        return false;
    }

    LOG_INFO("RTOS", "Discovery task state: running=%s suspended=%s interval=%lu ms",
             EspnowDiscovery::instance().is_running() ? "yes" : "no",
             EspnowDiscovery::instance().is_suspended() ? "yes" : "no",
             static_cast<unsigned long>(TimingConfig::ANNOUNCEMENT_INTERVAL_MS));

    LOG_DEBUG("RTOS", "Runtime tasks started");
    return true;
}

}  // namespace RuntimeTaskStartup
