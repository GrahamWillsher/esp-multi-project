#include "mqtt_ack_tracker.h"

#include "logging_config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <cstring>

MqttAckTracker::AckEntry MqttAckTracker::entries_[8]{};
SemaphoreHandle_t MqttAckTracker::mutex_ = nullptr;

void MqttAckTracker::ensureMutex() {
    if (mutex_ == nullptr) {
        mutex_ = xSemaphoreCreateMutex();
    }
}

void MqttAckTracker::handleAckMessage(const char* topic, const char* json_payload, size_t length) {
    if (!topic || !json_payload) {
        return;
    }

    ensureMutex();
    if (mutex_ == nullptr) {
        return;
    }

    DynamicJsonDocument doc(768);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_WARN("MQTT_ACK", "Failed to parse ACK on %s: %s", topic, error.c_str());
        return;
    }

    AckResult result;
    result.received = true;
    result.success = doc["success"] | false;
    strlcpy(result.request_id, doc["request_id"] | "", sizeof(result.request_id));
    strlcpy(result.topic, topic, sizeof(result.topic));
    strlcpy(result.code, doc["code"] | "", sizeof(result.code));
    strlcpy(result.message, doc["message"] | "", sizeof(result.message));
    const size_t payload_copy_len = std::min(length, sizeof(result.payload) - 1);
    memcpy(result.payload, json_payload, payload_copy_len);
    result.payload[payload_copy_len] = '\0';
    result.version = doc["new_version"] | doc["config_version"] | doc["applied_version"] | 0;

    if (result.request_id[0] == '\0') {
        LOG_WARN("MQTT_ACK", "Ignoring ACK without request_id on %s", topic);
        return;
    }

    storeAck(result);
    LOG_INFO("MQTT_ACK", "Stored ACK for %s success=%s code=%s",
             result.request_id,
             result.success ? "yes" : "no",
             result.code);
}

bool MqttAckTracker::waitForAck(const char* request_id, uint32_t timeout_ms, AckResult* out_result) {
    if (!request_id || request_id[0] == '\0') {
        return false;
    }

    ensureMutex();
    if (mutex_ == nullptr) {
        return false;
    }

    const uint32_t start_ms = millis();
    AckResult found;
    while ((millis() - start_ms) < timeout_ms) {
        if (findAck(request_id, &found)) {
            if (out_result) {
                *out_result = found;
            }
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    return false;
}

void MqttAckTracker::storeAck(const AckResult& result) {
    ensureMutex();
    if (mutex_ == nullptr) {
        return;
    }

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    int oldest_index = 0;
    uint32_t oldest_ts = entries_[0].ts_ms;

    for (int i = 0; i < 8; ++i) {
        if (entries_[i].in_use && strcmp(entries_[i].result.request_id, result.request_id) == 0) {
            entries_[i].result = result;
            entries_[i].ts_ms = millis();
            xSemaphoreGive(mutex_);
            return;
        }
        if (!entries_[i].in_use) {
            entries_[i].in_use = true;
            entries_[i].result = result;
            entries_[i].ts_ms = millis();
            xSemaphoreGive(mutex_);
            return;
        }
        if (entries_[i].ts_ms < oldest_ts) {
            oldest_ts = entries_[i].ts_ms;
            oldest_index = i;
        }
    }

    entries_[oldest_index].in_use = true;
    entries_[oldest_index].result = result;
    entries_[oldest_index].ts_ms = millis();
    xSemaphoreGive(mutex_);
}

bool MqttAckTracker::findAck(const char* request_id, AckResult* out_result) {
    ensureMutex();
    if (mutex_ == nullptr) {
        return false;
    }

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) != pdTRUE) {
        return false;
    }

    for (int i = 0; i < 8; ++i) {
        if (!entries_[i].in_use) {
            continue;
        }
        if (strcmp(entries_[i].result.request_id, request_id) == 0) {
            if (out_result) {
                *out_result = entries_[i].result;
            }
            entries_[i].in_use = false;
            xSemaphoreGive(mutex_);
            return true;
        }
    }

    xSemaphoreGive(mutex_);
    return false;
}
