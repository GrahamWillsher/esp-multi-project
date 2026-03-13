#include "sse_notifier.h"
#include <Arduino.h>

EventGroupHandle_t SSENotifier::event_group = nullptr;

void SSENotifier::init() {
    if (event_group == nullptr) {
        event_group = xEventGroupCreate();
        if (event_group != nullptr) {
            Serial.println("[SSE] Event group created");
        } else {
            Serial.println("[SSE] ERROR: Failed to create event group");
        }
    }
}

void SSENotifier::notifyDataUpdated() {
    if (event_group != nullptr) {
        xEventGroupSetBits(event_group, DATA_UPDATED_BIT);
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

EventGroupHandle_t SSENotifier::getEventGroup() {
    return event_group;
}
