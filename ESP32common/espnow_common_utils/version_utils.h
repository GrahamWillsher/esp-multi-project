#ifndef VERSION_UTILS_H
#define VERSION_UTILS_H

#include <stdint.h>

/**
 * @brief Version Utilities for Embedded Systems
 * 
 * Industry-standard versioning with:
 * - Fixed-width version fields (8, 16, or 32-bit)
 * - Monotonic increment with automatic rollover
 * - Wrap-around safe comparison logic
 * 
 * Example:
 *   32-bit: 0xFFFFFFFE -> 0xFFFFFFFF -> 0x00000000 -> 0x00000001
 *   The comparison logic correctly identifies 0x00000000 as newer than 0xFFFFFFFF
 */

namespace VersionUtils {

/**
 * @brief Increment a version number with automatic rollover
 * 
 * @tparam T Version type (uint8_t, uint16_t, or uint32_t)
 * @param version Current version (will be incremented in place)
 * @return New version value
 * 
 * Example:
 *   uint32_t v = 0xFFFFFFFF;
 *   increment_version(v);  // v is now 0x00000000
 */
template<typename T>
inline T increment_version(T& version) {
    version++;  // Unsigned overflow is well-defined in C++ (wraps to 0)
    return version;
}

/**
 * @brief Check if version_new is newer than version_old (wrap-around safe)
 * 
 * Uses "half-range" comparison to handle wrap-around correctly:
 * - If difference is in first half of range: version_new is newer
 * - If difference is in second half of range: version_old is newer
 * 
 * @tparam T Version type (uint8_t, uint16_t, or uint32_t)
 * @param version_new The version to check
 * @param version_old The reference version to compare against
 * @return true if version_new is newer than version_old
 * 
 * Examples (32-bit):
 *   is_version_newer(1, 0)           -> true   (normal increment)
 *   is_version_newer(100, 50)        -> true   (normal increment)
 *   is_version_newer(0, 0xFFFFFFFF)  -> true   (rollover increment)
 *   is_version_newer(5, 0xFFFFFFF0)  -> true   (rollover with gap)
 *   is_version_newer(50, 100)        -> false  (going backwards)
 *   is_version_newer(0, 0x80000000)  -> false  (too far, likely backwards)
 */
template<typename T>
inline bool is_version_newer(T version_new, T version_old) {
    // Calculate difference (unsigned arithmetic wraps correctly)
    T diff = version_new - version_old;
    
    // If diff is 0, versions are equal (not newer)
    if (diff == 0) {
        return false;
    }
    
    // If diff is in the first half of the range, new version is newer
    // This handles wrap-around: diff=1 after rollover is still < half-range
    // The half-range is the maximum value that could be a "forward" increment
    constexpr T HALF_RANGE = (T)(((uint64_t)1 << (sizeof(T) * 8 - 1)));
    
    return diff < HALF_RANGE;
}

/**
 * @brief Check if two versions are equal
 * 
 * @tparam T Version type (uint8_t, uint16_t, or uint32_t)
 * @param v1 First version
 * @param v2 Second version
 * @return true if versions are equal
 */
template<typename T>
inline bool is_version_equal(T v1, T v2) {
    return v1 == v2;
}

/**
 * @brief Calculate version distance (how many increments between versions)
 * 
 * Returns the forward distance from version_old to version_new.
 * Note: Only valid if version_new is actually newer (check with is_version_newer first)
 * 
 * @tparam T Version type (uint8_t, uint16_t, or uint32_t)
 * @param version_new The newer version
 * @param version_old The older version
 * @return Number of increments between versions
 * 
 * Example:
 *   distance(5, 2)           -> 3
 *   distance(2, 0xFFFFFFFE)  -> 4  (0xFFFFFFFE -> 0xFFFFFFFF -> 0 -> 1 -> 2)
 */
template<typename T>
inline T version_distance(T version_new, T version_old) {
    return version_new - version_old;  // Unsigned subtraction handles wrap-around
}

} // namespace VersionUtils

#endif // VERSION_UTILS_H
