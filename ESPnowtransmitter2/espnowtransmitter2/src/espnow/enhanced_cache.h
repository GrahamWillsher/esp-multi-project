#pragma once

#include <Arduino.h>
#include <espnow_common.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**
 * @file enhanced_cache.h
 * @brief Section 11 Enhanced Cache - Dual Storage (Transient + State)
 * 
 * CRITICAL ARCHITECTURE:
 * - Transient data (battery readings): FIFO queue (250 entries), delete after ACK
 * - State data (IP, MQTT, settings): Versioned slots, NEVER delete
 * - TX-only NVS persistence for state data
 * - Non-blocking for Battery Emulator control code (< 100µs writes)
 * 
 * Section 11 Architecture: Transmitter-Active with Bidirectional Sync
 */

// ═══════════════════════════════════════════════════════════════════════════
// DATA TYPE CLASSIFICATION
// ═══════════════════════════════════════════════════════════════════════════

enum class CacheDataType : uint8_t {
    TRANSIENT_DATA = 0,    // Battery readings - delete after ACK
    STATE_NETWORK = 1,     // Network config - version tracked, persisted
    STATE_MQTT = 2,        // MQTT config - version tracked, persisted
    STATE_BATTERY = 3      // Battery settings - version tracked, persisted
};

// ═══════════════════════════════════════════════════════════════════════════
// CACHE ENTRY STRUCTURES
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Transient data entry (battery telemetry)
 */
struct TransientEntry {
    espnow_payload_t data;      // Battery data payload
    uint32_t seq;               // Sequence number (ordering)
    uint32_t timestamp;         // Cached timestamp (millis())
    bool sent;                  // Transmitted to receiver
    bool acked;                 // Acknowledged by receiver
    uint8_t retry_count;        // Transmission retries
};

/**
 * @brief State data entry (configuration)
 */
struct StateEntry {
    CacheDataType type;         // Which config type
    uint16_t version;           // Version number (increments)
    uint32_t timestamp;         // Change timestamp
    bool sent;                  // Transmitted to receiver
    bool acked;                 // Acknowledged by receiver
    bool is_latest;             // Current active version
    
    union {
        struct {
            char ip[16];
            char gateway[16];
            char subnet[16];
            bool is_dhcp;
        } network;
        
        struct {
            char server[64];
            uint16_t port;
            char username[32];
            char password[32];
            bool enabled;
        } mqtt;
        
        struct {
            uint32_t capacity_wh;
            uint16_t nominal_voltage;
            uint8_t cell_count;
            uint16_t max_voltage;
            uint16_t min_voltage;
            float max_charge_current;
            float max_discharge_current;
        } battery;
    } config;
};

// ═══════════════════════════════════════════════════════════════════════════
// STATISTICS
// ═══════════════════════════════════════════════════════════════════════════

struct CacheStats {
    // Transient data
    size_t transient_current;
    size_t transient_max_reached;
    uint32_t transient_added;
    uint32_t transient_sent;
    uint32_t transient_acked;
    uint32_t transient_dropped;
    
    // State data
    uint32_t state_updates;
    uint32_t state_sent;
    uint32_t state_acked;
    uint32_t state_conflicts;
    
    // Timing
    uint32_t avg_cache_duration_ms;
    uint32_t max_cache_duration_ms;
    
    // Errors
    uint32_t mutex_timeouts;
    uint32_t overflow_events;
};

// ═══════════════════════════════════════════════════════════════════════════
// ENHANCED CACHE CLASS
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Enhanced cache with dual storage model
 * 
 * ARCHITECTURE:
 * - Transient queue: 250 entries (FIFO, dual battery support: 2×96 cells)
 * - State slots: Fixed versioned slots (network, MQTT, battery)
 * - Non-blocking: 10ms mutex timeout (doesn't block Battery Emulator)
 * - Thread-safe: FreeRTOS mutex protection
 */
class EnhancedCache {
public:
    /**
     * @brief Get singleton instance
     */
    static EnhancedCache& instance() {
        static EnhancedCache inst;
        return inst;
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // TRANSIENT DATA OPERATIONS (Battery Readings)
    // ═══════════════════════════════════════════════════════════════════════
    
    /**
     * @brief Add transient battery data to cache
     * @param data Battery telemetry payload  
     * @param timestamp Cache timestamp (millis())
     * @param seq Sequence number
     * @return true if cached, false if full or timeout
     * 
     * NON-BLOCKING: Returns in < 100µs (doesn't block control code)
     */
    bool add_transient(const espnow_payload_t& data, uint32_t timestamp, uint32_t seq);
    
    /**
     * @brief Add transient battery data to cache (simplified - auto-generates timestamp and seq)
     * @param data Battery telemetry payload
     * @return true if cached, false if full or timeout
     */
    bool add_transient(const espnow_payload_t& data);
    
    /**
     * @brief Peek next unsent transient entry (non-destructive)
     * @return Pointer to entry or nullptr if none
     */
    TransientEntry* peek_next_transient();
    
    /**
     * @brief Peek next unsent transient entry (non-destructive, const-safe)
     * @param entry Output parameter for entry data
     * @return true if entry found, false if queue empty
     */
    bool peek_next_transient(TransientEntry& entry) const;
    
    /**
     * @brief Mark transient entry as sent
     */
    void mark_transient_sent(uint32_t seq);
    
    /**
     * @brief Mark transient entry as acknowledged
     */
    void mark_transient_acked(uint32_t seq);
    
    /**
     * @brief Remove all acknowledged transient entries (cleanup task)
     * @return Number of entries removed
     */
    size_t cleanup_acked_transient();
    
    /**
     * @brief Get transient queue status
     */
    size_t transient_count() const { return transient_count_; }
    size_t transient_unsent_count() const;
    size_t transient_unacked_count() const;
    bool has_unsent_transient() const;
    
    // ═══════════════════════════════════════════════════════════════════════
    // STATE DATA OPERATIONS (Configuration)
    // ═══════════════════════════════════════════════════════════════════════
    
    /**
     * @brief Update state configuration (NEVER REMOVES OLD)
     * @param type State type (network/MQTT/battery)
     * @param entry State data with version
     * 
     * State data is NEVER deleted after ACK - only version-updated
     */
    void update_state(CacheDataType type, const StateEntry& entry);
    
    /**
     * @brief Get current state entry
     * @return Pointer to state or nullptr if not initialized
     */
    const StateEntry* get_state(CacheDataType type) const;
    
    /**
     * @brief Get current state entry (output parameter version)
     * @param type State type
     * @param entry Output parameter for state data
     * @return true if state exists
     */
    bool get_state(CacheDataType type, StateEntry& entry) const;
    
    /**
     * @brief Mark state as sent
     */
    void mark_state_sent(CacheDataType type);
    
    /**
     * @brief Mark state as acknowledged
     */
    void mark_state_acked(CacheDataType type);
    
    /**
     * @brief Check if state has unsent changes
     */
    bool has_unsent_state(CacheDataType type) const;
    
    /**
     * @brief Check if ANY state has unsent changes
     */
    bool has_unsent_state() const;
    
    // ═══════════════════════════════════════════════════════════════════════
    // PERSISTENCE (TX-ONLY NVS)
    // ═══════════════════════════════════════════════════════════════════════
    
    /**
     * @brief Persist state data to NVS (TX-only)
     * @param type State type to persist
     * @return true if successful
     */
    bool persist_state_to_nvs(CacheDataType type);
    
    /**
     * @brief Restore state data from NVS (TX-only, boot)
     * @param type State type to restore
     * @return true if restored successfully
     */
    bool restore_state_from_nvs(CacheDataType type);
    
    /**
     * @brief Restore all state from NVS (boot sequence)
     */
    void restore_all_from_nvs();
    
    // ═══════════════════════════════════════════════════════════════════════
    // STATISTICS & DIAGNOSTICS
    // ═══════════════════════════════════════════════════════════════════════
    
    CacheStats get_stats() const { return stats_; }
    void log_stats() const;
    void reset_stats();
    
private:
    EnhancedCache();
    ~EnhancedCache();
    
    // Delete copy/move for singleton
    EnhancedCache(const EnhancedCache&) = delete;
    EnhancedCache& operator=(const EnhancedCache&) = delete;
    
    // ═══════════════════════════════════════════════════════════════════════
    // STORAGE (Dual Model)
    // ═══════════════════════════════════════════════════════════════════════
    
    static constexpr size_t TRANSIENT_QUEUE_SIZE = 250;  // Dual battery: 192 cells + headroom
    static constexpr uint32_t MUTEX_TIMEOUT_MS = 10;     // Non-blocking timeout
    
    // Transient data (FIFO circular buffer)
    TransientEntry transient_queue_[TRANSIENT_QUEUE_SIZE];
    size_t transient_write_idx_;
    size_t transient_read_idx_;
    size_t transient_count_;
    
    // State data (fixed versioned slots - NEVER deleted)
    StateEntry state_network_;
    StateEntry state_mqtt_;
    StateEntry state_battery_;
    
    // Thread safety
    SemaphoreHandle_t mutex_;
    
    // Statistics
    mutable CacheStats stats_;
    
    // Helpers
    StateEntry* get_state_slot(CacheDataType type);
    const StateEntry* get_state_slot(CacheDataType type) const;
    const char* get_nvs_key(CacheDataType type) const;
};
