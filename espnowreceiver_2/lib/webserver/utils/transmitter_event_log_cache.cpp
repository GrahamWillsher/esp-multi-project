#include "transmitter_event_log_cache.h"
#include "sse_notifier.h"

#include "../logging.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

namespace {
    struct ScopedMutex {
        explicit ScopedMutex(SemaphoreHandle_t mutex)
            : mutex_(mutex), locked_(false) {
            if (mutex_ != nullptr) {
                locked_ = (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE);
            }
        }

        ~ScopedMutex() {
            if (locked_) {
                xSemaphoreGive(mutex_);
            }
        }

        bool locked() const { return locked_; }

    private:
        SemaphoreHandle_t mutex_;
        bool locked_;
    };

    SemaphoreHandle_t event_logs_mutex = nullptr;
    std::vector<TransmitterEventLogCache::EventLogEntry> event_logs;
    bool event_logs_known = false;
    uint32_t event_logs_last_update_ms = 0;

    void ensure_mutex() {
        if (event_logs_mutex == nullptr) {
            event_logs_mutex = xSemaphoreCreateMutex();
        }
    }
}

namespace TransmitterEventLogCache {

void store_event_logs(const JsonObject& logs) {
    ensure_mutex();

    ScopedMutex guard(event_logs_mutex);
    if (!guard.locked()) {
        LOG_WARN("EVENT_LOG_CACHE", "Failed to lock event logs mutex");
        return;
    }

    event_logs.clear();

    if (!logs.containsKey("events") || !logs["events"].is<JsonArray>()) {
        event_logs_known = false;
        LOG_WARN("EVENT_LOG_CACHE", "Event logs missing 'events' array");
        return;
    }

    JsonArray events = logs["events"].as<JsonArray>();
    const size_t max_events = 200;

    for (JsonObject evt : events) {
        if (event_logs.size() >= max_events) {
            break;
        }

        EventLogEntry entry = {};
        entry.timestamp = evt["timestamp"] | 0;
        entry.level = evt["level"] | 0;
        entry.data = evt["data"] | 0;

        const char* msg = evt["message"] | "";
        strncpy(entry.message, msg, sizeof(entry.message) - 1);
        entry.message[sizeof(entry.message) - 1] = '\0';

        event_logs.push_back(entry);
    }

    event_logs_known = true;
    event_logs_last_update_ms = millis();
    LOG_INFO("EVENT_LOG_CACHE", "Stored %u event logs", static_cast<unsigned>(event_logs.size()));
    SSENotifier::notifyDataUpdated();
}

bool has_event_logs() {
    ensure_mutex();

    ScopedMutex guard(event_logs_mutex);
    if (!guard.locked()) {
        return false;
    }

    return event_logs_known && !event_logs.empty();
}

void get_event_logs_snapshot(std::vector<EventLogEntry>& out_logs, uint32_t* out_last_update_ms) {
    ensure_mutex();

    out_logs.clear();

    ScopedMutex guard(event_logs_mutex);
    if (!guard.locked()) {
        if (out_last_update_ms) {
            *out_last_update_ms = 0;
        }
        return;
    }

    out_logs = event_logs;
    if (out_last_update_ms) {
        *out_last_update_ms = event_logs_last_update_ms;
    }
}

uint32_t get_event_log_count() {
    ensure_mutex();

    ScopedMutex guard(event_logs_mutex);
    if (!guard.locked()) {
        return 0;
    }

    return static_cast<uint32_t>(event_logs.size());
}

uint32_t get_event_logs_last_update_ms() {
    ensure_mutex();

    ScopedMutex guard(event_logs_mutex);
    if (!guard.locked()) {
        return 0;
    }

    return event_logs_last_update_ms;
}

} // namespace TransmitterEventLogCache
