#pragma once

#include <Arduino.h>

// ═══════════════════════════════════════════════════════════════════════
// Compile-time log level
//
// Set COMPILE_LOG_LEVEL in build_flags to restrict which levels compile in.
// Default: LOG_INFO.
//
// Phase A: standalone macros (Serial only, no MQTT).
// Phase F+: this header will delegate to logging_utilities/logging_config.h
//           once PubSubClient is in lib_deps and MqttLogger is initialised.
// ═══════════════════════════════════════════════════════════════════════

enum log_level_t {
    LOG_NONE  = 0,
    LOG_ERROR = 1,
    LOG_WARN  = 2,
    LOG_INFO  = 3,
    LOG_DEBUG = 4,
    LOG_TRACE = 5,
};

#ifndef COMPILE_LOG_LEVEL
    #define COMPILE_LOG_LEVEL LOG_INFO
#endif

extern log_level_t current_log_level;

#if COMPILE_LOG_LEVEL >= LOG_ERROR
    #define LOG_ERROR(tag, fmt, ...) \
        do { if (current_log_level >= LOG_ERROR) \
            Serial.printf("[ERROR][%s] " fmt "\n", tag, ##__VA_ARGS__); } while(0)
#else
    #define LOG_ERROR(tag, fmt, ...) ((void)0)
#endif

#if COMPILE_LOG_LEVEL >= LOG_WARN
    #define LOG_WARN(tag, fmt, ...) \
        do { if (current_log_level >= LOG_WARN) \
            Serial.printf("[WARN][%s] " fmt "\n", tag, ##__VA_ARGS__); } while(0)
#else
    #define LOG_WARN(tag, fmt, ...) ((void)0)
#endif

#if COMPILE_LOG_LEVEL >= LOG_INFO
    #define LOG_INFO(tag, fmt, ...) \
        do { if (current_log_level >= LOG_INFO) \
            Serial.printf("[INFO][%s] " fmt "\n", tag, ##__VA_ARGS__); } while(0)
#else
    #define LOG_INFO(tag, fmt, ...) ((void)0)
#endif

#if COMPILE_LOG_LEVEL >= LOG_DEBUG
    #define LOG_DEBUG(tag, fmt, ...) \
        do { if (current_log_level >= LOG_DEBUG) \
            Serial.printf("[DEBUG][%s] " fmt "\n", tag, ##__VA_ARGS__); } while(0)
#else
    #define LOG_DEBUG(tag, fmt, ...) ((void)0)
#endif

#if COMPILE_LOG_LEVEL >= LOG_TRACE
    #define LOG_TRACE(tag, fmt, ...) \
        do { if (current_log_level >= LOG_TRACE) \
            Serial.printf("[TRACE][%s] " fmt "\n", tag, ##__VA_ARGS__); } while(0)
#else
    #define LOG_TRACE(tag, fmt, ...) ((void)0)
#endif
