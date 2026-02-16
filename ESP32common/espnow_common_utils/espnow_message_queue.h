/**
 * @file espnow_message_queue.h
 * @brief Message queue for ESP-NOW pending messages
 * 
 * This is SHARED CODE - used by both transmitter and receiver.
 * Each device creates its own EspNowMessageQueue instance.
 * 
 * CRITICAL: This is NOT a singleton - each device has separate queue
 */

#pragma once

#include <queue>
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
    uint32_t timestamp;                  // When queued
    uint32_t retry_count;                // Number of send attempts
    
    QueuedMessage() : len(0), timestamp(0), retry_count(0) {
        memset(mac, 0, sizeof(mac));
        memset(data, 0, sizeof(data));
    }
    
    QueuedMessage(const uint8_t* dest_mac, const uint8_t* msg_data, size_t msg_len)
        : len(msg_len), timestamp(0), retry_count(0) {
        memcpy(mac, dest_mac, 6);
        memcpy(data, msg_data, msg_len);
    }
};

/**
 * @brief Thread-safe message queue for ESP-NOW
 * 
 * Each device creates its own instance:
 * - Transmitter has TransmitterConnectionManager with own queue
 * - Receiver has ReceiverConnectionManager with own queue
 */
class EspNowMessageQueue {
public:
    /**
     * @brief Constructor - creates mutex
     */
    EspNowMessageQueue();
    
    /**
     * @brief Destructor - destroys mutex
     */
    ~EspNowMessageQueue();
    
    /**
     * @brief Add message to queue
     * @param mac Destination MAC address
     * @param data Message data
     * @param len Data length
     * @return true if queued successfully
     */
    bool push(const uint8_t* mac, const uint8_t* data, size_t len);
    
    /**
     * @brief Get next message from queue (does not remove)
     * @param msg Output message
     * @return true if message available
     */
    bool peek(QueuedMessage& msg);
    
    /**
     * @brief Remove message from queue
     * @return true if message removed
     */
    bool pop();
    
    /**
     * @brief Get current queue size
     * @return Number of messages in queue
     */
    size_t size() const;
    
    /**
     * @brief Check if queue is empty
     * @return true if empty
     */
    bool empty() const;
    
    /**
     * @brief Check if queue is full
     * @return true if at max capacity
     */
    bool full() const;
    
    /**
     * @brief Clear all messages from queue
     */
    void clear();
    
    /**
     * @brief Get maximum queue capacity
     * @return Max number of messages
     */
    size_t capacity() const { return EspNowTiming::MAX_QUEUE_SIZE; }
    
private:
    std::queue<QueuedMessage> queue_;
    SemaphoreHandle_t queue_mutex_;
    const char* log_tag_;
    
    /**
     * @brief Lock queue mutex
     * @return true if locked
     */
    bool lock();
    
    /**
     * @brief Unlock queue mutex
     */
    void unlock();
};
