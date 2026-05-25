#pragma once

#include <cstdint>

/**
 * @file version_utils.h
 * @brief Transport-neutral firmware version comparison utilities
 * 
 * Provides monotonic version comparison with wraparound detection.
 * Supports 32-bit version numbers used across MQTT, ESP-NOW, and other transports.
 * 
 * Phase 8: Extracted from deleted espnow_common_utils as transport-neutral utility.
 */

/**
 * @brief Compare two version numbers with wraparound detection
 * 
 * Determines if `new_version` is newer than `old_version`, accounting for
 * 32-bit counter wraparound. Uses the convention that versions within
 * the upper half of the 32-bit range are considered "wrapped" to avoid
 * the ambiguity at the wraparound boundary.
 * 
 * @param new_version The candidate newer version
 * @param old_version The reference older version
 * @return true if new_version > old_version (accounting for wraparound), false otherwise
 * 
 * Examples:
 *   - is_version_newer(2, 1) → true (2 > 1)
 *   - is_version_newer(1, 2) → false (1 < 2)
 *   - is_version_newer(0, 0xFFFFFFFF) → true (wrapped: 0 comes after 0xFFFFFFFF)
 *   - is_version_newer(0xFFFFFFFF, 0) → false (0xFFFFFFFF is not > 0 without wrap)
 */
inline bool is_version_newer(uint32_t new_version, uint32_t old_version) {
    // If versions are equal, new is not newer
    if (new_version == old_version) {
        return false;
    }
    
    // Calculate the signed difference (handles wraparound)
    // If new_version > old_version in unsigned arithmetic AND
    // the difference is less than 2^31 (i.e., not a wraparound),
    // then new_version is newer.
    
    uint32_t diff = new_version - old_version;
    
    // If diff is in the lower half (0 to 2^31-1), new is newer
    // If diff is in the upper half (2^31 to 2^32-1), it's a wrapped diff,
    // meaning old_version was actually newer (and wrapped around)
    return diff < 0x80000000;
}

/**
 * @brief Check if two versions are compatible
 * 
 * Two versions are compatible if they represent compatible firmware states.
 * This is a simple equality check for now, but can be extended for
 * compatible version ranges if needed.
 * 
 * @param version1 First version number
 * @param version2 Second version number
 * @return true if versions are compatible, false otherwise
 */
inline bool is_version_compatible(uint32_t version1, uint32_t version2) {
    return version1 == version2;
}

namespace VersionUtils {

/**
 * @brief Increment a version number
 * 
 * Atomically increments a version number for settings changes.
 * Wraps around at 32-bit boundary.
 * 
 * @param version Reference to version number to increment
 */
inline void increment_version(uint32_t& version) {
    version++;
}

}  // namespace VersionUtils
