#pragma once

#include <Arduino.h>
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
    extern TaskHandle_t espnow_worker_task;
    extern TaskHandle_t mqtt_client_task;

    // Queue for display snapshots (producer: app/model, consumer: LVGL task).
    extern QueueHandle_t display_update_queue;

}  // namespace RTOS

namespace ESPNow {

    // Transmitter MAC address of the active peer.
    // Kept as fixed-size array for _2 ABI compatibility.
    extern uint8_t transmitter_mac[6];

    // Peer MAC alias used by LCD runtime code paths.
    extern uint8_t* peer_mac;

    // Inbound message queue (populated by ESP-NOW recv callback,
    // consumed by espnow_worker_task). Initialised in Phase E.
    extern QueueHandle_t message_queue;

    // Alias of message_queue for webserver telemetry API compatibility (_2 used ESPNow::queue).
    extern QueueHandle_t& queue;

    // ESP-NOW receive statistics (updated in recv callback).
    extern volatile uint32_t rx_callback_count;
    extern volatile uint32_t rx_queue_drop_count;
    extern volatile uint32_t rx_queue_high_watermark;

    // LED state (display-rendered status indicator). LCD stub — no physical LED.
    extern volatile uint8_t  current_led_color;
    extern volatile uint8_t  current_led_effect;
    extern volatile bool     receiver_ota_led_override_active;

}  // namespace ESPNow

enum class ErrorSeverity { WARNING, ERROR, FATAL };

void handle_error(ErrorSeverity severity, const char* component, const char* message);
