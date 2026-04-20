#include "sse_notifier.h"
#include <Arduino.h>

#include "../logging.h"

EventGroupHandle_t SSENotifier::event_group = nullptr;

namespace {
constexpr uint32_t kMonitorNotifyMinIntervalMs = 250;
volatile uint32_t g_monitor_client_count = 0;
volatile uint32_t g_last_monitor_notify_ms = 0;
portMUX_TYPE g_sse_notifier_mux = portMUX_INITIALIZER_UNLOCKED;
}

void SSENotifier::init() {
    if (event_group == nullptr) {
        event_group = xEventGroupCreate();
        if (event_group != nullptr) {
            LOG_INFO("SSE", "Event group created");
        } else {
            LOG_ERROR("SSE", "Failed to create event group");
        }
    }
}

void SSENotifier::monitorClientConnected() {
    portENTER_CRITICAL(&g_sse_notifier_mux);
    g_monitor_client_count++;
    g_last_monitor_notify_ms = 0;
    portEXIT_CRITICAL(&g_sse_notifier_mux);
}

void SSENotifier::monitorClientDisconnected() {
    portENTER_CRITICAL(&g_sse_notifier_mux);
    if (g_monitor_client_count > 0) {
        g_monitor_client_count--;
    }
    if (g_monitor_client_count == 0) {
        g_last_monitor_notify_ms = 0;
    }
    portEXIT_CRITICAL(&g_sse_notifier_mux);
}

void SSENotifier::notifyDataUpdated() {
    if (event_group == nullptr) {
        return;
    }

    bool should_notify = false;
    const uint32_t now = millis();

    portENTER_CRITICAL(&g_sse_notifier_mux);
    if (g_monitor_client_count > 0) {
        const uint32_t elapsed = now - g_last_monitor_notify_ms;
        if (g_last_monitor_notify_ms == 0 || elapsed >= kMonitorNotifyMinIntervalMs) {
            g_last_monitor_notify_ms = now;
            should_notify = true;
        }
    }
    portEXIT_CRITICAL(&g_sse_notifier_mux);

    if (should_notify) {
        xEventGroupSetBits(event_group, DATA_UPDATED_BIT);
    }
}

void SSENotifier::notifyCellDataUpdated() {
    if (event_group != nullptr) {
        xEventGroupSetBits(event_group, CELL_DATA_UPDATED_BIT);
    }
}

bool SSENotifier::waitForUpdate(TickType_t timeout_ms) {
    if (event_group == nullptr) return false;
    
    EventBits_t bits = xEventGroupWaitBits(
        event_group,
        DATA_UPDATED_BIT,
        pdTRUE,  // Clear on exit
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms)
    );
    
    return (bits & DATA_UPDATED_BIT) != 0;
}

bool SSENotifier::waitForCellDataUpdate(TickType_t timeout_ms) {
    if (event_group == nullptr) return false;

    EventBits_t bits = xEventGroupWaitBits(
        event_group,
        CELL_DATA_UPDATED_BIT,
        pdTRUE,
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms)
    );

    return (bits & CELL_DATA_UPDATED_BIT) != 0;
}
