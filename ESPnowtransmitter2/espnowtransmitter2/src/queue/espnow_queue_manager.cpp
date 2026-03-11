#include "espnow_queue_manager.h"
#include "../config/logging_config.h"
#include <Arduino.h>

EspnowQueueManager& EspnowQueueManager::instance() {
    static EspnowQueueManager instance;
    return instance;
}

EspnowQueueManager::EspnowQueueManager()
    : message_queue_(nullptr)
    , discovery_queue_(nullptr)
    , rx_queue_(nullptr)
    , message_queue_size_(0)
    , discovery_queue_size_(0)
    , rx_queue_size_(0)
    , initialized_(false) {
}

bool EspnowQueueManager::init(size_t message_queue_size, 
                               size_t discovery_queue_size, 
                               size_t rx_queue_size) {
    if (initialized_) {
        LOG_WARN("QUEUE_MGR", "Already initialized");
        return true;
    }
    
    // Create message queue (for RX task processing)
    message_queue_ = xQueueCreate(message_queue_size, sizeof(espnow_queue_msg_t));
    if (message_queue_ == nullptr) {
        LOG_ERROR("QUEUE_MGR", "Failed to create message queue (size=%d)", message_queue_size);
        return false;
    }
    message_queue_size_ = message_queue_size;
    LOG_DEBUG("QUEUE_MGR", "Message queue created (size=%d)", message_queue_size);
    
    // Create discovery queue (for active hopping PROBE/ACK)
    discovery_queue_ = xQueueCreate(discovery_queue_size, sizeof(espnow_queue_msg_t));
    if (discovery_queue_ == nullptr) {
        LOG_ERROR("QUEUE_MGR", "Failed to create discovery queue (size=%d)", discovery_queue_size);
        vQueueDelete(message_queue_);
        message_queue_ = nullptr;
        return false;
    }
    discovery_queue_size_ = discovery_queue_size;
    LOG_DEBUG("QUEUE_MGR", "Discovery queue created (size=%d)", discovery_queue_size);
    
    // Create RX queue (for espnow_transmitter library ISR)
    rx_queue_ = xQueueCreate(rx_queue_size, sizeof(espnow_queue_msg_t));
    if (rx_queue_ == nullptr) {
        LOG_ERROR("QUEUE_MGR", "Failed to create RX queue (size=%d)", rx_queue_size);
        vQueueDelete(message_queue_);
        vQueueDelete(discovery_queue_);
        message_queue_ = nullptr;
        discovery_queue_ = nullptr;
        return false;
    }
    rx_queue_size_ = rx_queue_size;
    LOG_DEBUG("QUEUE_MGR", "RX queue created (size=%d)", rx_queue_size);
    
    initialized_ = true;
    LOG_INFO("QUEUE_MGR", "✓ All ESP-NOW queues initialized");
    return true;
}

// Send operations
bool EspnowQueueManager::send_to_message_queue(const espnow_queue_msg_t& msg, uint32_t timeout_ms) {
    if (!initialized_ || message_queue_ == nullptr) {
        LOG_ERROR("QUEUE_MGR", "Message queue not initialized");
        return false;
    }
    
    TickType_t ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    BaseType_t result = xQueueSend(message_queue_, &msg, ticks);
    
    if (result == pdTRUE) {
        record_send_success(message_stats_);
        update_peak_depth(message_queue_, message_stats_);
        return true;
    } else {
        record_send_failure(message_stats_);
        return false;
    }
}

bool EspnowQueueManager::send_to_discovery_queue(const espnow_queue_msg_t& msg, uint32_t timeout_ms) {
    if (!initialized_ || discovery_queue_ == nullptr) {
        LOG_ERROR("QUEUE_MGR", "Discovery queue not initialized");
        return false;
    }
    
    TickType_t ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    BaseType_t result = xQueueSend(discovery_queue_, &msg, ticks);
    
    if (result == pdTRUE) {
        record_send_success(discovery_stats_);
        update_peak_depth(discovery_queue_, discovery_stats_);
        return true;
    } else {
        record_send_failure(discovery_stats_);
        return false;
    }
}

// Receive operations
bool EspnowQueueManager::receive_from_message_queue(espnow_queue_msg_t& msg, uint32_t timeout_ms) {
    if (!initialized_ || message_queue_ == nullptr) {
        LOG_ERROR("QUEUE_MGR", "Message queue not initialized");
        return false;
    }
    
    TickType_t ticks = (timeout_ms == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    BaseType_t result = xQueueReceive(message_queue_, &msg, ticks);
    
    if (result == pdTRUE) {
        record_receive_success(message_stats_);
        return true;
    } else {
        record_receive_timeout(message_stats_);
        return false;
    }
}

bool EspnowQueueManager::receive_from_discovery_queue(espnow_queue_msg_t& msg, uint32_t timeout_ms) {
    if (!initialized_ || discovery_queue_ == nullptr) {
        LOG_ERROR("QUEUE_MGR", "Discovery queue not initialized");
        return false;
    }
    
    TickType_t ticks = (timeout_ms == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    BaseType_t result = xQueueReceive(discovery_queue_, &msg, ticks);
    
    if (result == pdTRUE) {
        record_receive_success(discovery_stats_);
        return true;
    } else {
        record_receive_timeout(discovery_stats_);
        return false;
    }
}

bool EspnowQueueManager::receive_from_rx_queue(espnow_queue_msg_t& msg, uint32_t timeout_ms) {
    if (!initialized_ || rx_queue_ == nullptr) {
        LOG_ERROR("QUEUE_MGR", "RX queue not initialized");
        return false;
    }
    
    TickType_t ticks = (timeout_ms == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    BaseType_t result = xQueueReceive(rx_queue_, &msg, ticks);
    
    if (result == pdTRUE) {
        record_receive_success(rx_stats_);
        return true;
    } else {
        record_receive_timeout(rx_stats_);
        return false;
    }
}

// Queue monitoring
size_t EspnowQueueManager::get_message_queue_depth() const {
    if (!initialized_ || message_queue_ == nullptr) return 0;
    return uxQueueMessagesWaiting(message_queue_);
}

size_t EspnowQueueManager::get_discovery_queue_depth() const {
    if (!initialized_ || discovery_queue_ == nullptr) return 0;
    return uxQueueMessagesWaiting(discovery_queue_);
}

size_t EspnowQueueManager::get_rx_queue_depth() const {
    if (!initialized_ || rx_queue_ == nullptr) return 0;
    return uxQueueMessagesWaiting(rx_queue_);
}

size_t EspnowQueueManager::get_message_queue_available() const {
    if (!initialized_ || message_queue_ == nullptr) return 0;
    return uxQueueSpacesAvailable(message_queue_);
}

size_t EspnowQueueManager::get_discovery_queue_available() const {
    if (!initialized_ || discovery_queue_ == nullptr) return 0;
    return uxQueueSpacesAvailable(discovery_queue_);
}

size_t EspnowQueueManager::get_rx_queue_available() const {
    if (!initialized_ || rx_queue_ == nullptr) return 0;
    return uxQueueSpacesAvailable(rx_queue_);
}

bool EspnowQueueManager::is_message_queue_full() const {
    if (!initialized_ || message_queue_ == nullptr) return false;
    return uxQueueSpacesAvailable(message_queue_) == 0;
}

bool EspnowQueueManager::is_discovery_queue_full() const {
    if (!initialized_ || discovery_queue_ == nullptr) return false;
    return uxQueueSpacesAvailable(discovery_queue_) == 0;
}

bool EspnowQueueManager::is_rx_queue_full() const {
    if (!initialized_ || rx_queue_ == nullptr) return false;
    return uxQueueSpacesAvailable(rx_queue_) == 0;
}

// Queue flushing
void EspnowQueueManager::flush_message_queue() {
    if (!initialized_ || message_queue_ == nullptr) return;
    
    espnow_queue_msg_t flush_msg;
    int flushed = 0;
    while (xQueueReceive(message_queue_, &flush_msg, 0) == pdTRUE) {
        flushed++;
    }
    
    if (flushed > 0) {
        LOG_DEBUG("QUEUE_MGR", "Flushed %d messages from message queue", flushed);
    }
}

void EspnowQueueManager::flush_discovery_queue() {
    if (!initialized_ || discovery_queue_ == nullptr) return;
    
    espnow_queue_msg_t flush_msg;
    int flushed = 0;
    while (xQueueReceive(discovery_queue_, &flush_msg, 0) == pdTRUE) {
        flushed++;
    }
    
    if (flushed > 0) {
        LOG_DEBUG("QUEUE_MGR", "Flushed %d messages from discovery queue", flushed);
    }
}

void EspnowQueueManager::flush_rx_queue() {
    if (!initialized_ || rx_queue_ == nullptr) return;
    
    espnow_queue_msg_t flush_msg;
    int flushed = 0;
    while (xQueueReceive(rx_queue_, &flush_msg, 0) == pdTRUE) {
        flushed++;
    }
    
    if (flushed > 0) {
        LOG_DEBUG("QUEUE_MGR", "Flushed %d messages from RX queue", flushed);
    }
}

void EspnowQueueManager::flush_all_queues() {
    LOG_DEBUG("QUEUE_MGR", "Flushing all queues...");
    flush_message_queue();
    flush_discovery_queue();
    flush_rx_queue();
}

// Statistics tracking
void EspnowQueueManager::update_peak_depth(QueueHandle_t queue, QueueStatistics& stats) {
    if (queue == nullptr) return;
    
    size_t current_depth = uxQueueMessagesWaiting(queue);
    if (current_depth > stats.peak_depth) {
        stats.peak_depth = current_depth;
    }
    
    // Check for overflow condition
    if (uxQueueSpacesAvailable(queue) == 0) {
        stats.overflow_events++;
    }
}

void EspnowQueueManager::record_send_success(QueueStatistics& stats) {
    stats.total_sent++;
}

void EspnowQueueManager::record_send_failure(QueueStatistics& stats) {
    stats.send_failures++;
}

void EspnowQueueManager::record_receive_success(QueueStatistics& stats) {
    stats.total_received++;
}

void EspnowQueueManager::record_receive_timeout(QueueStatistics& stats) {
    stats.receive_timeouts++;
}

void EspnowQueueManager::reset_statistics() {
    message_stats_.reset();
    discovery_stats_.reset();
    rx_stats_.reset();
    LOG_DEBUG("QUEUE_MGR", "Statistics reset");
}

void EspnowQueueManager::print_diagnostics() const {
    if (!initialized_) {
        LOG_INFO("QUEUE_MGR", "Not initialized");
        return;
    }
    
    LOG_INFO("QUEUE_MGR", "═══ Queue Diagnostics ═══");
    
    // Message queue
    LOG_INFO("QUEUE_MGR", "Message Queue:");
    LOG_INFO("QUEUE_MGR", "  Depth: %d/%d (%.1f%% full)", 
             get_message_queue_depth(), 
             message_queue_size_,
             (get_message_queue_depth() * 100.0f) / message_queue_size_);
    LOG_INFO("QUEUE_MGR", "  Sent: %u | Received: %u | Failures: %u | Timeouts: %u",
             message_stats_.total_sent,
             message_stats_.total_received,
             message_stats_.send_failures,
             message_stats_.receive_timeouts);
    LOG_INFO("QUEUE_MGR", "  Peak Depth: %d | Overflows: %u",
             message_stats_.peak_depth,
             message_stats_.overflow_events);
    
    // Discovery queue
    LOG_INFO("QUEUE_MGR", "Discovery Queue:");
    LOG_INFO("QUEUE_MGR", "  Depth: %d/%d (%.1f%% full)", 
             get_discovery_queue_depth(), 
             discovery_queue_size_,
             (get_discovery_queue_depth() * 100.0f) / discovery_queue_size_);
    LOG_INFO("QUEUE_MGR", "  Sent: %u | Received: %u | Failures: %u | Timeouts: %u",
             discovery_stats_.total_sent,
             discovery_stats_.total_received,
             discovery_stats_.send_failures,
             discovery_stats_.receive_timeouts);
    LOG_INFO("QUEUE_MGR", "  Peak Depth: %d | Overflows: %u",
             discovery_stats_.peak_depth,
             discovery_stats_.overflow_events);
    
    // RX queue
    LOG_INFO("QUEUE_MGR", "RX Queue:");
    LOG_INFO("QUEUE_MGR", "  Depth: %d/%d (%.1f%% full)", 
             get_rx_queue_depth(), 
             rx_queue_size_,
             (get_rx_queue_depth() * 100.0f) / rx_queue_size_);
    LOG_INFO("QUEUE_MGR", "  Sent: %u | Received: %u | Failures: %u | Timeouts: %u",
             rx_stats_.total_sent,
             rx_stats_.total_received,
             rx_stats_.send_failures,
             rx_stats_.receive_timeouts);
    LOG_INFO("QUEUE_MGR", "  Peak Depth: %d | Overflows: %u",
             rx_stats_.peak_depth,
             rx_stats_.overflow_events);
}
