#pragma once

#include <Arduino.h>
#include <stdarg.h>
#include <time.h>

// Optional MQTT logger integration (only if available)
#if defined(__has_include)
    #if __has_include("mqtt_logger.h") && !defined(LOG_USE_MQTT)
        #include <mqtt_logger.h>
        #define LOG_USE_MQTT 1
    #endif
#endif
#ifndef LOG_USE_MQTT
    #define LOG_USE_MQTT 0
#endif

#if LOG_USE_MQTT
    #include <mqtt_logger.h>
#endif

// ═══════════════════════════════════════════════════════════════════════
// Debug Logging System (Tagged API only)
// ═══════════════════════════════════════════════════════════════════════

enum LogLevel {
    LOG_NONE = 0,
    LOG_ERROR = 1,
    LOG_WARN = 2,
    LOG_INFO = 3,
    LOG_DEBUG = 4,
    LOG_TRACE = 5
};

#ifndef COMPILE_LOG_LEVEL
    #define COMPILE_LOG_LEVEL LOG_INFO
#endif

// Optional serial format that matches transmitter console output:
// [dd-mm-yyyy HH:MM:SS] [0d 00h 00m 00s] [info][TAG] message
// Falls back to: [0d 00h 00m 00s] [info][TAG] message when RTC time is unavailable.
// Disabled by default to preserve existing log format in other projects.
#ifndef LOG_SERIAL_FORMAT_TRANSMITTER
    #define LOG_SERIAL_FORMAT_TRANSMITTER 0
#endif

static inline void log_format_uptime(char* buffer, size_t size, unsigned long uptime_ms) {
    const unsigned long uptime_s = uptime_ms / 1000UL;
    const int days = static_cast<int>(uptime_s / 86400UL);
    const int hours = static_cast<int>((uptime_s % 86400UL) / 3600UL);
    const int minutes = static_cast<int>((uptime_s % 3600UL) / 60UL);
    const int seconds = static_cast<int>(uptime_s % 60UL);
    snprintf(buffer, size, "%dd %02dh %02dm %02ds", days, hours, minutes, seconds);
}

static inline bool log_get_datetime(char* date_str, size_t date_size, char* time_str, size_t time_size) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10)) {
        return false;
    }
    strftime(date_str, date_size, "%d-%m-%Y", &timeinfo);
    strftime(time_str, time_size, "%H:%M:%S", &timeinfo);
    return true;
}

static inline void log_serial_printf_impl(const char* level,
                                          const char* tag,
                                          const char* fmt,
                                          ...) {
    char message[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

#if LOG_SERIAL_FORMAT_TRANSMITTER
    char level_lc[16];
    size_t i = 0;
    for (; level[i] != '\0' && i < (sizeof(level_lc) - 1); ++i) {
        const char c = level[i];
        level_lc[i] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    level_lc[i] = '\0';

    char uptime_str[32];
    log_format_uptime(uptime_str, sizeof(uptime_str), millis());

    char date_str[16] = {0};
    char time_str[16] = {0};
    if (log_get_datetime(date_str, sizeof(date_str), time_str, sizeof(time_str))) {
        Serial.printf("[%s %s] [%s] [%s][%s] %s\n", date_str, time_str, uptime_str, level_lc, tag, message);
    } else {
        Serial.printf("[%s] [%s][%s] %s\n", uptime_str, level_lc, tag, message);
    }
#else
    Serial.printf("[%s][%s] %s\n", level, tag, message);
#endif
}

#define LOG_SERIAL_PRINTF(level, tag, fmt, ...) \
    log_serial_printf_impl(level, tag, fmt, ##__VA_ARGS__)

extern LogLevel current_log_level;

#if COMPILE_LOG_LEVEL >= LOG_ERROR
    #if LOG_USE_MQTT
        #define LOG_ERROR(tag, fmt, ...) if (current_log_level >= LOG_ERROR) { \
            LOG_SERIAL_PRINTF("ERROR", tag, fmt, ##__VA_ARGS__); \
            MQTT_LOG_ERROR(tag, fmt, ##__VA_ARGS__); \
        }
    #else
        #define LOG_ERROR(tag, fmt, ...) if (current_log_level >= LOG_ERROR) { \
            LOG_SERIAL_PRINTF("ERROR", tag, fmt, ##__VA_ARGS__); \
        }
    #endif
#else
    #define LOG_ERROR(tag, fmt, ...) ((void)0)
#endif

#if COMPILE_LOG_LEVEL >= LOG_WARN
    #if LOG_USE_MQTT
        #define LOG_WARN(tag, fmt, ...) if (current_log_level >= LOG_WARN) { \
            MQTT_LOG_WARNING(tag, fmt, ##__VA_ARGS__); \
        }
    #else
        #define LOG_WARN(tag, fmt, ...) if (current_log_level >= LOG_WARN) { \
            LOG_SERIAL_PRINTF("WARN", tag, fmt, ##__VA_ARGS__); \
        }
    #endif
#else
    #define LOG_WARN(tag, fmt, ...) ((void)0)
#endif

#if COMPILE_LOG_LEVEL >= LOG_INFO
    #if LOG_USE_MQTT
        #define LOG_INFO(tag, fmt, ...) if (current_log_level >= LOG_INFO) { \
            MQTT_LOG_INFO(tag, fmt, ##__VA_ARGS__); \
        }
    #else
        #define LOG_INFO(tag, fmt, ...) if (current_log_level >= LOG_INFO) { \
            LOG_SERIAL_PRINTF("INFO", tag, fmt, ##__VA_ARGS__); \
        }
    #endif
#else
    #define LOG_INFO(tag, fmt, ...) ((void)0)
#endif

#if COMPILE_LOG_LEVEL >= LOG_DEBUG
    #if LOG_USE_MQTT
        #define LOG_DEBUG(tag, fmt, ...) if (current_log_level >= LOG_DEBUG) { \
            MQTT_LOG_DEBUG(tag, fmt, ##__VA_ARGS__); \
        }
    #else
        #define LOG_DEBUG(tag, fmt, ...) if (current_log_level >= LOG_DEBUG) { \
            LOG_SERIAL_PRINTF("DEBUG", tag, fmt, ##__VA_ARGS__); \
        }
    #endif
#else
    #define LOG_DEBUG(tag, fmt, ...) ((void)0)
#endif

#if COMPILE_LOG_LEVEL >= LOG_TRACE
    #if LOG_USE_MQTT
        #define LOG_TRACE(tag, fmt, ...) if (current_log_level >= LOG_TRACE) { \
            MQTT_LOG_DEBUG(tag, fmt, ##__VA_ARGS__); \
        }
    #else
        #define LOG_TRACE(tag, fmt, ...) if (current_log_level >= LOG_TRACE) { \
            LOG_SERIAL_PRINTF("TRACE", tag, fmt, ##__VA_ARGS__); \
        }
    #endif
#else
    #define LOG_TRACE(tag, fmt, ...) ((void)0)
#endif
