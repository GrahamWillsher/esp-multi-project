#ifndef HEARTBEAT_MANAGER_H
#define HEARTBEAT_MANAGER_H

#include <Arduino.h>
#include <esp_now.h>
#include <espnow_common.h>
#include <connection_manager.h>

/**
 * Heartbeat Manager - Transmitter Side
 * 
 * Responsibilities:
 * - Send heartbeat every 10s when CONNECTED
 * - Track sequence numbers
 * - Monitor ACKs from receiver
 * - Detect connection loss (3 consecutive unacked heartbeats)
 */
class HeartbeatManager {
public:
    static HeartbeatManager& instance() {
        static HeartbeatManager inst;
        return inst;
    }
    
    // Initialize heartbeat manager
    void init();
    
    // Call periodically from task (checks if it's time to send)
    void tick();
    
    // Handle heartbeat ACK from receiver
    void on_heartbeat_ack(const heartbeat_ack_t* ack);
    
    // Reset state (e.g., on connection lost)
    void reset();
    
    // Get statistics
    uint32_t get_sent_count() const { return m_heartbeat_seq; }
    uint32_t get_acked_count() const { return m_last_ack_seq; }
    uint32_t get_unacked_count() const { return m_heartbeat_seq - m_last_ack_seq; }
    
private:
    HeartbeatManager() = default;
    HeartbeatManager(const HeartbeatManager&) = delete;
    HeartbeatManager& operator=(const HeartbeatManager&) = delete;
    
    void send_heartbeat();
    
    static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 10000;  // 10 seconds
    static constexpr uint32_t MAX_UNACKED_HEARTBEATS = 3;     // Trigger CONNECTION_LOST after 3
    
    uint32_t m_heartbeat_seq = 0;        // Monotonic sequence counter
    uint32_t m_last_ack_seq = 0;         // Last acknowledged sequence
    uint32_t m_last_send_time = 0;       // Timestamp of last heartbeat send
    bool m_initialized = false;
};

#endif // HEARTBEAT_MANAGER_H
