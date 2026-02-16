#pragma once

#include <Arduino.h>
#include <time.h>
#include <sys/time.h>

/**
 * @brief Time Manager for Transmitter
 * 
 * Manages NTP synchronization and provides accurate time to other components.
 * Time is distributed to receivers via enhanced heartbeat messages.
 * 
 * Features:
 * - NTP synchronization with automatic retry
 * - Time source tracking (Unsynced/NTP/Manual/GPS)
 * - Unix timestamp and uptime tracking
 * - Configurable NTP servers and timezone
 */
class TimeManager {
public:
    enum class TimeSource : uint8_t {
        UNSYNCED = 0,
        NTP      = 1,
        MANUAL   = 2,
        GPS      = 3
    };
    
    static TimeManager& instance();
    
    /**
     * @brief Initialize time manager and start NTP sync
     * @param ntp_server Primary NTP server (default: "pool.ntp.org")
     * @param tz_offset Timezone offset in seconds (default: 0 UTC)
     */
    void init(const char* ntp_server = "pool.ntp.org", long tz_offset = 0);
    
    /**
     * @brief Check if time is synchronized
     */
    bool is_synced() const { return time_source_ != TimeSource::UNSYNCED; }
    
    /**
     * @brief Get current Unix timestamp (seconds since epoch)
     */
    uint64_t get_unix_time() const;
    
    /**
     * @brief Get uptime in milliseconds
     */
    uint64_t get_uptime_ms() const;
    
    /**
     * @brief Get time source
     */
    TimeSource get_time_source() const { return time_source_; }
    
    /**
     * @brief Get time source as uint8_t (for ESP-NOW messages)
     */
    uint8_t get_time_source_byte() const { return static_cast<uint8_t>(time_source_); }
    
    /**
     * @brief Get time source as string
     */
    const char* get_time_source_string() const;
    
    /**
     * @brief Manually set time (for manual time source)
     * @param unix_time Unix timestamp in seconds
     */
    void set_time_manual(uint64_t unix_time);
    
    /**
     * @brief Force NTP resync
     */
    void force_ntp_resync();
    
    /**
     * @brief Get last NTP sync timestamp
     */
    uint64_t get_last_ntp_sync() const { return last_ntp_sync_; }
    
    /**
     * @brief Get time since last NTP sync (seconds)
     */
    uint32_t get_time_since_ntp_sync() const;
    
private:
    TimeManager() = default;
    ~TimeManager() = default;
    
    // Prevent copying
    TimeManager(const TimeManager&) = delete;
    TimeManager& operator=(const TimeManager&) = delete;
    
    // NTP callback (called when time is synchronized)
    static void time_sync_notification_cb(struct timeval *tv);
    
    // State
    TimeSource time_source_{TimeSource::UNSYNCED};
    uint64_t last_ntp_sync_{0};
    bool ntp_initialized_{false};
    
    // NTP configuration
    char ntp_server_[64]{"pool.ntp.org"};
    long tz_offset_{0};
    
    // Uptime tracking (handle millis() overflow)
    uint64_t uptime_ms_{0};
    uint32_t last_millis_{0};
    
    // NTP sync constants
    static constexpr uint32_t NTP_RESYNC_INTERVAL_MS = 3600000; // 1 hour
};
