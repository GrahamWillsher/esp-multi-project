#pragma once

#include <stdint.h>
#include <cstddef>

/**
 * Firmware Metadata Structure
 * 
 * This structure is embedded in the .rodata section of the firmware binary
 * with magic markers for identification. Used for display purposes only.
 * 
 * Total size: 128 bytes (fixed, padded with reserved space)
 */

namespace FirmwareMetadata {
    // Magic markers for binary search
    constexpr uint32_t MAGIC_START = 0x464D5441;  // "FMTA" in little-endian
    constexpr uint32_t MAGIC_END = 0x454E4446;    // "ENDF" in little-endian
    
    /**
     * Metadata structure - MUST be 128 bytes total
     * Packed to ensure consistent layout across compilers
     */
    struct __attribute__((packed)) Metadata {
        uint32_t magic_start;       // Offset 0: Magic marker 0x464D5441
        char env_name[32];          // Offset 4: Environment name (e.g., "lilygo-t-display-s3")
        char device_type[16];       // Offset 36: Device type ("RECEIVER" or "TRANSMITTER")
        uint8_t version_major;      // Offset 52: Major version number
        uint8_t version_minor;      // Offset 53: Minor version number
        uint8_t version_patch;      // Offset 54: Patch version number
        uint8_t reserved1;          // Offset 55: Reserved for alignment
        char build_date[48];        // Offset 56: Human-readable build date
        uint8_t reserved[20];       // Offset 104: Reserved for future use
        uint32_t magic_end;         // Offset 124: Magic marker 0x454E4446
    };                              // Total: 128 bytes
    
    // Global metadata instance - defined in firmware_metadata.cpp
    extern const Metadata metadata;
    
    // Helper function to check if metadata is valid
    inline bool isValid(const Metadata& m) {
        return (m.magic_start == MAGIC_START && m.magic_end == MAGIC_END);
    }
    
    /**
     * Get firmware info as formatted string
     * Returns metadata if valid (with ● indicator), otherwise falls back to build flags (with *)
     * 
     * @param buffer Output buffer for formatted string
     * @param bufSize Size of output buffer
     * @param includeBuildDate If true, includes build date on second line
     */
    void getInfoString(char* buffer, size_t bufSize, bool includeBuildDate = false);
}
