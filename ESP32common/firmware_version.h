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

// Version compatibility structure
struct VersionCompatibility {
    uint32_t my_version;
    uint32_t min_peer_version;
    uint32_t max_peer_version;
};

// Device identification (set by build flags from platformio.ini)
#ifdef RECEIVER_DEVICE
    #define DEVICE_TYPE "RECEIVER"
#elif defined(TRANSMITTER_DEVICE)
    #define DEVICE_TYPE "TRANSMITTER"
#else
    #define DEVICE_TYPE "UNKNOWN"
#endif

// Device hardware name (pulled from platformio.ini build flags)
#ifndef DEVICE_HARDWARE
    #define DEVICE_HARDWARE "UNKNOWN"
#endif
#define DEVICE_NAME DEVICE_HARDWARE

// Helper function to format version string
inline String getFirmwareVersionString() {
    return String(FW_VERSION_STRING) + " (" + FW_BUILD_DATE + " " + FW_BUILD_TIME + ")";
}

// Helper to check compatibility (dynamic major version matching)
inline bool isVersionCompatible(uint32_t otherVersion) {
    // Calculate compatibility range based on current major version
    // Compatible = same major version (any minor/patch)
    // Example: v2.0.0 accepts v2.0.0 to v2.99.99
    uint16_t my_major = FW_VERSION_MAJOR;
    uint32_t min_compatible = my_major * 10000;        // Same major version minimum
    uint32_t max_compatible = (my_major + 1) * 10000 - 1;  // Up to next major version
    
    // Check if other version is within compatible range
    return (otherVersion >= min_compatible && otherVersion <= max_compatible);
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
