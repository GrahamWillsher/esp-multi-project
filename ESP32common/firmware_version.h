#pragma once

#include <Arduino.h>

// Firmware version (Semantic Versioning: MAJOR.MINOR.PATCH)
// These can be overridden by build flags in platformio.ini
#ifndef FW_VERSION_MAJOR
    #define FW_VERSION_MAJOR 1
#endif
#ifndef FW_VERSION_MINOR
    #define FW_VERSION_MINOR 0
#endif
#ifndef FW_VERSION_PATCH
    #define FW_VERSION_PATCH 0
#endif

// Computed version number for comparison
#define FW_VERSION_NUMBER ((FW_VERSION_MAJOR * 10000) + (FW_VERSION_MINOR * 100) + FW_VERSION_PATCH)

// String representation (dynamically constructed from version macros)
#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)
#define FW_VERSION_STRING STRINGIFY(FW_VERSION_MAJOR) "." STRINGIFY(FW_VERSION_MINOR) "." STRINGIFY(FW_VERSION_PATCH)

// Build date/time (automatically set by compiler)
#define FW_BUILD_DATE __DATE__
#define FW_BUILD_TIME __TIME__

// Protocol version (increment when ESP-NOW protocol changes)
#define PROTOCOL_VERSION 1

// Minimum compatible version (refuse to run with older incompatible firmware on other device)
#define MIN_COMPATIBLE_VERSION 10000  // 1.0.0

// Device identification (set by build flags)
#ifdef RECEIVER_DEVICE
    #define DEVICE_TYPE "RECEIVER"
    #define DEVICE_NAME "LilyGo-T-Display-S3"
#elif defined(TRANSMITTER_DEVICE)
    #define DEVICE_TYPE "TRANSMITTER"
    #define DEVICE_NAME "ESP32-POE-ISO"
#else
    #define DEVICE_TYPE "UNKNOWN"
    #define DEVICE_NAME "UNKNOWN"
#endif

// Helper function to format version string
inline String getFirmwareVersionString() {
    return String(FW_VERSION_STRING) + " (" + FW_BUILD_DATE + " " + FW_BUILD_TIME + ")";
}

// Helper to check compatibility
inline bool isVersionCompatible(uint32_t otherVersion) {
    return (otherVersion >= MIN_COMPATIBLE_VERSION);
}

// Helper to extract version components
inline void getVersionComponents(uint32_t version, uint16_t& major, uint16_t& minor, uint16_t& patch) {
    major = version / 10000;
    minor = (version / 100) % 100;
    patch = version % 100;
}

// Helper to format version number as string
inline String formatVersion(uint32_t version) {
    uint16_t major, minor, patch;
    getVersionComponents(version, major, minor, patch);
    return String(major) + "." + String(minor) + "." + String(patch);
}
