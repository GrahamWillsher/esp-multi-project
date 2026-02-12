#include "data_cache.h"
#include "../config/logging_config.h"
#include <esp_now.h>

extern uint8_t receiver_mac[6];  // From espnow_transmitter library

DataCache::DataCache() {
    mutex_ = xSemaphoreCreateMutex();
    if (mutex_ == NULL) {
        LOG_ERROR("[CACHE] Failed to create mutex!");
    }
    cache_.reserve(MAX_CACHE_SIZE);
    LOG_DEBUG("[CACHE] Initialized (max size: %d)", MAX_CACHE_SIZE);
}

DataCache::~DataCache() {
    if (mutex_ != NULL) {
        vSemaphoreDelete(mutex_);
    }
}

bool DataCache::add(const espnow_payload_t& data) {
    if (mutex_ == NULL) {
        LOG_ERROR("[CACHE] Mutex not initialized");
        return false;
    }
    
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        bool success = false;
        
        if (cache_.size() < MAX_CACHE_SIZE) {
            cache_.push_back(data);
            stats_.current_size = cache_.size();
            stats_.total_added++;
            
            if (cache_.size() > stats_.max_size_reached) {
                stats_.max_size_reached = cache_.size();
            }
            
            LOG_DEBUG("[CACHE] Data cached (SOC=%d%%, Power=%dW, total: %d/%d)", 
                     data.soc, data.power, cache_.size(), MAX_CACHE_SIZE);
            success = true;
        } else {
            // Cache full - drop oldest message (FIFO)
            LOG_WARN("[CACHE] Cache full (%d), dropping oldest message", MAX_CACHE_SIZE);
            cache_.erase(cache_.begin());
            cache_.push_back(data);
            stats_.total_dropped++;
            stats_.total_added++;
            success = true;
        }
        
        xSemaphoreGive(mutex_);
        return success;
    } else {
        LOG_ERROR("[CACHE] Failed to acquire mutex for add");
        return false;
    }
}

size_t DataCache::flush() {
    if (mutex_ == NULL) {
        LOG_ERROR("[CACHE] Mutex not initialized");
        return 0;
    }
    
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        LOG_ERROR("[CACHE] Failed to acquire mutex for flush");
        return 0;
    }
    
    size_t sent_count = 0;
    size_t total_messages = cache_.size();
    
    if (total_messages == 0) {
        xSemaphoreGive(mutex_);
        return 0;
    }
    
    LOG_INFO("[CACHE] ═══ Flushing %d cached messages ═══", total_messages);
    
    for (const auto& data : cache_) {
        // Send via ESP-NOW
        esp_err_t result = esp_now_send(receiver_mac, 
                                       (const uint8_t*)&data, 
                                       sizeof(espnow_payload_t));
        
        if (result == ESP_OK) {
            sent_count++;
            LOG_DEBUG("[CACHE] Sent cached message %d/%d (SOC=%d%%, Power=%dW)", 
                     sent_count, total_messages, data.soc, data.power);
        } else {
            LOG_WARN("[CACHE] Failed to send cached message %d/%d: %s", 
                    sent_count + 1, total_messages, esp_err_to_name(result));
        }
        
        // Small delay between sends to avoid overwhelming receiver
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    // Clear cache
    cache_.clear();
    stats_.current_size = 0;
    stats_.total_flushed += sent_count;
    
    xSemaphoreGive(mutex_);
    
    LOG_INFO("[CACHE] ✓ Flush complete: %d/%d messages sent successfully", 
             sent_count, total_messages);
    
    if (sent_count < total_messages) {
        LOG_WARN("[CACHE] %d messages failed to send", total_messages - sent_count);
    }
    
    return sent_count;
}

void DataCache::clear() {
    if (mutex_ == NULL) {
        return;
    }
    
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        size_t cleared = cache_.size();
        cache_.clear();
        stats_.current_size = 0;
        
        LOG_INFO("[CACHE] Cleared %d cached messages", cleared);
        xSemaphoreGive(mutex_);
    }
}
