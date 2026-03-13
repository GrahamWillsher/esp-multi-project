#include "battery_settings_cache.h"
#include "../../lib/webserver/logging.h"

BatterySettingsCache& BatterySettingsCache::instance() {
    static BatterySettingsCache instance;
    return instance;
}

void BatterySettingsCache::init() {
    if (initialized_) {
        LOG_WARN("BATTERY_CACHE", "Already initialized");
        return;
    }
    
    Preferences prefs;
    if (prefs.begin("batt_cache", true)) {
        version_ = prefs.getUInt("version", 0);
        prefs.end();
        LOG_INFO("BATTERY_CACHE", "Loaded version %u from NVS", version_);
    } else {
        LOG_INFO("BATTERY_CACHE", "No cached version, starting at 0");
        version_ = 0;
    }
    
    initialized_ = true;
}

bool BatterySettingsCache::update_version(uint32_t new_version) {
    if (new_version != version_) {
        LOG_INFO("BATTERY_CACHE", "Version changed: %u → %u", version_, new_version);
        version_ = new_version;
        save_version();
        return true;  // Needs refresh
    }
    return false;  // No change
}

void BatterySettingsCache::save_version() {
    Preferences prefs;
    if (prefs.begin("batt_cache", false)) {
        prefs.putUInt("version", version_);
        prefs.end();
        LOG_DEBUG("BATTERY_CACHE", "Saved version %u to NVS", version_);
    } else {
        LOG_ERROR("BATTERY_CACHE", "Failed to save version to NVS");
    }
}

void BatterySettingsCache::mark_updated(uint32_t new_version) {
    LOG_INFO("BATTERY_CACHE", "Settings updated to version %u", new_version);
    version_ = new_version;
    save_version();
}
