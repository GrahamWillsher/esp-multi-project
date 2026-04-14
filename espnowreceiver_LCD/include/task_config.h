#pragma once

#include <cstdint>

// ═══════════════════════════════════════════════════════════════════════
// FreeRTOS Task Configuration
//
// Centralised stack sizes, priorities, and core affinity for all tasks
// that will exist once the full receiver stack is ported (Phase B+).
//
// Stack sizes are sized for PSRAM allocation (see Phase B notes in port
// analysis doc). Priorities use FreeRTOS scale: 0 (lowest) – 25 (highest).
// All worker tasks are pinned to WORKER_CORE (Core 1) so Core 0 stays
// free for the WiFi/BLE stack.
// ═══════════════════════════════════════════════════════════════════════

namespace TaskConfig {

    // ── Stack sizes (bytes) ──────────────────────────────────────────

    // LVGL render + lv_timer_handler, pinned to Core 1.
    // Must own the LVGL mutex; all other tasks post via DisplayUpdateQueue.
    constexpr uint32_t DISPLAY_RENDERER_STACK = 4096;

    // Always-on connection-status indicator animation.
    constexpr uint32_t LED_RENDERER_STACK = 3072;

    // ESP-NOW receive callbacks → message queue → worker task.
    constexpr uint32_t ESPNOW_WORKER_STACK = 4096;

    // MQTT connection, pub/sub, JSON serialisation.
    // Larger stack accommodates ArduinoJson scratch buffer.
    constexpr uint32_t MQTT_CLIENT_STACK = 10240;

    // Periodic ESP-NOW discovery announcements.
    constexpr uint32_t ANNOUNCEMENT_TASK_STACK = 4096;

    // Background heap sampling (internal + PSRAM), lowest priority.
    constexpr uint32_t MEMORY_SAMPLER_STACK = 2560;

    // ── Task priorities ──────────────────────────────────────────────

    constexpr uint8_t DISPLAY_RENDERER_PRIORITY  = 1;
    constexpr uint8_t LED_RENDERER_PRIORITY       = 1;
    constexpr uint8_t ESPNOW_WORKER_PRIORITY      = 2;
    constexpr uint8_t MQTT_CLIENT_PRIORITY        = 0;
    constexpr uint8_t ANNOUNCEMENT_PRIORITY       = 1;
    constexpr uint8_t MEMORY_SAMPLER_PRIORITY     = 0;

    // ── Core affinity ────────────────────────────────────────────────

    // Core 0 is reserved for the WiFi/BLE stack.
    // All application worker tasks run on Core 1.
    constexpr uint8_t WORKER_CORE = 1;

}  // namespace TaskConfig
