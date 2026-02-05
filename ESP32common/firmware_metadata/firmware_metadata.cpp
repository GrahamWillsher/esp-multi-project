#include "firmware_metadata.h"
#include <stdio.h>
#include <string.h>

// Helper macro to stringify build flags
#ifndef STRINGIFY
#define STRINGIFY(x) #x
#endif

#ifndef TOSTRING
#define TOSTRING(x) STRINGIFY(x)
#endif

// Default values if build flags are not provided
#ifndef PIO_ENV_NAME
#define PIO_ENV_NAME unknown
#endif

#ifndef TARGET_DEVICE
#define TARGET_DEVICE UNKNOWN
#endif

#ifndef FW_VERSION_MAJOR
#define FW_VERSION_MAJOR 0
#endif

#ifndef FW_VERSION_MINOR
#define FW_VERSION_MINOR 0
#endif

#ifndef FW_VERSION_PATCH
#define FW_VERSION_PATCH 0
#endif

#ifndef BUILD_DATE
#define BUILD_DATE "Unknown build date"
#endif

/**
 * Global firmware metadata instance
 * 
 * This is placed in the .rodata section (read-only data) and will be
 * embedded in the final .bin file. It can be found by searching for
 * the magic markers (0x464D5441 and 0x454E4446).
 * 
 * Values are populated at compile time from build flags passed via
 * platformio.ini.
 */
namespace FirmwareMetadata {
    // Use compound literal initialization for C++ compatibility
    const Metadata metadata __attribute__((section(".rodata"))) = {
        MAGIC_START,                    // magic_start
        TOSTRING(PIO_ENV_NAME),        // env_name
        TOSTRING(TARGET_DEVICE),       // device_type
        FW_VERSION_MAJOR,              // version_major
        FW_VERSION_MINOR,              // version_minor
        FW_VERSION_PATCH,              // version_patch
        0,                             // reserved1
        TOSTRING(BUILD_DATE),          // build_date
        {0},                           // reserved
        MAGIC_END                      // magic_end
    };
    
    /**
     * Get firmware info as formatted string
     * 
     * This returns the metadata information without directly using Serial.print,
     * allowing the caller to use their preferred logging system.
     */
    void getInfoString(char* buffer, size_t bufSize, bool includeBuildDate) {
        if (isValid(metadata)) {
            // Metadata is embedded and valid
            if (includeBuildDate) {
                snprintf(buffer, bufSize, 
                    "Firmware: %s %s v%d.%d.%d ●\nBuilt: %s",
                    metadata.device_type,
                    metadata.env_name,
                    metadata.version_major,
                    metadata.version_minor,
                    metadata.version_patch,
                    metadata.build_date);
            } else {
                snprintf(buffer, bufSize, 
                    "Firmware: %s %s v%d.%d.%d ●",
                    metadata.device_type,
                    metadata.env_name,
                    metadata.version_major,
                    metadata.version_minor,
                    metadata.version_patch);
            }
        } else {
            // Fallback to build flags (no embedded metadata)
            if (includeBuildDate) {
                snprintf(buffer, bufSize, 
                    "Firmware: v%d.%d.%d *\n(No embedded metadata)",
                    FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
            } else {
                snprintf(buffer, bufSize, 
                    "Firmware: v%d.%d.%d *",
                    FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
            }
        }
    }
}
