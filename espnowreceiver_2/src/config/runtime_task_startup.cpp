#include "runtime_task_startup.h"

#include "../common.h"
#include "../helpers.h"
#include "task_config.h"
#include <esp32common/config/timing_config.h>

#include "../display/display_update_queue.h"
#include "../mqtt/mqtt_task.h"

#include "../memory/memory_sampler.h"

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

    // Initialize decoupled display snapshot queue
    DisplayUpdateQueue::init();
}

void start_runtime_tasks(TaskFunction_t led_renderer_task_fn) {
    // Create FreeRTOS tasks
    LOG_DEBUG("MAIN", "Creating FreeRTOS tasks...");

    const TaskDescriptor tasks[] = {
        // Task: Display Renderer (decoupled from MQTT worker)
        { DisplayUpdateQueue::task_renderer, "DisplayRenderer", TaskConfig::DISPLAY_RENDERER_STACK, TaskConfig::DISPLAY_RENDERER_PRIORITY, &RTOS::task_display_renderer },
        // Task: MQTT Client (low priority, receives spec data)
        { task_mqtt_client, "MqttClient", TaskConfig::MQTT_CLIENT_STACK, TaskConfig::MQTT_CLIENT_PRIORITY, NULL },
        // Task: LED Renderer (always-on effect loop)
        { led_renderer_task_fn, "LedRenderer", TaskConfig::LED_RENDERER_STACK, TaskConfig::LED_RENDERER_PRIORITY, &RTOS::task_indicator },
        // Task: Memory Sampler (background heap health monitoring)
        { MemorySampler::task_memory_sampler, "MemSampler", TaskConfig::MEMORY_SAMPLER_STACK, TaskConfig::MEMORY_SAMPLER_PRIORITY, NULL },
    };

    for (const auto& task : tasks) {
        create_task_or_fail(task);
    }

    LOG_DEBUG("MAIN", "All tasks created successfully");
}

} // namespace RuntimeTaskStartup
