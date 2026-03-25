/**
 * @file espnow_message_queue.cpp
 * @brief Implementation of fixed-capacity ring buffer for ESP-NOW messages
 * 
 * HARDENING PHASE A (2026-03-25): Complete rewrite from std::queue to ring buffer
 * 
 * Previous design problems fixed:
 * - Dynamic allocation removed → Fixed buffer allocated once at construction
 * - Unbounded growth eliminated → Deterministic capacity
 * - Mixed synchronization fixed → Single-lock discipline
 * - No metrics → Added overflow tracking and max-depth monitoring
 */

#include "espnow_message_queue.h"
#include <esp_timer.h>
#include <cstdlib>

// ============================================================================
// Constructor and Destructor
// ============================================================================

EspNowMessageQueue::EspNowMessageQueue(size_t capacity, QueueOverflowPolicy overflow_policy)
    : capacity_(capacity > 0 ? capacity : EspNowTiming::MAX_QUEUE_SIZE),
      head_(0),
      tail_(0),
      count_(0),
      overflow_policy_(overflow_policy),
      buffer_mutex_(nullptr) {
    
    
    // Allocate ring buffer (use malloc for compatibility)
    buffer_ = (QueuedMessage*)malloc(sizeof(QueuedMessage) * capacity_);
    if (buffer_ == nullptr) {
        LOG_ERROR("ESPNOW_QUEUE", "Failed to allocate ring buffer (%u messages)", capacity_);
        capacity_ = 0;
        return;
    }
    
    // Initialize buffer (zero-initialize for safety)
    memset(buffer_, 0, sizeof(QueuedMessage) * capacity_);
    
    // Create mutex
    buffer_mutex_ = xSemaphoreCreateMutex();
    if (buffer_mutex_ == nullptr) {
        LOG_ERROR("ESPNOW_QUEUE", "Failed to create queue mutex");
        free(buffer_);
        buffer_ = nullptr;
        return;
    }
    // Initialize metrics
    metrics_.push_failures = 0;
    metrics_.overflow_count = 0;
    metrics_.max_depth_seen = 0;
    
    LOG_INFO("ESPNOW_QUEUE", "Ring buffer queue initialized (capacity: %u, policy: %s)",
             capacity_,
             overflow_policy_ == QueueOverflowPolicy::DROP_OLDEST ? "DROP_OLDEST" : "REJECT");
}

EspNowMessageQueue::~EspNowMessageQueue() {
    // Lock and clear
    if (lock_queue()) {
        count_ = 0;
        head_ = 0;
        tail_ = 0;
        unlock_queue();
    }
    
    // Free buffer
    if (buffer_ != nullptr) {
            free(buffer_);
        buffer_ = nullptr;
    }
    
    // Destroy mutex
    if (buffer_mutex_ != nullptr) {
        vSemaphoreDelete(buffer_mutex_);
        buffer_mutex_ = nullptr;
    }
    
    LOG_INFO("ESPNOW_QUEUE", "Ring buffer queue destroyed");
}

// ============================================================================
// Synchronization Helpers
// ============================================================================

bool EspNowMessageQueue::lock_queue(uint32_t timeout_ms) {
    if (buffer_mutex_ == nullptr) {
        LOG_ERROR("ESPNOW_QUEUE", "Queue mutex is null!");
        return false;
    }
    
    BaseType_t result = xSemaphoreTake(buffer_mutex_, pdMS_TO_TICKS(timeout_ms));
    if (result != pdTRUE) {
        LOG_WARN("ESPNOW_QUEUE", "Failed to acquire queue lock (timeout: %ums)", timeout_ms);
        return false;
    }
    
    return true;
}

void EspNowMessageQueue::unlock_queue() {
    if (buffer_mutex_ != nullptr) {
        xSemaphoreGive(buffer_mutex_);
    }
}

// ============================================================================
// Core Queue Operations
// ============================================================================

bool EspNowMessageQueue::push(const uint8_t* mac, const uint8_t* data, size_t len) {
    // Validate parameters
    if (mac == nullptr || data == nullptr || len == 0 || len > 250) {
        LOG_ERROR("ESPNOW_QUEUE", "Invalid push parameters (mac:%p, data:%p, len:%zu)",
                 mac, data, len);
        return false;
    }
    
    if (!lock_queue()) {
        LOG_ERROR("ESPNOW_QUEUE", "Failed to lock queue for push");
        metrics_.push_failures++;
        return false;
    }
    
    // Check if buffer is initialized
    if (buffer_ == nullptr || capacity_ == 0) {
        LOG_ERROR("ESPNOW_QUEUE", "Queue buffer not initialized");
        unlock_queue();
        metrics_.push_failures++;
        return false;
    }
    
    // Handle overflow
    if (count_ >= capacity_) {
        handle_overflow();
        // After overflow handling, if still full, fail or queue was cleared
        if (count_ >= capacity_) {
            LOG_WARN("ESPNOW_QUEUE", "Queue full and overflow not handled");
            unlock_queue();
            metrics_.push_failures++;
            return false;
        }
    }
    
    // Add message to buffer at head position
    buffer_[head_] = QueuedMessage(mac, data, len);
    buffer_[head_].timestamp = esp_timer_get_time() / 1000;  // ms since boot
    buffer_[head_].retry_count = 0;
    
    // Advance head with wraparound
    head_ = (head_ + 1) % capacity_;
    count_++;
    
    // Track max depth
    if (count_ > metrics_.max_depth_seen) {
        metrics_.max_depth_seen = count_;
    }
    
    LOG_DEBUG("ESPNOW_QUEUE", "Message pushed (size: %u/%u)", count_, capacity_);
    
    unlock_queue();
    return true;
}

bool EspNowMessageQueue::peek(QueuedMessage& msg) {
    if (!lock_queue()) {
        LOG_ERROR("ESPNOW_QUEUE", "Failed to lock queue for peek");
        return false;
    }
    
    if (count_ == 0) {
        unlock_queue();
        return false;
    }
    
    // Copy message at tail
    msg = buffer_[tail_];
    
    unlock_queue();
    return true;
}

bool EspNowMessageQueue::pop() {
    if (!lock_queue()) {
        LOG_ERROR("ESPNOW_QUEUE", "Failed to lock queue for pop");
        return false;
    }
    
    if (count_ == 0) {
        unlock_queue();
        return false;
    }
    
    // Clear message at tail (for safety)
    memset(&buffer_[tail_], 0, sizeof(QueuedMessage));
    
    // Advance tail with wraparound
    tail_ = (tail_ + 1) % capacity_;
    count_--;
    
    LOG_DEBUG("ESPNOW_QUEUE", "Message popped (remaining: %u/%u)", count_, capacity_);
    
    unlock_queue();
    return true;
}

// ============================================================================
// Queue Status
// ============================================================================

size_t EspNowMessageQueue::size() const {
    // Not fully synchronized, but safe for read-only check
    return count_;
}

bool EspNowMessageQueue::empty() const {
    return count_ == 0;
}

bool EspNowMessageQueue::full() const {
    return count_ >= capacity_;
}

QueueMetrics EspNowMessageQueue::get_metrics() const {
    // Note: Not synchronized; snapshot may be slightly stale
    return metrics_;
}

void EspNowMessageQueue::reset_metrics() {
    if (!lock_queue()) {
        return;
    }
    
    metrics_.push_failures = 0;
    metrics_.overflow_count = 0;
    metrics_.max_depth_seen = count_;  // Current depth becomes new max reference
    
    LOG_INFO("ESPNOW_QUEUE", "Metrics reset");
    
    unlock_queue();
}

// ============================================================================
// Queue Management
// ============================================================================

void EspNowMessageQueue::clear() {
    if (!lock_queue()) {
        LOG_ERROR("ESPNOW_QUEUE", "Failed to lock queue for clear");
        return;
    }
    
    size_t cleared = count_;
    
    // Reset ring buffer pointers
    head_ = 0;
    tail_ = 0;
    count_ = 0;
    
    unlock_queue();
    
    if (cleared > 0) {
        LOG_INFO("ESPNOW_QUEUE", "Cleared %u messages from queue", cleared);
    }
}

void EspNowMessageQueue::handle_overflow() {
    // Called when buffer is full and we need to make room
    // Assumes lock is already held
    
    if (overflow_policy_ == QueueOverflowPolicy::DROP_OLDEST) {
        // Remove oldest (tail) message to make room
        tail_ = (tail_ + 1) % capacity_;
        count_--;
        metrics_.overflow_count++;
        LOG_WARN("ESPNOW_QUEUE", "Queue full - dropped oldest message (policy: DROP_OLDEST)");
    } else {
        // REJECT policy - new message will not be added
        metrics_.overflow_count++;
        LOG_WARN("ESPNOW_QUEUE", "Queue full - new message rejected (policy: REJECT)");
    }
}
