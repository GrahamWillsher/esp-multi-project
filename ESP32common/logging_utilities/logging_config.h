#pragma once

#include <Arduino.h>

// Optional MQTT logger integration (only if available)
#if defined(__has_include)
    #if __has_include("mqtt_logger.h")
        #include <mqtt_logger.h>
        #define LOG_USE_MQTT 1
    #endif
#endif
#ifndef LOG_USE_MQTT
    #define LOG_USE_MQTT 0
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

extern LogLevel current_log_level;

#if COMPILE_LOG_LEVEL >= LOG_ERROR
    #if LOG_USE_MQTT
        #define LOG_ERROR(tag, fmt, ...) if (current_log_level >= LOG_ERROR) { \
            Serial.printf("[ERROR][%s] " fmt "\n", tag, ##__VA_ARGS__); \
            MQTT_LOG_ERROR(tag, fmt, ##__VA_ARGS__); \
        }
    #else
        #define LOG_ERROR(tag, fmt, ...) if (current_log_level >= LOG_ERROR) { \
            Serial.printf("[ERROR][%s] " fmt "\n", tag, ##__VA_ARGS__); \
        }
    #endif
#else
    #define LOG_ERROR(tag, fmt, ...) ((void)0)
#endif

#if COMPILE_LOG_LEVEL >= LOG_WARN
    #if LOG_USE_MQTT
        #define LOG_WARN(tag, fmt, ...) if (current_log_level >= LOG_WARN) { \
            Serial.printf("[WARN][%s] " fmt "\n", tag, ##__VA_ARGS__); \
            MQTT_LOG_WARNING(tag, fmt, ##__VA_ARGS__); \
        }
    #else
        #define LOG_WARN(tag, fmt, ...) if (current_log_level >= LOG_WARN) { \
            Serial.printf("[WARN][%s] " fmt "\n", tag, ##__VA_ARGS__); \
        }
    #endif
#else
    #define LOG_WARN(tag, fmt, ...) ((void)0)
#endif

#if COMPILE_LOG_LEVEL >= LOG_INFO
    #if LOG_USE_MQTT
        #define LOG_INFO(tag, fmt, ...) if (current_log_level >= LOG_INFO) { \
            Serial.printf("[INFO][%s] " fmt "\n", tag, ##__VA_ARGS__); \
            MQTT_LOG_INFO(tag, fmt, ##__VA_ARGS__); \
        }
    #else
        #define LOG_INFO(tag, fmt, ...) if (current_log_level >= LOG_INFO) { \
            Serial.printf("[INFO][%s] " fmt "\n", tag, ##__VA_ARGS__); \
        }
    #endif
#else
    #define LOG_INFO(tag, fmt, ...) ((void)0)
#endif

#if COMPILE_LOG_LEVEL >= LOG_DEBUG
    #if LOG_USE_MQTT
        #define LOG_DEBUG(tag, fmt, ...) if (current_log_level >= LOG_DEBUG) { \
            Serial.printf("[DEBUG][%s] " fmt "\n", tag, ##__VA_ARGS__); \
            MQTT_LOG_DEBUG(tag, fmt, ##__VA_ARGS__); \
        }
    #else
        #define LOG_DEBUG(tag, fmt, ...) if (current_log_level >= LOG_DEBUG) { \
            Serial.printf("[DEBUG][%s] " fmt "\n", tag, ##__VA_ARGS__); \
        }
    #endif
#else
    #define LOG_DEBUG(tag, fmt, ...) ((void)0)
#endif

#if COMPILE_LOG_LEVEL >= LOG_TRACE
    #if LOG_USE_MQTT
        #define LOG_TRACE(tag, fmt, ...) if (current_log_level >= LOG_TRACE) { \
            Serial.printf("[TRACE][%s] " fmt "\n", tag, ##__VA_ARGS__); \
            MQTT_LOG_DEBUG(tag, fmt, ##__VA_ARGS__); \
        }
    #else
        #define LOG_TRACE(tag, fmt, ...) if (current_log_level >= LOG_TRACE) { \
            Serial.printf("[TRACE][%s] " fmt "\n", tag, ##__VA_ARGS__); \
        }
    #endif
#else
    #define LOG_TRACE(tag, fmt, ...) ((void)0)
#endif
