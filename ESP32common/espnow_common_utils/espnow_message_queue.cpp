/**
 * @file espnow_message_queue.cpp
 * @brief Implementation of ESP-NOW message queue
 */

#include "espnow_message_queue.h"
#include <esp_timer.h>

EspNowMessageQueue::EspNowMessageQueue() 
    : queue_mutex_(nullptr), 
      log_tag_("ESPNOW_QUEUE") {
    
    // Create mutex for thread-safe queue access
    queue_mutex_ = xSemaphoreCreateMutex();
    if (queue_mutex_ == nullptr) {
        LOG_ERROR(log_tag_, "Failed to create queue mutex!");
    }
    
    LOG_INFO(log_tag_, "Message queue initialized (capacity: %u)", 
             EspNowTiming::MAX_QUEUE_SIZE);
}

EspNowMessageQueue::~EspNowMessageQueue() {
    // Clear queue
    clear();
    
    // Delete mutex
    if (queue_mutex_ != nullptr) {
        vSemaphoreDelete(queue_mutex_);
        queue_mutex_ = nullptr;
    }
    
    LOG_INFO(log_tag_, "Message queue destroyed");
}

bool EspNowMessageQueue::lock() {
    if (queue_mutex_ != nullptr) {
        return xSemaphoreTake(queue_mutex_, pdMS_TO_TICKS(EspNowTiming::QUEUE_OPERATION_TIMEOUT_MS)) == pdTRUE;
    }
    LOG_WARN(log_tag_, "Queue mutex is null!");
    return false;
}

void EspNowMessageQueue::unlock() {
    if (queue_mutex_ != nullptr) {
        xSemaphoreGive(queue_mutex_);
    }
}

bool EspNowMessageQueue::push(const uint8_t* mac, const uint8_t* data, size_t len) {
    // Validate parameters
    if (mac == nullptr || data == nullptr || len == 0 || len > EspNowTiming::MAX_ESPNOW_PAYLOAD) {
        LOG_ERROR(log_tag_, "Invalid message parameters");
        return false;
    }
    
    if (!lock()) {
        LOG_ERROR(log_tag_, "Failed to lock queue for push");
        return false;
    }
    
    // Check if queue is full
    if (queue_.size() >= EspNowTiming::MAX_QUEUE_SIZE) {
        unlock();
        LOG_WARN(log_tag_, "Queue is full (%u messages), cannot add", queue_.size());
        return false;
    }
    
    // Create and add message
    QueuedMessage msg(mac, data, len);
    msg.timestamp = esp_timer_get_time() / 1000;  // Current time in ms
    msg.retry_count = 0;
    
    queue_.push(msg);
    
    size_t current_size = queue_.size();
    unlock();
    
    LOG_DEBUG(log_tag_, "Message queued (queue size: %u)", current_size);
    return true;
}

bool EspNowMessageQueue::peek(QueuedMessage& msg) {
    if (!lock()) {
        LOG_ERROR(log_tag_, "Failed to lock queue for peek");
        return false;
    }
    
    if (queue_.empty()) {
        unlock();
        return false;
    }
    
    msg = queue_.front();
    unlock();
    return true;
}

bool EspNowMessageQueue::pop() {
    if (!lock()) {
        LOG_ERROR(log_tag_, "Failed to lock queue for pop");
        return false;
    }
    
    if (queue_.empty()) {
        unlock();
        return false;
    }
    
    queue_.pop();
    size_t remaining = queue_.size();
    unlock();
    
    LOG_DEBUG(log_tag_, "Message removed from queue (remaining: %u)", remaining);
    return true;
}

size_t EspNowMessageQueue::size() const {
    // Note: This is not fully thread-safe, but safe enough for size check
    return queue_.size();
}

bool EspNowMessageQueue::empty() const {
    return queue_.empty();
}

bool EspNowMessageQueue::full() const {
    return queue_.size() >= EspNowTiming::MAX_QUEUE_SIZE;
}

void EspNowMessageQueue::clear() {
    if (!lock()) {
        LOG_ERROR(log_tag_, "Failed to lock queue for clear");
        return;
    }
    
    size_t cleared = queue_.size();
    
    // Clear the queue
    while (!queue_.empty()) {
        queue_.pop();
    }
    
    unlock();
    
    if (cleared > 0) {
        LOG_INFO(log_tag_, "Cleared %u messages from queue", cleared);
    }
}
