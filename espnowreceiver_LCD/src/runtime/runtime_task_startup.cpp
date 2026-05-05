#include "runtime/runtime_task_startup.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp32common/config/timing_config.h>
#include <esp32common/espnow/tx_scheduler.h>

#include "common_lcd.h"
#include "espnow/espnow_runtime.h"
#include "espnow/rx_state_machine.h"
#include <esp32common/espnow/common.h>
#include "helpers.h"
#include "logging_config.h"
#include "memory/memory_sampler.h"
#include "mqtt/mqtt_task.h"
#include "runtime/display_update_queue.h"
#include "task_config.h"
#include "ui/runtime/ui_runtime.h"

namespace {

constexpr uint32_t kLvglTaskPeriodMs = 33;

// ── Uniform task creation helper (mirrors espnowreceiver_2 pattern) ─────────
struct TaskDescriptor {
    TaskFunction_t task_fn;
    const char*    name;
    uint32_t       stack;
    UBaseType_t    priority;
    TaskHandle_t*  handle;  // nullptr if no stored handle is needed
};

void create_task_or_fail(const TaskDescriptor& task) {
    const BaseType_t result = xTaskCreatePinnedToCore(
        task.task_fn,
        task.name,
        task.stack,
        nullptr,
        task.priority,
        task.handle,
        TaskConfig::WORKER_CORE);

    if (result != pdPASS) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Failed to create task: %s", task.name);
        handle_error(ErrorSeverity::FATAL, "RTOS", msg);
    }
}

// ── LVGL render task ─────────────────────────────────────────────────────────
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
    RTOS::lvgl_mutex = xSemaphoreCreateMutex();
    if (RTOS::lvgl_mutex == nullptr) {
        handle_error(ErrorSeverity::FATAL, "RTOS", "Failed to create LVGL mutex");
        return false;
    }

    // CRITICAL: message routes must be registered before the ESP-NOW worker
    // task starts — prevents PROBE messages arriving before handlers exist.
    LOG_DEBUG("RTOS", "Setting up ESP-NOW runtime and message routes...");
    if (!ESPNowRuntime::prepare_runtime()) {
        handle_error(ErrorSeverity::FATAL, "RTOS", "Failed to prepare ESP-NOW runtime");
        return false;
    }
    LOG_DEBUG("RTOS", "ESP-NOW runtime ready");

    // TX scheduler — provisioned for MQTT + ESP-NOW coexistence.
    // queue_depth=16 gives CONTROL queue depth ~4 (30% of 16), sufficient to
    // buffer ACK frames during a 150 ms MQTT TCP burst window.
    // no_mem_retry_attempts=6 mirrors the espnowreceiver_2 baseline.
    EspnowTxScheduler::InitOptions tx_options{};
    tx_options.queue_depth = 16;
    tx_options.no_mem_retry_attempts = 6;   // avoid prolonged retry storms under sustained NO_MEM
    tx_options.retry_base_delay_ms = 8;     // longer backoff gives WiFi driver buffer pool time to recover
    tx_options.inter_frame_delay_ms = 8;    // wider spacing reduces immediate descriptor re-contention
    tx_options.task_priority = TaskConfig::ESPNOW_TX_PRIORITY;
    tx_options.task_stack = TaskConfig::ESPNOW_TX_STACK;
    tx_options.task_core = TaskConfig::WORKER_CORE;
    tx_options.task_name = "EspnowTx";
    if (!EspnowTxScheduler::init(tx_options)) {
        handle_error(ErrorSeverity::FATAL, "RTOS", "Failed to initialize ESP-NOW TX scheduler");
        return false;
    }

    if (!DisplayUpdateQueue::init(8)) {
        handle_error(ErrorSeverity::FATAL, "RTOS", "Failed to create display update queue");
        return false;
    }

    LOG_DEBUG("RTOS", "Runtime primitives created");
    return true;
}

bool start_runtime_tasks() {
    LOG_DEBUG("RTOS", "Creating FreeRTOS tasks...");

    const TaskDescriptor tasks[] = {
        // ESP-NOW worker — highest priority; processes inbound message queue
        { ESPNowRuntime::task_worker,         "task_espnow", TaskConfig::ESPNOW_WORKER_STACK,   TaskConfig::ESPNOW_WORKER_PRIORITY,   &RTOS::espnow_worker_task },
        // LVGL render task — dequeues snapshots, pumps lv_timer_handler()
        { task_lvgl,                          "task_lvgl",   TaskConfig::DISPLAY_RENDERER_STACK, TaskConfig::DISPLAY_RENDERER_PRIORITY, &RTOS::lvgl_task          },
    };

    for (const auto& task : tasks) {
        create_task_or_fail(task);
    }

    // MQTT client — pinned to Core 0 (WiFi/lwIP core) to keep TCP socket
    // operations in the same CPU context as the WiFi driver, eliminating
    // inter-core IPC overhead and reducing TX buffer contention with ESP-NOW.
    // The task itself gates on RxStateMachine::CONNECTED before doing any
    // broker work, so it does not generate radio traffic during reconnect.
    {
        const BaseType_t result = xTaskCreatePinnedToCore(
            task_mqtt_client,
            "MqttClient",
            TaskConfig::MQTT_CLIENT_STACK,
            nullptr,
            TaskConfig::MQTT_CLIENT_PRIORITY,
            nullptr,
            TaskConfig::MQTT_CORE);
        if (result != pdPASS) {
            handle_error(ErrorSeverity::FATAL, "RTOS", "Failed to create task: MqttClient");
        }
    }

    LOG_DEBUG("RTOS", "All runtime tasks created");
    return true;
}

}  // namespace RuntimeTaskStartup
