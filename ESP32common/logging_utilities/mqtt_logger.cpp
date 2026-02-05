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

void MqttLogger::init(PubSubClient* mqtt_client, const char* device_id) {
    mqtt_client_ = mqtt_client;
    device_id_ = device_id;
    topic_prefix_ = String(device_id_) + "/debug/";
    initialized_ = true;
    
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
    
    // Publish to MQTT
    if (initialized_ && mqtt_client_ && mqtt_client_->connected()) {
        publish_message(level, tag, buffer);
        
        // Flush any buffered messages
        if (buffer_count_ > 0) {
            flush_buffer();
        }
    } else {
        // Buffer message if MQTT not available
        if (buffer_count_ < BUFFER_SIZE) {
            buffer_[buffer_head_].level = level;
            buffer_[buffer_head_].tag = tag;
            buffer_[buffer_head_].message = buffer;
            buffer_[buffer_head_].timestamp = millis();
            buffer_head_ = (buffer_head_ + 1) % BUFFER_SIZE;
            buffer_count_++;
        }
        
        // Fallback to Serial
        Serial.printf("%s [%s][%s] %s\n", prefix, level_to_string(level), tag, buffer);
    }
}

void MqttLogger::publish_message(MqttLogLevel level, const char* tag, const char* message) {
    // Build topic
    String topic = topic_prefix_ + level_to_string(level);
    
    // Format uptime
    char uptime_str[32];
    format_uptime(uptime_str, sizeof(uptime_str), millis());
    
    // Try to get date and time
    char date_str[16] = {0};
    char time_str[16] = {0};
    bool has_datetime = get_datetime_strings(date_str, time_str, 16);
    
    // Build message with date/time first (if available), then uptime in brackets
    String formatted_msg = "";
    if (has_datetime) {
        formatted_msg = String("[") + date_str + " " + time_str + "] ";
    }
    formatted_msg += String("[") + uptime_str + "] " + message;
    
    // Build JSON payload with metadata
    String payload = String("{\"tag\":\"") + tag + 
                    "\",\"msg\":\"" + formatted_msg + 
                    "\",\"heap\":" + String(ESP.getFreeHeap()) + "}";
    
    // Publish with appropriate QoS and retain flag
    bool published = mqtt_client_->publish(topic.c_str(), payload.c_str(), get_retained(level));
    
    if (!published) {
        Serial.printf("[MQTT_LOG] Failed to publish: %s\n", topic.c_str());
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
    size_t flushed = 0;
    
    for (size_t i = 0; i < buffer_count_ && flushed < 5; i++) {
        size_t idx = (buffer_head_ - buffer_count_ + i) % BUFFER_SIZE;
        publish_message(buffer_[idx].level, 
                       buffer_[idx].tag.c_str(), 
                       buffer_[idx].message.c_str());
        flushed++;
    }
    
    buffer_count_ = (buffer_count_ > flushed) ? (buffer_count_ - flushed) : 0;
    
    if (buffer_count_ == 0) {
        Serial.printf("[MQTT_LOG] Buffer flushed (%u messages)\n", flushed);
    }
}

void MqttLogger::set_level(MqttLogLevel min_level) {
    if (min_level != min_level_) {
        Serial.printf("[MQTT_LOG] Level changed: %s → %s\n", 
                     level_to_string(min_level_), 
                     level_to_string(min_level));
        min_level_ = min_level;
        publish_status();
    }
}

void MqttLogger::publish_status() {
    if (!mqtt_client_ || !mqtt_client_->connected()) return;
    
    String topic = topic_prefix_ + "level";
    mqtt_client_->publish(topic.c_str(), level_to_string(min_level_), true);
    
    // Format uptime
    char uptime_str[32];
    format_uptime(uptime_str, sizeof(uptime_str), millis());
    
    // Try to get date and time
    char date_str[16] = {0};
    char time_str[16] = {0};
    bool has_datetime = get_datetime_strings(date_str, time_str, 16);
    
    // Build status message with date/time first (if available), then uptime in brackets
    String status_msg = "";
    if (has_datetime) {
        status_msg = String("[") + date_str + " " + time_str + "] ";
    }
    status_msg += String("[") + uptime_str + "]";
    
    // Publish detailed status
    String status_topic = topic_prefix_ + "status";
    String status = String("{\"level\":\"") + level_to_string(min_level_) + 
                   "\",\"device\":\"" + device_id_ + 
                   "\",\"status\":\"" + status_msg + "\"}";
    mqtt_client_->publish(status_topic.c_str(), status.c_str(), true);
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
