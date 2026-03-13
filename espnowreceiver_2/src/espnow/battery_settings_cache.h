#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <version_utils.h>

/**
 * @brief Battery Settings Cache - Stores local copy of transmitter's battery settings
 * 
 * Implements Option 3.1 A from synchronization strategy:
 * - Maintains version-tracked cache of battery settings
 * - Compares versions with transmitter notifications
 * - Requests updates when version mismatch detected
 */
class BatterySettingsCache {
public:
    static BatterySettingsCache& instance();
    
    /**
     * @brief Initialize cache and load from NVS
     */
    void init();
    
    /**
     * @brief Get cached battery settings version
     * @return Current version number
     */
    uint32_t get_version() const { return version_; }
    
    /**
     * @brief Update version number (when change notification received)
     * @param new_version New version from transmitter
     * @return true if version changed (needs re-request)
     */
    bool update_version(uint32_t new_version);
    
    /**
     * @brief Save version to NVS
     */
    void save_version();
    
    /**
     * @brief Check if settings need refresh
     * @param transmitter_version Version from transmitter
     * @return true if local version is out of date
     */
    bool needs_refresh(uint32_t transmitter_version) const {
        // Use wrap-around safe comparison from common utilities
        return VersionUtils::is_version_newer(transmitter_version, version_);
    }
    
    /**
     * @brief Mark that settings were successfully updated
     * @param new_version New version number
     */
    void mark_updated(uint32_t new_version);
    
private:
    BatterySettingsCache() = default;
    ~BatterySettingsCache() = default;
    
    // Prevent copying
    BatterySettingsCache(const BatterySettingsCache&) = delete;
    BatterySettingsCache& operator=(const BatterySettingsCache&) = delete;
    
    uint32_t version_{0};           // Current cached version
    bool initialized_{false};
};
