#pragma once

#include <Arduino.h>
#include <esp32common/espnow/common.h>

class RxHeartbeatManager {
public:
    static RxHeartbeatManager& instance();

    void init();
    void tick();
    void on_heartbeat(const heartbeat_t* hb, const uint8_t* mac);
    void reset();
    void on_connection_established();

    uint32_t get_received_count() const { return m_heartbeats_received; }
    uint32_t get_sent_ack_count() const { return m_acks_sent; }
    uint32_t get_last_seq() const { return m_last_heartbeat_seq; }
    uint32_t get_time_since_last() const { return millis() - m_last_rx_time_ms; }

private:
    RxHeartbeatManager() = default;
    RxHeartbeatManager(const RxHeartbeatManager&) = delete;
    RxHeartbeatManager& operator=(const RxHeartbeatManager&) = delete;

    void send_ack(uint32_t ack_seq, const uint8_t* mac);

    uint32_t m_last_heartbeat_seq = 0;
    uint32_t m_last_rx_time_ms = 0;
    uint32_t m_heartbeats_received = 0;
    uint32_t m_acks_sent = 0;
    bool m_initialized = false;
};
