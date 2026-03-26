#include "log_routed.h"

#include <Arduino.h>
#include <stdarg.h>

static MqttLogLevel to_mqtt_level(RoutedLevel level) noexcept {
    if (level == RoutedLevel::Trace) {
        return MQTT_LOG_DEBUG;
    }
    return static_cast<MqttLogLevel>(static_cast<uint8_t>(level));
}

static LogLevel to_local_level(RoutedLevel level) noexcept {
    switch (level) {
        case RoutedLevel::Emerg:
        case RoutedLevel::Alert:
        case RoutedLevel::Crit:
        case RoutedLevel::Error:
            return LOG_ERROR;
        case RoutedLevel::Warn:
            return LOG_WARN;
        case RoutedLevel::Notice:
        case RoutedLevel::Info:
            return LOG_INFO;
        case RoutedLevel::Debug:
            return LOG_DEBUG;
        case RoutedLevel::Trace:
            return LOG_TRACE;
    }
    return LOG_ERROR;
}

static const char* level_prefix(RoutedLevel level) noexcept {
    switch (level) {
        case RoutedLevel::Emerg:
            return "[EMERG]";
        case RoutedLevel::Alert:
            return "[ALERT]";
        case RoutedLevel::Crit:
        case RoutedLevel::Error:
            return "[ERROR]";
        case RoutedLevel::Warn:
            return "[WARN ]";
        case RoutedLevel::Notice:
        case RoutedLevel::Info:
            return "[INFO ]";
        case RoutedLevel::Debug:
            return "[DEBUG]";
        case RoutedLevel::Trace:
            return "[TRACE]";
    }
    return "[?????]";
}

void log_routed(LogSink sinks,
                RoutedLevel level,
                const char* tag,
                const char* fmt,
                ...) {
    if (!any(sinks) || tag == nullptr || fmt == nullptr) {
        return;
    }

    char msg[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    if (any(sinks & LogSink::Local)) {
        if (current_log_level >= to_local_level(level)) {
            Serial.printf("%s[%s] %s\n", level_prefix(level), tag, msg);
        }
    }

    if (any(sinks & LogSink::Mqtt)) {
        MqttLogger::instance().log(to_mqtt_level(level), tag, "%s", msg);
    }
}
