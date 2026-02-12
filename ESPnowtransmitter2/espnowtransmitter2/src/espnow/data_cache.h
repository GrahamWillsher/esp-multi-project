#pragma once

#include <Arduino.h>
#include <vector>
#include <espnow_common.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**
 * @file data_cache.h
 * @brief Static data cache for ESP-NOW messages
 * 
 * Stores battery data during ESP-NOW initialization phase when receiver
 * is not yet discovered. Once receiver connection is established, cache
 * is automatically flushed.
 * 
 * This allows transmitter to continue collecting data even before ESP-NOW
 * connection is available, eliminating data loss during boot.
 */

class DataCache {
public:
    /**
     * @brief Get singleton instance
     */
    static DataCache& instance() {
        static DataCache inst;
        return inst;
    }
    
    /**
     * @brief Add data to cache
     * @param data ESP-NOW payload to cache
     * @return true if cached successfully, false if cache full
     */
    bool add(const espnow_payload_t& data);
    
    /**
     * @brief Flush cache to receiver
     * Sends all cached messages via ESP-NOW in FIFO order
     * @return Number of messages sent successfully
     */
    size_t flush();
    
    /**
     * @brief Get number of cached messages
     */
    size_t size() const { 
        return cache_.size(); 
    }
    
    /**
     * @brief Check if cache is full
     */
    bool is_full() const { 
        return cache_.size() >= MAX_CACHE_SIZE; 
    }
    
    /**
     * @brief Check if cache is empty
     */
    bool is_empty() const {
        return cache_.empty();
    }
    
    /**
     * @brief Clear cache without sending
     */
    void clear();
    
    /**
     * @brief Get cache statistics
     */
    struct Stats {
        size_t current_size;
        size_t total_added;
        size_t total_flushed;
        size_t total_dropped;
        size_t max_size_reached;
    };
    
    Stats get_stats() const {
        return stats_;
    }
    
private:
    DataCache();
    ~DataCache();
    
    // Delete copy/move constructors for singleton
    DataCache(const DataCache&) = delete;
    DataCache& operator=(const DataCache&) = delete;
    
    static constexpr size_t MAX_CACHE_SIZE = 100;  ///< Maximum cached messages
    
    std::vector<espnow_payload_t> cache_;
    SemaphoreHandle_t mutex_;
    
    // Statistics
    mutable Stats stats_{0, 0, 0, 0, 0};
};
