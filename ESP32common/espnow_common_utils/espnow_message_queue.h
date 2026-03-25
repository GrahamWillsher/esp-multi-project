/**
 * @file espnow_message_queue.h
 * @brief Message queue for ESP-NOW pending messages
 * 
 * HARDENING PHASE A (2026-03-25): Rewritten as fixed-capacity ring buffer
 * 
 * PREVIOUS DESIGN (dynamic deque):
 * - Used std::queue with unbounded growth
 * - Mixed synchronization (per-operation mutex + condition variables)
 * - Fragmentation risk under memory pressure
 * 
 * NEW DESIGN (fixed-capacity ring buffer):
 * - Preallocated fixed-size storage (deterministic memory)
 * - Single-lock discipline for all operations
 * - Explicit overflow policy (DROP_OLDEST or REJECT)
 * - Built-in metrics (push_failures, overflow_count, max_depth_seen)
 * 
 * This is SHARED CODE - used by both transmitter and receiver.
 * Each device creates its own EspNowMessageQueue instance.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "espnow_timing_config.h"

/**
 * @brief Queued ESP-NOW message
 */
struct QueuedMessage {
    uint8_t mac[6];                      // Destination MAC
    uint8_t data[250];                   // Message data
    size_t len;                          // Data length
    uint32_t timestamp;                  // When queued (ms since boot)
    uint32_t retry_count;                // Number of send attempts
    
    QueuedMessage() : len(0), timestamp(0), retry_count(0) {
        memset(mac, 0, sizeof(mac));
        memset(data, 0, sizeof(data));
    }
    
    QueuedMessage(const uint8_t* dest_mac, const uint8_t* msg_data, size_t msg_len)
        : len(msg_len), timestamp(0), retry_count(0) {
        if (dest_mac && msg_data && msg_len > 0) {
            memcpy(mac, dest_mac, 6);
            memcpy(data, msg_data, msg_len);
        }
    }
};

/**
 * @brief Overflow policy for ring buffer when full
 */
enum class QueueOverflowPolicy {
    DROP_OLDEST,   // Remove oldest message to make room for new message
    REJECT         // Reject new message if queue full
};

/**
 * @brief Queue metrics for monitoring and diagnostics
 */
struct QueueMetrics {
    uint32_t push_failures;      // Total failed push operations
    uint32_t overflow_count;     // Total overflows (messages dropped/rejected)
    size_t max_depth_seen;       // Maximum queue depth observed
};

/**
 * @brief Thread-safe message queue for ESP-NOW using fixed-capacity ring buffer
 * 
 * Provides deterministic memory footprint and predictable behavior.
 * Suitable for long-running embedded systems.
 * 
 * Usage:
 *   EspNowMessageQueue queue(EspNowTiming::MAX_QUEUE_SIZE, QueueOverflowPolicy::DROP_OLDEST);
 *   queue.push(mac, data, len);
 *   if (queue.peek(msg)) {
 *       // Process message
 *       queue.pop();
 *   }
 */
class EspNowMessageQueue {
public:
    /**
     * @brief Constructor
     * @param capacity Maximum number of messages to store
     * @param overflow_policy What to do when queue is full
     */
    EspNowMessageQueue(size_t capacity = EspNowTiming::MAX_QUEUE_SIZE,
                      QueueOverflowPolicy overflow_policy = QueueOverflowPolicy::DROP_OLDEST);
    
    /**
     * @brief Destructor - frees preallocated buffer and mutex
     */
    ~EspNowMessageQueue();
    
    /**
     * @brief Add message to queue
     * 
     * Applies overflow policy if queue is full.
     * 
     * @param mac Destination MAC address
     * @param data Message data
     * @param len Data length (must be <= 250)
     * @return true if message added (or overflow policy handled it), false on validation error
     */
    bool push(const uint8_t* mac, const uint8_t* data, size_t len);
    
    /**
     * @brief Get next message from queue (does not remove)
     * @param msg Output message
     * @return true if message available
     */
    bool peek(QueuedMessage& msg);
    
    /**
     * @brief Remove front message from queue
     * @return true if message removed, false if empty
     */
    bool pop();
    
    /**
     * @brief Get current queue depth
     * @return Number of messages currently in queue
     */
    size_t size() const;
    
    /**
     * @brief Check if queue is empty
     * @return true if queue contains no messages
     */
    bool empty() const;
    
    /**
     * @brief Check if queue is full
     * @return true if queue at maximum capacity
     */
    bool full() const;
    
    /**
     * @brief Get queue capacity (maximum size)
     * @return Maximum number of messages this queue can hold
     */
    size_t capacity() const { return capacity_; }
    
    /**
     * @brief Clear all messages from queue
     */
    void clear();
    
    /**
     * @brief Get queue metrics (diagnostics)
     * @return Current metrics snapshot
     */
    QueueMetrics get_metrics() const;
    
    /**
     * @brief Reset metrics counters
     */
    void reset_metrics();

private:
    // Ring buffer storage
    QueuedMessage* buffer_;              // Preallocated ring buffer
    size_t capacity_;                    // Buffer capacity (fixed at creation)
    size_t head_;                        // Index of next write position
    size_t tail_;                        // Index of next read position
    size_t count_;                       // Current number of messages
    QueueOverflowPolicy overflow_policy_; // Overflow handling strategy
    
    // Synchronization
    SemaphoreHandle_t buffer_mutex_;     // Single lock for all operations
    
    // Metrics
    QueueMetrics metrics_;
    
    // Helpers
    bool lock_queue(uint32_t timeout_ms = EspNowTiming::QUEUE_OPERATION_TIMEOUT_MS);
    void unlock_queue();
    void handle_overflow();
};

