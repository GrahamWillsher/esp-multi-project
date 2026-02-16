#include "time_manager.h"
#include "../config/logging_config.h"
#include <esp_sntp.h>

TimeManager& TimeManager::instance() {
    static TimeManager instance;
    return instance;
}

void TimeManager::init(const char* ntp_server, long tz_offset) {
    if (ntp_initialized_) {
        LOG_WARN("TIME", "Time manager already initialized");
        return;
    }
    
    // Store configuration
    strncpy(ntp_server_, ntp_server, sizeof(ntp_server_) - 1);
    ntp_server_[sizeof(ntp_server_) - 1] = '\0';
    tz_offset_ = tz_offset;
    
    LOG_INFO("TIME", "Time manager initialized (using system time from ethernet_utilities NTP)");
    
    // NOTE: DO NOT initialize SNTP here - ethernet_utilities already handles NTP
    // Initializing SNTP here would interfere with WiFi channel locking for ESP-NOW
    // We simply read the system time that's already being synchronized
    
    ntp_initialized_ = true;
}

uint64_t TimeManager::get_unix_time() const {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    
    // Check if time is valid (year > 2020)
    struct tm timeinfo;
    gmtime_r(&tv.tv_sec, &timeinfo);
    if ((timeinfo.tm_year + 1900) <= 2020) {
        return 0;  // Time not synced yet
    }
    
    // Update time source based on valid time
    const_cast<TimeManager*>(this)->time_source_ = TimeSource::NTP;
    
    return static_cast<uint64_t>(tv.tv_sec);
}

uint64_t TimeManager::get_uptime_ms() const {
    // Handle millis() overflow (every 49.7 days)
    uint32_t current_millis = millis();
    uint64_t uptime = uptime_ms_;
    
    if (current_millis < last_millis_) {
        // Overflow detected
        uptime += (0xFFFFFFFF - last_millis_) + current_millis + 1;
    } else {
        uptime += (current_millis - last_millis_);
    }
    
    // Update state (need to cast away const - this is a non-critical update)
    const_cast<TimeManager*>(this)->uptime_ms_ = uptime;
    const_cast<TimeManager*>(this)->last_millis_ = current_millis;
    
    return uptime;
}

const char* TimeManager::get_time_source_string() const {
    switch (time_source_) {
        case TimeSource::UNSYNCED: return "Unsynced";
        case TimeSource::NTP:      return "NTP";
        case TimeSource::MANUAL:   return "Manual";
        case TimeSource::GPS:      return "GPS";
        default:                   return "Unknown";
    }
}

void TimeManager::set_time_manual(uint64_t unix_time) {
    struct timeval tv;
    tv.tv_sec = unix_time;
    tv.tv_usec = 0;
    
    if (settimeofday(&tv, nullptr) == 0) {
        time_source_ = TimeSource::MANUAL;
        LOG_INFO("TIME", "Time set manually to %llu", unix_time);
    } else {
        LOG_ERROR("TIME", "Failed to set time manually");
    }
}

void TimeManager::force_ntp_resync() {
    // Not applicable - ethernet_utilities handles NTP synchronization
    LOG_WARN("TIME", "force_ntp_resync() not available - NTP managed by ethernet_utilities");
}

uint32_t TimeManager::get_time_since_ntp_sync() const {
    if (last_ntp_sync_ == 0) {
        return 0xFFFFFFFF;
    }
    
    uint64_t current_time = get_unix_time();
    if (current_time == 0) {
        return 0xFFFFFFFF;
    }
    
    return static_cast<uint32_t>(current_time - last_ntp_sync_);
}

void TimeManager::time_sync_notification_cb(struct timeval *tv) {
    TimeManager& mgr = instance();
    
    // Update time source
    mgr.time_source_ = TimeSource::NTP;
    mgr.last_ntp_sync_ = static_cast<uint64_t>(tv->tv_sec);
    
    // Log time sync success
    struct tm timeinfo;
    localtime_r(&tv->tv_sec, &timeinfo);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    LOG_INFO("TIME", "NTP sync successful: %s UTC", time_str);
}
