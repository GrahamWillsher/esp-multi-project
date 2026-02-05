#pragma once

#include <Arduino.h>
#include <mqtt_logger.h>

// ═══════════════════════════════════════════════════════════════════════
// Debug Logging System
// ═══════════════════════════════════════════════════════════════════════

// Logging levels (higher number = more verbose)
enum LogLevel { 
    LOG_NONE = 0,   // Disable all logging
    LOG_ERROR = 1,  // Critical errors only
    LOG_WARN = 2,   // Warnings and errors
    LOG_INFO = 3,   // Important information
    LOG_DEBUG = 4,  // Detailed debug information
    LOG_TRACE = 5   // Very verbose trace information
};

// Global log level (can be changed at runtime or compile-time)
// Set via platformio.ini build flag: -D COMPILE_LOG_LEVEL=LOG_INFO
#ifndef COMPILE_LOG_LEVEL
    #define COMPILE_LOG_LEVEL LOG_INFO
#endif

extern LogLevel current_log_level;

// Logging macros - compile out based on COMPILE_LOG_LEVEL
#if COMPILE_LOG_LEVEL >= LOG_ERROR
    #define LOG_ERROR(fmt, ...) if (current_log_level >= LOG_ERROR) { \
        Serial.printf("[ERROR] " fmt "\n", ##__VA_ARGS__); \
        MQTT_LOG_ERROR("SYS", fmt, ##__VA_ARGS__); \
    }
#else
    #define LOG_ERROR(fmt, ...) ((void)0)
#endif

#if COMPILE_LOG_LEVEL >= LOG_WARN
    #define LOG_WARN(fmt, ...) if (current_log_level >= LOG_WARN) { \
        Serial.printf("[WARN] " fmt "\n", ##__VA_ARGS__); \
        MQTT_LOG_WARNING("SYS", fmt, ##__VA_ARGS__); \
    }
#else
    #define LOG_WARN(fmt, ...) ((void)0)
#endif

#if COMPILE_LOG_LEVEL >= LOG_INFO
    #define LOG_INFO(fmt, ...) if (current_log_level >= LOG_INFO) { \
        Serial.printf("[INFO] " fmt "\n", ##__VA_ARGS__); \
        MQTT_LOG_INFO("SYS", fmt, ##__VA_ARGS__); \
    }
#else
    #define LOG_INFO(fmt, ...) ((void)0)
#endif

#if COMPILE_LOG_LEVEL >= LOG_DEBUG
    #define LOG_DEBUG(fmt, ...) if (current_log_level >= LOG_DEBUG) { \
        Serial.printf("[DEBUG] " fmt "\n", ##__VA_ARGS__); \
        MQTT_LOG_DEBUG("SYS", fmt, ##__VA_ARGS__); \
    }
#else
    #define LOG_DEBUG(fmt, ...) ((void)0)
#endif

#if COMPILE_LOG_LEVEL >= LOG_TRACE
    #define LOG_TRACE(fmt, ...) if (current_log_level >= LOG_TRACE) { \
        Serial.printf("[TRACE] " fmt "\n", ##__VA_ARGS__); \
        MQTT_LOG_DEBUG("SYS", fmt, ##__VA_ARGS__); \
    }
#else
    #define LOG_TRACE(fmt, ...) ((void)0)
#endif
