#pragma once

#include <stdint.h>
#include <logging_config.h>
#include <mqtt_logger.h>

enum class LogSink : uint8_t {
    None  = 0,
    Local = 1u << 0,
    Mqtt  = 1u << 1,
    Both  = Local | Mqtt
};

constexpr LogSink operator|(LogSink a, LogSink b) noexcept {
    return static_cast<LogSink>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr LogSink operator&(LogSink a, LogSink b) noexcept {
    return static_cast<LogSink>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

constexpr bool any(LogSink s) noexcept {
    return static_cast<uint8_t>(s) != 0u;
}

enum class RoutedLevel : uint8_t {
    Emerg  = 0,
    Alert  = 1,
    Crit   = 2,
    Error  = 3,
    Warn   = 4,
    Notice = 5,
    Info   = 6,
    Debug  = 7,
    Trace  = 8
};

static_assert(static_cast<uint8_t>(RoutedLevel::Emerg) == MQTT_LOG_EMERG,
              "RoutedLevel::Emerg != MQTT_LOG_EMERG");
static_assert(static_cast<uint8_t>(RoutedLevel::Debug) == MQTT_LOG_DEBUG,
              "RoutedLevel::Debug != MQTT_LOG_DEBUG");

void log_routed(LogSink sinks,
                RoutedLevel level,
                const char* tag,
                const char* fmt,
                ...) __attribute__((format(printf, 4, 5)));
