#include "enhanced_cache.h"
#include "../config/logging_config.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <cstring>

// ═══════════════════════════════════════════════════════════════════════════
// CONSTRUCTOR / DESTRUCTOR
// ═══════════════════════════════════════════════════════════════════════════

EnhancedCache::EnhancedCache()
    : transient_write_idx_(0)
    , transient_read_idx_(0)
    , transient_count_(0)
    , stats_{}
{
    mutex_ = xSemaphoreCreateMutex();
    
    // Initialize state slots
    memset(&state_network_, 0, sizeof(StateEntry));
    memset(&state_mqtt_, 0, sizeof(StateEntry));
    memset(&state_battery_, 0, sizeof(StateEntry));
    
    state_network_.type = CacheDataType::STATE_NETWORK;
    state_mqtt_.type = CacheDataType::STATE_MQTT;
    state_battery_.type = CacheDataType::STATE_BATTERY;
    
    LOG_INFO("[CACHE] Enhanced cache initialized (Transient: %d, State: 3 slots)", 
             TRANSIENT_QUEUE_SIZE);
}

EnhancedCache::~EnhancedCache() {
    if (mutex_) {
        vSemaphoreDelete(mutex_);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// TRANSIENT DATA OPERATIONS
// ═══════════════════════════════════════════════════════════════════════════

bool EnhancedCache::add_transient(const espnow_payload_t& data, uint32_t timestamp, uint32_t seq) {
    // Try to acquire mutex with timeout (non-blocking for control code)
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        stats_.mutex_timeouts++;
        LOG_WARN("[CACHE] Mutex timeout - data dropped (control code priority)");
        return false;
    }
    
    // Check for overflow
    if (transient_count_ >= TRANSIENT_QUEUE_SIZE) {
        stats_.overflow_events++;
        stats_.transient_dropped++;
        
        LOG_WARN("[CACHE] Transient queue full (%d/%d) - oldest entry dropped",
                 transient_count_, TRANSIENT_QUEUE_SIZE);
        
        // Drop oldest entry (FIFO overflow)
        transient_read_idx_ = (transient_read_idx_ + 1) % TRANSIENT_QUEUE_SIZE;
        transient_count_--;
    }
    
    // Add new entry
    TransientEntry& entry = transient_queue_[transient_write_idx_];
    entry.data = data;
    entry.seq = seq;
    entry.timestamp = timestamp;
    entry.sent = false;
    entry.acked = false;
    entry.retry_count = 0;
    
    transient_write_idx_ = (transient_write_idx_ + 1) % TRANSIENT_QUEUE_SIZE;
    transient_count_++;
    
    // Update stats
    stats_.transient_added++;
    if (transient_count_ > stats_.transient_max_reached) {
        stats_.transient_max_reached = transient_count_;
    }
    stats_.transient_current = transient_count_;
    
    xSemaphoreGive(mutex_);
    return true;
}

// Simplified add_transient (auto-generates timestamp and seq)
bool EnhancedCache::add_transient(const espnow_payload_t& data) {
    static uint32_t auto_seq = 1;
    return add_transient(data, millis(), auto_seq++);
}

TransientEntry* EnhancedCache::peek_next_transient() {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return nullptr;
    }
    
    TransientEntry* result = nullptr;
    
    // Find first unsent entry
    for (size_t i = 0; i < transient_count_; i++) {
        size_t idx = (transient_read_idx_ + i) % TRANSIENT_QUEUE_SIZE;
        if (!transient_queue_[idx].sent) {
            result = &transient_queue_[idx];
            break;
        }
    }
    
    xSemaphoreGive(mutex_);
    return result;
}

// Const-safe peek_next_transient (output parameter)
bool EnhancedCache::peek_next_transient(TransientEntry& entry) const {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return false;
    }
    
    bool found = false;
    
    // Find first unsent entry
    for (size_t i = 0; i < transient_count_; i++) {
        size_t idx = (transient_read_idx_ + i) % TRANSIENT_QUEUE_SIZE;
        if (!transient_queue_[idx].sent) {
            entry = transient_queue_[idx];
            found = true;
            break;
        }
    }
    
    xSemaphoreGive(mutex_);
    return found;
}

void EnhancedCache::mark_transient_sent(uint32_t seq) {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return;
    }
    
    for (size_t i = 0; i < transient_count_; i++) {
        size_t idx = (transient_read_idx_ + i) % TRANSIENT_QUEUE_SIZE;
        if (transient_queue_[idx].seq == seq) {
            transient_queue_[idx].sent = true;
            stats_.transient_sent++;
            break;
        }
    }
    
    xSemaphoreGive(mutex_);
}

void EnhancedCache::mark_transient_acked(uint32_t seq) {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return;
    }
    
    for (size_t i = 0; i < transient_count_; i++) {
        size_t idx = (transient_read_idx_ + i) % TRANSIENT_QUEUE_SIZE;
        if (transient_queue_[idx].seq == seq) {
            transient_queue_[idx].acked = true;
            stats_.transient_acked++;
            
            // Calculate cache duration
            uint32_t duration = millis() - transient_queue_[idx].timestamp;
            if (duration > stats_.max_cache_duration_ms) {
                stats_.max_cache_duration_ms = duration;
            }
            break;
        }
    }
    
    xSemaphoreGive(mutex_);
}

size_t EnhancedCache::cleanup_acked_transient() {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return 0;
    }
    
    size_t removed = 0;
    
    // Remove from front of queue (FIFO)
    while (transient_count_ > 0) {
        if (transient_queue_[transient_read_idx_].acked) {
            // Remove this entry
            transient_read_idx_ = (transient_read_idx_ + 1) % TRANSIENT_QUEUE_SIZE;
            transient_count_--;
            removed++;
        } else {
            // Stop at first un-acked entry
            break;
        }
    }
    
    stats_.transient_current = transient_count_;
    
    xSemaphoreGive(mutex_);
    
    if (removed > 0) {
        LOG_DEBUG("[CACHE] Cleaned up %d acked transient entries", removed);
    }
    
    return removed;
}

size_t EnhancedCache::transient_unsent_count() const {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return 0;
    }
    
    size_t count = 0;
    for (size_t i = 0; i < transient_count_; i++) {
        size_t idx = (transient_read_idx_ + i) % TRANSIENT_QUEUE_SIZE;
        if (!transient_queue_[idx].sent) {
            count++;
        }
    }
    
    xSemaphoreGive(mutex_);
    return count;
}

size_t EnhancedCache::transient_unacked_count() const {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return 0;
    }
    
    size_t count = 0;
    for (size_t i = 0; i < transient_count_; i++) {
        size_t idx = (transient_read_idx_ + i) % TRANSIENT_QUEUE_SIZE;
        if (transient_queue_[idx].sent && !transient_queue_[idx].acked) {
            count++;
        }
    }
    
    xSemaphoreGive(mutex_);
    return count;
}

bool EnhancedCache::has_unsent_transient() const {
    return transient_unsent_count() > 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// STATE DATA OPERATIONS
// ═══════════════════════════════════════════════════════════════════════════

void EnhancedCache::update_state(CacheDataType type, const StateEntry& entry) {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        stats_.mutex_timeouts++;
        return;
    }
    
    StateEntry* slot = get_state_slot(type);
    if (slot) {
        // Update state (NEVER delete old version)
        *slot = entry;
        slot->type = type;
        slot->is_latest = true;
        slot->sent = false;
        slot->acked = false;
        
        stats_.state_updates++;
        
        LOG_INFO("[CACHE] State updated: type=%d, version=%u",
                 static_cast<int>(type), entry.version);
    }
    
    xSemaphoreGive(mutex_);
}

const StateEntry* EnhancedCache::get_state(CacheDataType type) const {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return nullptr;
    }
    
    const StateEntry* result = get_state_slot(type);
    
    xSemaphoreGive(mutex_);
    return result;
}

// Output parameter version of get_state
bool EnhancedCache::get_state(CacheDataType type, StateEntry& entry) const {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return false;
    }
    
    const StateEntry* slot = get_state_slot(type);
    if (slot && slot->version > 0) {
        entry = *slot;
        xSemaphoreGive(mutex_);
        return true;
    }
    
    xSemaphoreGive(mutex_);
    return false;
}

void EnhancedCache::mark_state_sent(CacheDataType type) {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return;
    }
    
    StateEntry* slot = get_state_slot(type);
    if (slot) {
        slot->sent = true;
        stats_.state_sent++;
    }
    
    xSemaphoreGive(mutex_);
}

void EnhancedCache::mark_state_acked(CacheDataType type) {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return;
    }
    
    StateEntry* slot = get_state_slot(type);
    if (slot) {
        slot->acked = true;
        stats_.state_acked++;
        // NOTE: State data is NEVER removed from cache after ACK
    }
    
    xSemaphoreGive(mutex_);
}

bool EnhancedCache::has_unsent_state(CacheDataType type) const {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return false;
    }
    
    const StateEntry* slot = get_state_slot(type);
    bool result = (slot && !slot->sent);
    
    xSemaphoreGive(mutex_);
    return result;
}

// Check if ANY state type has unsent changes
bool EnhancedCache::has_unsent_state() const {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return false;
    }
    
    bool has_unsent = false;
    
    // Check all state types
    if ((state_network_.version > 0 && !state_network_.sent) ||
        (state_mqtt_.version > 0 && !state_mqtt_.sent) ||
        (state_battery_.version > 0 && !state_battery_.sent)) {
        has_unsent = true;
    }
    
    xSemaphoreGive(mutex_);
    return has_unsent;
}

// ═══════════════════════════════════════════════════════════════════════════
// NVS PERSISTENCE (TX-ONLY)
// ═══════════════════════════════════════════════════════════════════════════

bool EnhancedCache::persist_state_to_nvs(CacheDataType type) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("cache_state", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        LOG_ERROR("[CACHE] Failed to open NVS for write: %s", esp_err_to_name(err));
        return false;
    }
    
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        nvs_close(nvs_handle);
        return false;
    }
    
    const StateEntry* slot = get_state_slot(type);
    if (slot) {
        const char* key = get_nvs_key(type);
        err = nvs_set_blob(nvs_handle, key, slot, sizeof(StateEntry));
        
        if (err == ESP_OK) {
            err = nvs_commit(nvs_handle);
            if (err == ESP_OK) {
                LOG_INFO("[CACHE] State persisted to NVS: type=%d, version=%u",
                         static_cast<int>(type), slot->version);
            }
        }
    }
    
    xSemaphoreGive(mutex_);
    nvs_close(nvs_handle);
    
    return (err == ESP_OK);
}

bool EnhancedCache::restore_state_from_nvs(CacheDataType type) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("cache_state", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        // Not an error - NVS may not exist yet
        return false;
    }
    
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        nvs_close(nvs_handle);
        return false;
    }
    
    StateEntry* slot = get_state_slot(type);
    if (slot) {
        const char* key = get_nvs_key(type);
        size_t required_size = sizeof(StateEntry);
        
        err = nvs_get_blob(nvs_handle, key, slot, &required_size);
        if (err == ESP_OK) {
            LOG_INFO("[CACHE] State restored from NVS: type=%d, version=%u",
                     static_cast<int>(type), slot->version);
        }
    }
    
    xSemaphoreGive(mutex_);
    nvs_close(nvs_handle);
    
    return (err == ESP_OK);
}

void EnhancedCache::restore_all_from_nvs() {
    LOG_INFO("[CACHE] Restoring all state from NVS...");
    
    bool network_restored = restore_state_from_nvs(CacheDataType::STATE_NETWORK);
    bool mqtt_restored = restore_state_from_nvs(CacheDataType::STATE_MQTT);
    bool battery_restored = restore_state_from_nvs(CacheDataType::STATE_BATTERY);
    
    LOG_INFO("[CACHE] NVS restore complete: Network=%s, MQTT=%s, Battery=%s",
             network_restored ? "OK" : "NONE",
             mqtt_restored ? "OK" : "NONE",
             battery_restored ? "OK" : "NONE");
}

// ═══════════════════════════════════════════════════════════════════════════
// STATISTICS
// ═══════════════════════════════════════════════════════════════════════════

void EnhancedCache::log_stats() const {
    LOG_INFO("[CACHE] ═══ Cache Statistics ═══");
    LOG_INFO("[CACHE] Transient Queue:");
    LOG_INFO("[CACHE]   Current: %d/%d (%.1f%% full)",
             stats_.transient_current, TRANSIENT_QUEUE_SIZE,
             (stats_.transient_current * 100.0f) / TRANSIENT_QUEUE_SIZE);
    LOG_INFO("[CACHE]   Added: %u, Sent: %u, Acked: %u, Dropped: %u",
             stats_.transient_added, stats_.transient_sent,
             stats_.transient_acked, stats_.transient_dropped);
    LOG_INFO("[CACHE]   Max reached: %d", stats_.transient_max_reached);
    
    LOG_INFO("[CACHE] State Data:");
    LOG_INFO("[CACHE]   Updates: %u, Sent: %u, Acked: %u, Conflicts: %u",
             stats_.state_updates, stats_.state_sent,
             stats_.state_acked, stats_.state_conflicts);
    
    LOG_INFO("[CACHE] Timing:");
    LOG_INFO("[CACHE]   Max cache duration: %ums", stats_.max_cache_duration_ms);
    
    LOG_INFO("[CACHE] Errors:");
    LOG_INFO("[CACHE]   Mutex timeouts: %u, Overflows: %u",
             stats_.mutex_timeouts, stats_.overflow_events);
}

void EnhancedCache::reset_stats() {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return;
    }
    
    stats_ = {};
    stats_.transient_current = transient_count_;
    
    xSemaphoreGive(mutex_);
    LOG_INFO("[CACHE] Statistics reset");
}

// ═══════════════════════════════════════════════════════════════════════════
// HELPERS
// ═══════════════════════════════════════════════════════════════════════════

StateEntry* EnhancedCache::get_state_slot(CacheDataType type) {
    switch (type) {
        case CacheDataType::STATE_NETWORK:
            return &state_network_;
        case CacheDataType::STATE_MQTT:
            return &state_mqtt_;
        case CacheDataType::STATE_BATTERY:
            return &state_battery_;
        default:
            return nullptr;
    }
}

const StateEntry* EnhancedCache::get_state_slot(CacheDataType type) const {
    switch (type) {
        case CacheDataType::STATE_NETWORK:
            return &state_network_;
        case CacheDataType::STATE_MQTT:
            return &state_mqtt_;
        case CacheDataType::STATE_BATTERY:
            return &state_battery_;
        default:
            return nullptr;
    }
}

const char* EnhancedCache::get_nvs_key(CacheDataType type) const {
    switch (type) {
        case CacheDataType::STATE_NETWORK:
            return "net_cfg";
        case CacheDataType::STATE_MQTT:
            return "mqtt_cfg";
        case CacheDataType::STATE_BATTERY:
            return "bat_cfg";
        default:
            return "unknown";
    }
}
