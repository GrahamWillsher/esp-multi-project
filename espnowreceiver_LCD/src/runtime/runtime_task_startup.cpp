#include "runtime/runtime_task_startup.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp32common/config/timing_config.h>
#include <WiFi.h>
#include <runtime_common_utils/device_temperature.h>

#include "common_lcd.h"
#include "config/wifi_setup.h"
#include "helpers.h"
#include "logging_config.h"
#include "memory/memory_sampler.h"
#include "mqtt/mqtt_client.h"
#include "mqtt/mqtt_task.h"
#include "runtime/display_update_queue.h"
#include "task_config.h"
#include "ui/runtime/ui_runtime.h"

namespace {

constexpr uint32_t kLvglTaskPeriodMs = 33;
constexpr uint32_t kNetworkUiRefreshMs = 1000;
constexpr uint32_t kTemperatureSampleIntervalMs = 5000;
constexpr uint32_t kMqttLauncherRetryMs = 2000;
constexpr uint32_t kMqttLauncherLogEveryAttempts = 5;

TaskHandle_t g_mqtt_task_handle = nullptr;
bool g_mqtt_task_started = false;

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
    uint32_t last_network_ui_ms = 0;
    uint32_t last_temperature_sample_ms = 0;

    for (;;) {
        bool has_snapshot = false;
        while (DisplayUpdateQueue::try_dequeue(snapshot)) {
            has_snapshot = true;
        }

        const uint32_t now_ms = millis();
        if (last_temperature_sample_ms == 0 ||
            (now_ms - last_temperature_sample_ms) >= kTemperatureSampleIntervalMs) {
            DeviceTemperature::tick(kTemperatureSampleIntervalMs);
            last_temperature_sample_ms = now_ms;
        }

        if (xSemaphoreTake(RTOS::lvgl_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (has_snapshot) {
                UI::Runtime::set_state(snapshot.soc_percent, snapshot.power_w);
            }

            if (last_network_ui_ms == 0 || (now_ms - last_network_ui_ms) >= kNetworkUiRefreshMs) {
                last_network_ui_ms = now_ms;

                char net_buf[80] = {};
                const bool ap_mode = WiFiSetup::is_ap_mode();
                const bool sta_mode = WiFiSetup::is_sta_connected();
                if (ap_mode && sta_mode) {
                    snprintf(net_buf, sizeof(net_buf),
                             "WiFi APSTA A:%s S:%s",
                             WiFi.softAPIP().toString().c_str(),
                             WiFi.localIP().toString().c_str());
                } else if (ap_mode) {
                    snprintf(net_buf, sizeof(net_buf), "WiFi AP %s", WiFi.softAPIP().toString().c_str());
                } else if (sta_mode) {
                    snprintf(net_buf, sizeof(net_buf), "WiFi STA %s", WiFi.localIP().toString().c_str());
                } else {
                    snprintf(net_buf, sizeof(net_buf), "WiFi disconnected");
                }

                UI::Runtime::set_network_status(net_buf, sta_mode || ap_mode);
            }

                if (RuntimeState::current_led_state_valid.load()) {
                UI::Runtime::set_led_state(
                    RuntimeState::current_led_color.load(),
                    RuntimeState::current_led_effect.load());
            }

            UI::Runtime::set_link_connected(MqttClient::isConnected());
            UI::Runtime::tick(now_ms);
            xSemaphoreGive(RTOS::lvgl_mutex);
        }

        smart_delay(kLvglTaskPeriodMs);
    }
}

void task_mqtt_launcher(void* /*param*/) {
    uint32_t attempts = 0;
    for (;;) {
        ++attempts;

        const BaseType_t result = xTaskCreatePinnedToCore(
            task_mqtt_client,
            "MqttClient",
            TaskConfig::MQTT_CLIENT_STACK,
            nullptr,
            TaskConfig::MQTT_CLIENT_PRIORITY,
            &g_mqtt_task_handle,
            TaskConfig::MQTT_CORE);

        if (result == pdPASS) {
            g_mqtt_task_started = true;
            LOG_INFO("RTOS", "MqttClient task started (attempt=%lu stack=%lu free_heap=%u min_free_heap=%u)",
                     static_cast<unsigned long>(attempts),
                     static_cast<unsigned long>(TaskConfig::MQTT_CLIENT_STACK),
                     static_cast<unsigned>(ESP.getFreeHeap()),
                     static_cast<unsigned>(esp_get_minimum_free_heap_size()));
            vTaskDelete(nullptr);
            return;
        }

        if ((attempts % kMqttLauncherLogEveryAttempts) == 1) {
            const size_t largest_8bit_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
            LOG_WARN("RTOS", "MqttClient task create failed (attempt=%lu free_heap=%u min_free_heap=%u largest_8bit=%u), retry in %lu ms",
                     static_cast<unsigned long>(attempts),
                     static_cast<unsigned>(ESP.getFreeHeap()),
                     static_cast<unsigned>(esp_get_minimum_free_heap_size()),
                     static_cast<unsigned>(largest_8bit_block),
                     static_cast<unsigned long>(kMqttLauncherRetryMs));
        }

        vTaskDelay(pdMS_TO_TICKS(kMqttLauncherRetryMs));
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
        // LVGL render task — dequeues snapshots, pumps lv_timer_handler()
        { task_lvgl,                          "task_lvgl",   TaskConfig::DISPLAY_RENDERER_STACK, TaskConfig::DISPLAY_RENDERER_PRIORITY, &RTOS::lvgl_task          },
    };

    for (const auto& task : tasks) {
        create_task_or_fail(task);
    }

    // MQTT launcher task — avoids boot-fatal if heap is briefly too low
    // while webserver/FS startup allocations are still settling.
    // It retries MqttClient creation until successful, then self-deletes.
    {
        const BaseType_t result = xTaskCreatePinnedToCore(
            task_mqtt_launcher,
            "MqttLaunch",
            3072,
            nullptr,
            TaskConfig::MQTT_CLIENT_PRIORITY,
            nullptr,
            TaskConfig::WORKER_CORE);

        if (result != pdPASS) {
            LOG_ERROR("RTOS", "Failed to create MQTT launcher task (free_heap=%u min_free_heap=%u)",
                      static_cast<unsigned>(ESP.getFreeHeap()),
                      static_cast<unsigned>(esp_get_minimum_free_heap_size()));
            // Non-fatal: receiver can still function for local UI/web paths.
        }
    }

    LOG_DEBUG("RTOS", "All runtime tasks created");
    return true;
}

}  // namespace RuntimeTaskStartup
