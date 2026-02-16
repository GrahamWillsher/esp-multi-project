#ifndef RX_HEARTBEAT_MANAGER_H
#define RX_HEARTBEAT_MANAGER_H

#include <Arduino.h>
#include <esp_now.h>
#include <espnow_common.h>
#include <connection_manager.h>

/**
 * Heartbeat Manager - Receiver Side
 * 
 * Responsibilities:
 * - Receive heartbeat from transmitter
 * - Send ACK immediately
 * - Track last heartbeat time
 * - Detect connection loss (90s timeout)
 */
class RxHeartbeatManager {
public:
    static RxHeartbeatManager& instance() {
        static RxHeartbeatManager inst;
        return inst;
    }
    
    // Initialize heartbeat manager
    void init();
    
    // Call periodically from task (checks for timeout)
    void tick();
    
    // Handle heartbeat from transmitter
    void on_heartbeat(const heartbeat_t* hb, const uint8_t* mac);
    
    // Reset state (e.g., on connection lost)
    void reset();
    
    // Called when connection is established to reset timeout counter
    void on_connection_established();
    
    // Get statistics
    uint32_t get_received_count() const { return m_heartbeats_received; }
    uint32_t get_sent_ack_count() const { return m_acks_sent; }
    uint32_t get_last_seq() const { return m_last_heartbeat_seq; }
    uint32_t get_time_since_last() const { return millis() - m_last_rx_time_ms; }
    
private:
    RxHeartbeatManager() = default;
    RxHeartbeatManager(const RxHeartbeatManager&) = delete;
    RxHeartbeatManager& operator=(const RxHeartbeatManager&) = delete;
    
    void send_ack(uint32_t ack_seq, const uint8_t* mac);
    
    static constexpr uint32_t HEARTBEAT_TIMEOUT_MS = 90000;  // 90 seconds
    
    uint32_t m_last_heartbeat_seq = 0;       // Last received sequence
    uint32_t m_last_rx_time_ms = 0;          // Timestamp of last heartbeat
    uint32_t m_heartbeats_received = 0;      // Total heartbeats received
    uint32_t m_acks_sent = 0;                // Total ACKs sent
    bool m_initialized = false;
};

#endif // RX_HEARTBEAT_MANAGER_H
