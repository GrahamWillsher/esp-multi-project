#pragma once

#include <PubSubClient.h>
#include <Arduino.h>

enum MqttLogLevel {
    MQTT_LOG_EMERG   = 0,  // System unusable
    MQTT_LOG_ALERT   = 1,  // Action must be taken immediately
    MQTT_LOG_CRIT    = 2,  // Critical conditions
    MQTT_LOG_ERROR   = 3,  // Error conditions
    MQTT_LOG_WARNING = 4,  // Warning conditions
    MQTT_LOG_NOTICE  = 5,  // Normal but significant
    MQTT_LOG_INFO    = 6,  // Informational
    MQTT_LOG_DEBUG   = 7   // Debug messages
};

class MqttLogger {
public:
    static MqttLogger& instance();
    
    // Initialize with existing MQTT client
    void init(PubSubClient* mqtt_client, const char* device_id);
    
    // Set minimum log level (messages below this are ignored)
    void set_level(MqttLogLevel min_level);
    MqttLogLevel get_level() const { return min_level_; }
    
    // Main logging function
    void log(MqttLogLevel level, const char* tag, const char* format, ...);
    
    // Publish current configuration
    void publish_status();
    
    // Flush buffered messages (call after MQTT reconnects)
    void flush_buffer();
    
    // String conversions
    const char* level_to_string(MqttLogLevel level) const;
    MqttLogLevel string_to_level(const char* level_str) const;
    
private:
    MqttLogger() = default;
    
    PubSubClient* mqtt_client_ = nullptr;
    String device_id_;
    String topic_prefix_;
    MqttLogLevel min_level_ = MQTT_LOG_INFO;
    bool initialized_ = false;
    
    // Circular buffer for messages when MQTT unavailable
    static const size_t BUFFER_SIZE = 20;
    struct BufferedMessage {
        MqttLogLevel level;
        String tag;
        String message;
        unsigned long timestamp;
    };
    BufferedMessage buffer_[BUFFER_SIZE];
    size_t buffer_head_ = 0;
    size_t buffer_count_ = 0;
    
    void publish_message(MqttLogLevel level, const char* tag, const char* message);
    uint8_t get_qos(MqttLogLevel level) const;
    bool get_retained(MqttLogLevel level) const;
};

// Convenience macros with automatic tagging
#define MQTT_LOG_EMERG(tag, ...)   MqttLogger::instance().log(MQTT_LOG_EMERG, tag, __VA_ARGS__)
#define MQTT_LOG_ALERT(tag, ...)   MqttLogger::instance().log(MQTT_LOG_ALERT, tag, __VA_ARGS__)
#define MQTT_LOG_CRIT(tag, ...)    MqttLogger::instance().log(MQTT_LOG_CRIT, tag, __VA_ARGS__)
#define MQTT_LOG_ERROR(tag, ...)   MqttLogger::instance().log(MQTT_LOG_ERROR, tag, __VA_ARGS__)
#define MQTT_LOG_WARNING(tag, ...) MqttLogger::instance().log(MQTT_LOG_WARNING, tag, __VA_ARGS__)
#define MQTT_LOG_NOTICE(tag, ...)  MqttLogger::instance().log(MQTT_LOG_NOTICE, tag, __VA_ARGS__)
#define MQTT_LOG_INFO(tag, ...)    MqttLogger::instance().log(MQTT_LOG_INFO, tag, __VA_ARGS__)
#define MQTT_LOG_DEBUG(tag, ...)   MqttLogger::instance().log(MQTT_LOG_DEBUG, tag, __VA_ARGS__)

// Auto-tag from function name
#define LOG_E(...) MQTT_LOG_ERROR(__func__, __VA_ARGS__)
#define LOG_W(...) MQTT_LOG_WARNING(__func__, __VA_ARGS__)
#define LOG_I(...) MQTT_LOG_INFO(__func__, __VA_ARGS__)
#define LOG_D(...) MQTT_LOG_DEBUG(__func__, __VA_ARGS__)
