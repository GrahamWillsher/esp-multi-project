#include "runtime_task_startup.h"

#include "../common.h"
#include "../helpers.h"
#include "task_config.h"
#include <esp32common/config/timing_config.h>

#include "../display/display_update_queue.h"
#include "../espnow/espnow_tasks.h"
#include "../espnow/rx_state_machine.h"
#include "../mqtt/mqtt_task.h"

#include <espnow_discovery.h>

namespace RuntimeTaskStartup {

namespace {
struct TaskDescriptor {
    TaskFunction_t task_fn;
    const char* name;
    uint32_t stack;
    UBaseType_t priority;
    TaskHandle_t* handle;
};

void create_task_or_fail(const TaskDescriptor& task) {
    BaseType_t result = xTaskCreatePinnedToCore(
        task.task_fn,
        task.name,
        task.stack,
        NULL,
        task.priority,
        task.handle,
        TaskConfig::WORKER_CORE
    );

    if (result != pdPASS) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Failed to create task: %s", task.name);
        handle_error(ErrorSeverity::FATAL, "RTOS", msg);
    }
}
} // namespace

void create_runtime_primitives() {
    // Create mutex for TFT display access
    RTOS::tft_mutex = xSemaphoreCreateMutex();
    if (RTOS::tft_mutex == NULL) {
        handle_error(ErrorSeverity::FATAL, "RTOS", "Failed to create TFT mutex");
    }
    LOG_DEBUG("MAIN", "TFT mutex created");

    // Create ESP-NOW message queue
    ESPNow::queue = xQueueCreate(ESPNow::QUEUE_SIZE, sizeof(espnow_queue_msg_t));
    if (ESPNow::queue == NULL) {
        handle_error(ErrorSeverity::FATAL, "RTOS", "Failed to create ESP-NOW queue");
    }
    LOG_DEBUG("MAIN", "ESP-NOW queue created (size=%d)", ESPNow::QUEUE_SIZE);

    // CRITICAL: Setup message routes BEFORE starting worker task
    // This prevents race condition where PROBE messages arrive before handlers are registered
    LOG_DEBUG("MAIN", "Setting up ESP-NOW message routes...");
    setup_message_routes();
    LOG_DEBUG("MAIN", "ESP-NOW message routes initialized");

    // Initialize decoupled display snapshot queue
    DisplayUpdateQueue::init();
}

void start_runtime_tasks(TaskFunction_t led_renderer_task_fn) {
    // Create FreeRTOS tasks
    LOG_DEBUG("MAIN", "Creating FreeRTOS tasks...");

    const TaskDescriptor tasks[] = {
        // Task: ESP-NOW Worker (highest priority for message processing)
        { task_espnow_worker, "ESPNowWorker", TaskConfig::ESPNOW_WORKER_STACK, TaskConfig::ESPNOW_WORKER_PRIORITY, &RTOS::task_espnow_worker },
        // Task: Display Renderer (decoupled from ESP-NOW worker)
        { DisplayUpdateQueue::task_renderer, "DisplayRenderer", TaskConfig::DISPLAY_RENDERER_STACK, TaskConfig::DISPLAY_RENDERER_PRIORITY, &RTOS::task_display_renderer },
        // Task: MQTT Client (low priority, receives spec data)
        { task_mqtt_client, "MqttClient", TaskConfig::MQTT_CLIENT_STACK, TaskConfig::MQTT_CLIENT_PRIORITY, NULL },
        // Task: LED Renderer (always-on effect loop)
        { led_renderer_task_fn, "LedRenderer", TaskConfig::LED_RENDERER_STACK, TaskConfig::LED_RENDERER_PRIORITY, &RTOS::task_indicator },
    };

    for (const auto& task : tasks) {
        create_task_or_fail(task);
    }

    // Start periodic announcement using common discovery component
    // (creates its own internal task, no need to wrap it)
    LOG_DEBUG("MAIN", "Starting periodic announcement task...");
    EspnowDiscovery::instance().start(
        []() -> bool {
            const auto state = RxStateMachine::instance().connection_state();
            return state == RxStateMachine::ConnectionState::CONNECTED ||
                   state == RxStateMachine::ConnectionState::ACTIVE ||
                   state == RxStateMachine::ConnectionState::STALE;
        },
        TimingConfig::ANNOUNCEMENT_INTERVAL_MS,
        TaskConfig::ANNOUNCEMENT_PRIORITY,
        TaskConfig::ANNOUNCEMENT_TASK_STACK
    );

    LOG_DEBUG("MAIN", "All tasks created successfully");
}

} // namespace RuntimeTaskStartup
