#include "transmitter_event_log_cache.h"
#include "sse_notifier.h"

#include "../logging.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <algorithm>
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
    TransmitterEventLogCache::SnapshotStatus snapshot_status;
    std::vector<uint8_t> snapshot_batch_seen;

    void ensure_mutex() {
        if (event_logs_mutex == nullptr) {
            event_logs_mutex = xSemaphoreCreateMutex();
        }
    }

    int find_event_index_by_type(const char* type) {
        if (type == nullptr || type[0] == '\0') {
            return -1;
        }

        for (size_t i = 0; i < event_logs.size(); ++i) {
            if (strncmp(event_logs[i].type, type, sizeof(event_logs[i].type)) == 0) {
                return static_cast<int>(i);
            }
        }

        return -1;
    }
}

namespace TransmitterEventLogCache {

void begin_snapshot_session(bool clear_existing_cache) {
    ensure_mutex();

    ScopedMutex guard(event_logs_mutex);
    if (!guard.locked()) {
        LOG_WARN("EVENT_LOG_CACHE", "Failed to lock event logs mutex for snapshot begin");
        return;
    }

    if (clear_existing_cache) {
        event_logs.clear();
        event_logs_known = true;
        event_logs_last_update_ms = millis();
    }

    snapshot_status = SnapshotStatus{};
    snapshot_status.session_active = true;
    snapshot_status.session_started_ms = millis();
    snapshot_status.last_update_ms = snapshot_status.session_started_ms;
    snapshot_batch_seen.clear();

    LOG_INFO("EVENT_LOG_CACHE", "Started event-log snapshot session");
}

void end_snapshot_session(bool clear_cached_logs) {
    ensure_mutex();

    ScopedMutex guard(event_logs_mutex);
    if (!guard.locked()) {
        LOG_WARN("EVENT_LOG_CACHE", "Failed to lock event logs mutex for snapshot end");
        return;
    }

    snapshot_status.session_active = false;
    snapshot_status.last_update_ms = millis();

    if (clear_cached_logs) {
        event_logs.clear();
        event_logs_known = true;
        event_logs_last_update_ms = snapshot_status.last_update_ms;
        SSENotifier::notifyDataUpdated();
    }

    LOG_INFO("EVENT_LOG_CACHE", "Ended event-log snapshot session (cache cleared=%s)",
             clear_cached_logs ? "yes" : "no");
}

SnapshotStatus get_snapshot_status() {
    ensure_mutex();

    ScopedMutex guard(event_logs_mutex);
    if (!guard.locked()) {
        return SnapshotStatus{};
    }

    return snapshot_status;
}

void store_event_logs(const JsonObject& logs) {
    ensure_mutex();

    ScopedMutex guard(event_logs_mutex);
    if (!guard.locked()) {
        LOG_WARN("EVENT_LOG_CACHE", "Failed to lock event logs mutex");
        return;
    }

    if (!logs.containsKey("events") || !logs["events"].is<JsonArray>()) {
        event_logs_known = false;
        LOG_WARN("EVENT_LOG_CACHE", "Event logs missing 'events' array");
        return;
    }

    JsonArray events = logs["events"].as<JsonArray>();
    const size_t max_events = 200;
    uint32_t new_count = 0;

    const uint64_t incoming_snapshot_id = logs["snapshot_id"] | static_cast<uint64_t>(0);
    const int incoming_batch_index = logs["batch_index"] | -1;
    const uint16_t incoming_batch_count = logs["batch_count"] | static_cast<uint16_t>(0);
    const bool incoming_snapshot_complete = logs["snapshot_complete"] | false;

    if (incoming_batch_count > 0 && incoming_batch_index >= 0) {
        if (!snapshot_status.session_active) {
            snapshot_status.session_active = true;
            snapshot_status.session_started_ms = millis();
        }

        const bool new_snapshot = !snapshot_status.metadata_seen ||
                                  snapshot_status.snapshot_id != incoming_snapshot_id ||
                                  snapshot_status.batch_count != incoming_batch_count;

        if (new_snapshot) {
            snapshot_status.metadata_seen = true;
            snapshot_status.complete = false;
            snapshot_status.snapshot_id = incoming_snapshot_id;
            snapshot_status.batch_count = incoming_batch_count;
            snapshot_status.last_batch_index = -1;
            snapshot_status.received_batches = 0;

            snapshot_batch_seen.assign(incoming_batch_count, 0);
        }

        if (incoming_batch_index < (int)snapshot_batch_seen.size()) {
            if (!snapshot_batch_seen[(size_t)incoming_batch_index]) {
                snapshot_batch_seen[(size_t)incoming_batch_index] = 1;
                snapshot_status.received_batches++;
            }
            snapshot_status.last_batch_index = static_cast<int16_t>(incoming_batch_index);
        }

        if (incoming_snapshot_complete ||
            (snapshot_status.batch_count > 0 && snapshot_status.received_batches >= snapshot_status.batch_count)) {
            snapshot_status.complete = true;
        }

        snapshot_status.last_update_ms = millis();
    }

    // Only the most recently received delta batch should be marked as new.
    for (auto& entry : event_logs) {
        entry.is_new = false;
    }

    for (JsonObject evt : events) {
        EventLogEntry entry = {};
        entry.timestamp_ms = evt["timestamp_ms"] | (evt["timestamp"] | static_cast<uint64_t>(0));
        entry.event_unix_ms = evt["event_unix_ms"] | static_cast<uint64_t>(0);
        entry.event_utc_offset_min = evt["event_utc_offset_min"] | static_cast<int16_t>(0);
        entry.level = evt["level"] | 0;
        entry.data = evt["data"] | 0;
        entry.count = evt["count"] | 1;
        entry.is_new = evt["is_new"] | true;

        const char* type = evt["type"] | evt["event"] | "";
        strncpy(entry.type, type, sizeof(entry.type) - 1);
        entry.type[sizeof(entry.type) - 1] = '\0';

        if (entry.type[0] == '\0') {
            continue;
        }

        const char* msg = evt["message"] | "";
        strncpy(entry.message, msg, sizeof(entry.message) - 1);
        entry.message[sizeof(entry.message) - 1] = '\0';

        int existing_idx = find_event_index_by_type(entry.type);
        if (existing_idx >= 0) {
            event_logs[existing_idx] = entry;
            new_count++;
            continue;
        }

        if (event_logs.size() >= max_events) {
            // Replace the oldest entry to keep cache bounded.
            size_t oldest_index = 0;
            uint64_t oldest_ts = event_logs[0].timestamp_ms;
            for (size_t i = 1; i < event_logs.size(); ++i) {
                if (event_logs[i].timestamp_ms < oldest_ts) {
                    oldest_ts = event_logs[i].timestamp_ms;
                    oldest_index = i;
                }
            }
            event_logs[oldest_index] = entry;
        } else {
            event_logs.push_back(entry);
        }

        new_count++;
    }

    // Keep newest events first for API/UI consumers.
    std::sort(event_logs.begin(), event_logs.end(),
              [](const EventLogEntry& a, const EventLogEntry& b) {
                  return a.timestamp_ms > b.timestamp_ms;
              });

    event_logs_known = true;
    event_logs_last_update_ms = millis();
    LOG_INFO("EVENT_LOG_CACHE", "Merged event logs: total=%u, new=%u",
             static_cast<unsigned>(event_logs.size()),
             static_cast<unsigned>(new_count));
    SSENotifier::notifyDataUpdated();
}

void clear_event_logs() {
    ensure_mutex();

    ScopedMutex guard(event_logs_mutex);
    if (!guard.locked()) {
        LOG_WARN("EVENT_LOG_CACHE", "Failed to lock event logs mutex for clear");
        return;
    }

    event_logs.clear();
    event_logs_known = true;
    event_logs_last_update_ms = millis();
    LOG_INFO("EVENT_LOG_CACHE", "Cleared cached event logs");
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
