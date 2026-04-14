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
    extern TaskHandle_t display_renderer_task;
    extern TaskHandle_t espnow_worker_task;
    extern TaskHandle_t mqtt_client_task;

}  // namespace RTOS

namespace ESPNow {

    // Peer MAC address of the active transmitter (set on first heartbeat).
    extern uint8_t peer_mac[6];

    // Inbound message queue (populated by ESP-NOW recv callback,
    // consumed by espnow_worker_task). Initialised in Phase E.
    extern QueueHandle_t message_queue;

}  // namespace ESPNow
