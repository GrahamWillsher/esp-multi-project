#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp32common/espnow/common.h>
#include <stdint.h>

/**
 * EspnowQueueManager - Singleton encapsulation for ESP-NOW message queues
 * 
 * Encapsulates three separate queues:
 * 1. Message Queue: Application messages (processed by RX task)
 * 2. Discovery Queue: PROBE/ACK messages (consumed by active hopping task)
 * 3. RX Queue: Raw ESP-NOW frames (filled by ISR callback in espnow_transmitter library)
 * 
 * Benefits:
 * - Centralized queue management and initialization
 * - Queue depth monitoring and overflow detection
 * - Statistics tracking for debugging and telemetry
 * - Consistent API across all queue operations
 * - Single point of failure for diagnostics
 */
class EspnowQueueManager {
public:
    static EspnowQueueManager& instance();
    
    // Initialization (must be called before any queue operations)
    bool init(size_t message_queue_size = 10, 
              size_t discovery_queue_size = 20, 
              size_t rx_queue_size = 30);
    
    // Queue handles for external access (library and ISR need direct handles)
    QueueHandle_t get_message_queue() const { return message_queue_; }
    QueueHandle_t get_discovery_queue() const { return discovery_queue_; }
    QueueHandle_t get_rx_queue() const { return rx_queue_; }
    
    // Safe send operations (with statistics tracking)
    bool send_to_message_queue(const espnow_queue_msg_t& msg, uint32_t timeout_ms = 0);
    bool send_to_discovery_queue(const espnow_queue_msg_t& msg, uint32_t timeout_ms = 0);
    
    // Safe receive operations (with statistics tracking)
    bool receive_from_message_queue(espnow_queue_msg_t& msg, uint32_t timeout_ms);
    bool receive_from_discovery_queue(espnow_queue_msg_t& msg, uint32_t timeout_ms);
    bool receive_from_rx_queue(espnow_queue_msg_t& msg, uint32_t timeout_ms);
    
    // Queue monitoring
    size_t get_message_queue_depth() const;
    size_t get_discovery_queue_depth() const;
    size_t get_rx_queue_depth() const;
    
    size_t get_message_queue_available() const;
    size_t get_discovery_queue_available() const;
    size_t get_rx_queue_available() const;
    
    bool is_message_queue_full() const;
    bool is_discovery_queue_full() const;
    bool is_rx_queue_full() const;
    
    // Queue flushing (clear all pending messages)
    void flush_message_queue();
    void flush_discovery_queue();
    void flush_rx_queue();
    void flush_all_queues();
    
    // Statistics
    struct QueueStatistics {
        uint32_t total_sent{0};
        uint32_t total_received{0};
        uint32_t send_failures{0};         // Failed to send (queue full or error)
        uint32_t receive_timeouts{0};      // Receive timed out
        size_t peak_depth{0};              // Maximum queue depth ever reached
        uint32_t overflow_events{0};       // Times queue became full
        
        void reset() {
            total_sent = 0;
            total_received = 0;
            send_failures = 0;
            receive_timeouts = 0;
            peak_depth = 0;
            overflow_events = 0;
        }
    };
    
    QueueStatistics get_message_queue_stats() const { return message_stats_; }
    QueueStatistics get_discovery_queue_stats() const { return discovery_stats_; }
    QueueStatistics get_rx_queue_stats() const { return rx_stats_; }
    
    void reset_statistics();
    
    // Diagnostics - get textual summary of all queues
    void print_diagnostics() const;
    
private:
    EspnowQueueManager();
    ~EspnowQueueManager() = default;
    
    // Prevent copying
    EspnowQueueManager(const EspnowQueueManager&) = delete;
    EspnowQueueManager& operator=(const EspnowQueueManager&) = delete;
    
    // Update statistics
    void update_peak_depth(QueueHandle_t queue, QueueStatistics& stats);
    void record_send_success(QueueStatistics& stats);
    void record_send_failure(QueueStatistics& stats);
    void record_receive_success(QueueStatistics& stats);
    void record_receive_timeout(QueueStatistics& stats);
    
    // Queue handles
    QueueHandle_t message_queue_{nullptr};
    QueueHandle_t discovery_queue_{nullptr};
    QueueHandle_t rx_queue_{nullptr};
    
    // Queue sizes (stored for available space calculation)
    size_t message_queue_size_{0};
    size_t discovery_queue_size_{0};
    size_t rx_queue_size_{0};
    
    // Statistics
    QueueStatistics message_stats_;
    QueueStatistics discovery_stats_;
    QueueStatistics rx_stats_;
    
    bool initialized_{false};
};
