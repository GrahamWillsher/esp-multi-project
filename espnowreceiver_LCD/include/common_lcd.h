#pragma once

#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

// ═══════════════════════════════════════════════════════════════════════
// common_lcd.h — Shared runtime handles for espnowreceiver_LCD
//
// Provides the namespace scaffolding that Phase B+ will populate.
// Replaces the TFT-coupled common.h from espnowreceiver_2.
//
// Display access is intentionally absent here: all UI updates go through
// UI::Runtime::set_state() to keep the LVGL layer decoupled.
// ═══════════════════════════════════════════════════════════════════════

namespace RTOS {

    // LVGL access mutex — all lv_obj_* calls outside the LVGL task must
    // take this lock. Initialised in Phase B RuntimeTaskStartup::init().
    extern SemaphoreHandle_t lvgl_mutex;

    // Task handles — populated by RuntimeTaskStartup::create_tasks().
    extern TaskHandle_t lvgl_task;
    extern TaskHandle_t display_renderer_task;
    extern TaskHandle_t mqtt_client_task;

    // Queue for display snapshots (producer: app/model, consumer: LVGL task).
    extern QueueHandle_t display_update_queue;

}  // namespace RTOS

namespace RuntimeState {

    enum class LedStatus : uint8_t {
        Unknown = 0,
        Ok = 1,
        Warning = 2,
        Error = 3,
        Updating = 4,
    };

    // Transport-neutral LED state used by MQTT/runtime UI paths.
    extern std::atomic<uint8_t> current_led_color;
    extern std::atomic<uint8_t> current_led_effect;
    extern std::atomic<uint8_t> current_led_status;
    extern std::atomic<bool> current_led_state_valid;
    extern std::atomic<bool> receiver_ota_led_override_active;

    // Legacy telemetry compatibility counters for web API shape.
    // LCD receiver is MQTT-only; these remain zeroed unless explicitly updated.
    extern std::atomic<uint32_t> rx_callback_count;
    extern std::atomic<uint32_t> rx_queue_drop_count;
    extern std::atomic<uint32_t> rx_queue_high_watermark;

}  // namespace RuntimeState

enum class ErrorSeverity { WARNING, ERROR, FATAL };

void handle_error(ErrorSeverity severity, const char* component, const char* message);
