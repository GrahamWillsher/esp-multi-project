#include "mqtt_logger.h"
#include <stdarg.h>
#include <time.h>

// Helper function to format uptime as "Xd XXh XXm XXs"
static void format_uptime(char* buffer, size_t size, unsigned long uptime_ms) {
    unsigned long uptime_s = uptime_ms / 1000;
    int days = uptime_s / 86400;
    int hours = (uptime_s % 86400) / 3600;
    int minutes = (uptime_s % 3600) / 60;
    int seconds = uptime_s % 60;
    snprintf(buffer, size, "%dd %02dh %02dm %02ds", days, hours, minutes, seconds);
}

// Helper function to get date and time strings
static bool get_datetime_strings(char* date_str, char* time_str, size_t size) {
    time_t now;
    struct tm timeinfo;
    
    time(&now);
    if (!getLocalTime(&timeinfo, 10)) {
        return false;  // Time not available
    }
    
    // Format date as dd-mm-yyyy
    if (date_str) {
        strftime(date_str, size, "%d-%m-%Y", &timeinfo);
    }
    
    // Format time as HH:mm:ss
    if (time_str) {
        strftime(time_str, size, "%H:%M:%S", &timeinfo);
    }
    
    return true;
}

MqttLogger& MqttLogger::instance() {
    static MqttLogger instance;
    return instance;
}

bool MqttLogger::is_mqtt_available() {
    return initialized_ && mqtt_client_ && mqtt_available_cached_;
}

void MqttLogger::set_mqtt_available(bool available) {
    mqtt_available_cached_ = available;
}

void MqttLogger::init(PubSubClient* mqtt_client, const char* device_id) {
    mqtt_client_ = mqtt_client;
    snprintf(device_id_,    sizeof(device_id_),    "%s", device_id);
    snprintf(topic_prefix_, sizeof(topic_prefix_), "%s/debug/", device_id_);
    if (!buffer_mutex_) {
        buffer_mutex_ = xSemaphoreCreateMutex();
    }
    initialized_ = true;
    mqtt_available_cached_ = false;
    
    Serial.printf("[MQTT_LOG] Initialized for device: %s\n", device_id);
    
    // Publish initial status
    publish_status();
}

void MqttLogger::log(MqttLogLevel level, const char* tag, const char* format, ...) {
    // Filter by level
    if (level > min_level_) return;
    
    // Format message
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    // Get uptime and date/time for all logging
    char uptime_str[32];
    format_uptime(uptime_str, sizeof(uptime_str), millis());
    
    char date_str[16] = {0};
    char time_str[16] = {0};
    bool has_datetime = get_datetime_strings(date_str, time_str, 16);
    
    // Build prefix with date/time first (if available), then uptime
    char prefix[64];
    if (has_datetime) {
        snprintf(prefix, sizeof(prefix), "[%s %s] [%s]", date_str, time_str, uptime_str);
    } else {
        snprintf(prefix, sizeof(prefix), "[%s]", uptime_str);
    }
    
    // Always output critical messages to Serial
    if (level <= MQTT_LOG_CRIT) {
        Serial.printf("%s [%s][%s] %s\n", prefix, level_to_string(level), tag, buffer);
    }
    
    // Always enqueue for MQTT publication.
    // IMPORTANT: PubSubClient is not thread-safe; direct publish from arbitrary
    // tasks/event callbacks can race with mqtt_task::client.loop/connect/disconnect
    // and cause connection instability after link flaps.
    if (buffer_mutex_ && xSemaphoreTake(buffer_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
        if (buffer_count_ < BUFFER_SIZE) {
            buffer_[buffer_head_].level = level;
            snprintf(buffer_[buffer_head_].tag,     sizeof(buffer_[buffer_head_].tag),     "%s", tag);
            snprintf(buffer_[buffer_head_].message, sizeof(buffer_[buffer_head_].message), "%s", buffer);
            buffer_[buffer_head_].timestamp = millis();
            buffer_head_ = (buffer_head_ + 1) % BUFFER_SIZE;
            buffer_count_++;
        }
        xSemaphoreGive(buffer_mutex_);
    }

    // Fallback to Serial while MQTT is unavailable.
    if (!is_mqtt_available()) {
        Serial.printf("%s [%s][%s] %s\n", prefix, level_to_string(level), tag, buffer);
    }
}

void MqttLogger::publish_message(MqttLogLevel level, const char* tag, const char* message) {
    // Build topic
    char topic[96];
    snprintf(topic, sizeof(topic), "%s%s", topic_prefix_, level_to_string(level));

    // Format uptime
    char uptime_str[32];
    format_uptime(uptime_str, sizeof(uptime_str), millis());

    // Try to get date and time
    char date_str[16] = {0};
    char time_str[16] = {0};
    bool has_datetime = get_datetime_strings(date_str, time_str, 16);

    // Build timestamped message
    char formatted_msg[320];
    if (has_datetime) {
        snprintf(formatted_msg, sizeof(formatted_msg), "[%s %s] [%s] %s",
                 date_str, time_str, uptime_str, message);
    } else {
        snprintf(formatted_msg, sizeof(formatted_msg), "[%s] %s", uptime_str, message);
    }

    // Build JSON payload with metadata
    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"tag\":\"%s\",\"msg\":\"%s\",\"heap\":%lu}",
             tag, formatted_msg, (unsigned long)ESP.getFreeHeap());

    // Publish with appropriate QoS and retain flag
    bool published = mqtt_client_->publish(topic, payload, get_retained(level));

    if (!published) {
        // Stop immediate retries until next probe interval
        mqtt_available_cached_ = false;
        Serial.printf("[MQTT_LOG] Failed to publish: %s\n", topic);
    }
}

uint8_t MqttLogger::get_qos(MqttLogLevel level) const {
    // Note: PubSubClient doesn't support QoS in publish, but we keep this for documentation
    if (level <= MQTT_LOG_ALERT) return 2;  // Would be guaranteed delivery
    if (level <= MQTT_LOG_ERROR) return 1;  // Would be at least once
    return 0;  // Best effort
}

bool MqttLogger::get_retained(MqttLogLevel level) const {
    // Retain only critical messages for visibility
    return (level <= MQTT_LOG_ALERT);
}

void MqttLogger::flush_buffer() {
    if (!is_mqtt_available() || !buffer_mutex_) {
        return;
    }

    if (xSemaphoreTake(buffer_mutex_, pdMS_TO_TICKS(10)) != pdTRUE) {
        return;
    }

    size_t flushed = 0;

    for (size_t i = 0; i < buffer_count_ && flushed < 5; i++) {
        size_t idx = (buffer_head_ - buffer_count_ + i) % BUFFER_SIZE;
        // Capture fields before releasing mutex for the publish call
        MqttLogLevel  lvl = buffer_[idx].level;
        char          tag_copy[32];
        char          msg_copy[256];
        snprintf(tag_copy, sizeof(tag_copy), "%s", buffer_[idx].tag);
        snprintf(msg_copy, sizeof(msg_copy), "%s", buffer_[idx].message);
        xSemaphoreGive(buffer_mutex_);

        publish_message(lvl, tag_copy, msg_copy);

        if (!is_mqtt_available()) {
            // publish_message() cleared mqtt_available_cached_ on failure; stop here.
            buffer_count_ = (buffer_count_ > flushed) ? (buffer_count_ - flushed) : 0;
            return;
        }
        flushed++;

        if (xSemaphoreTake(buffer_mutex_, pdMS_TO_TICKS(10)) != pdTRUE) {
            // Lost mutex re-acquire; leave remaining count intact for next flush.
            return;
        }
    }

    buffer_count_ = (buffer_count_ > flushed) ? (buffer_count_ - flushed) : 0;
    xSemaphoreGive(buffer_mutex_);
}

void MqttLogger::set_level(MqttLogLevel min_level) {
    if (min_level != min_level_) {
        Serial.printf("[MQTT_LOG] Level changed: %s -> %s\n", 
                     level_to_string(min_level_), 
                     level_to_string(min_level));
        min_level_ = min_level;
        publish_status();
    }
}

void MqttLogger::publish_status() {
    if (!is_mqtt_available()) return;

    char topic[96];
    snprintf(topic, sizeof(topic), "%slevel", topic_prefix_);
    mqtt_client_->publish(topic, level_to_string(min_level_), true);

    // Format uptime
    char uptime_str[32];
    format_uptime(uptime_str, sizeof(uptime_str), millis());

    // Try to get date and time
    char date_str[16] = {0};
    char time_str[16] = {0};
    bool has_datetime = get_datetime_strings(date_str, time_str, 16);

    // Build timestamped status string
    char status_msg[64];
    if (has_datetime) {
        snprintf(status_msg, sizeof(status_msg), "[%s %s] [%s]", date_str, time_str, uptime_str);
    } else {
        snprintf(status_msg, sizeof(status_msg), "[%s]", uptime_str);
    }

    // Publish detailed status
    char status_topic[96];
    snprintf(status_topic, sizeof(status_topic), "%sstatus", topic_prefix_);
    char status[256];
    snprintf(status, sizeof(status),
             "{\"level\":\"%s\",\"device\":\"%s\",\"status\":\"%s\"}",
             level_to_string(min_level_), device_id_, status_msg);
    mqtt_client_->publish(status_topic, status, true);
}

const char* MqttLogger::level_to_string(MqttLogLevel level) const {
    switch (level) {
        case MQTT_LOG_EMERG:   return "emerg";
        case MQTT_LOG_ALERT:   return "alert";
        case MQTT_LOG_CRIT:    return "crit";
        case MQTT_LOG_ERROR:   return "error";
        case MQTT_LOG_WARNING: return "warning";
        case MQTT_LOG_NOTICE:  return "notice";
        case MQTT_LOG_INFO:    return "info";
        case MQTT_LOG_DEBUG:   return "debug";
        default:               return "unknown";
    }
}

MqttLogLevel MqttLogger::string_to_level(const char* level_str) const {
    if (strcasecmp(level_str, "emerg") == 0)   return MQTT_LOG_EMERG;
    if (strcasecmp(level_str, "alert") == 0)   return MQTT_LOG_ALERT;
    if (strcasecmp(level_str, "crit") == 0)    return MQTT_LOG_CRIT;
    if (strcasecmp(level_str, "error") == 0)   return MQTT_LOG_ERROR;
    if (strcasecmp(level_str, "warning") == 0) return MQTT_LOG_WARNING;
    if (strcasecmp(level_str, "notice") == 0)  return MQTT_LOG_NOTICE;
    if (strcasecmp(level_str, "info") == 0)    return MQTT_LOG_INFO;
    if (strcasecmp(level_str, "debug") == 0)   return MQTT_LOG_DEBUG;
    return min_level_;  // Keep current if invalid
}
