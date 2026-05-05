#pragma once

#include <Arduino.h>
#include <esp_err.h>
#include <esp32common/espnow/common.h>
#include <cstdint>

struct RxHeartbeatManagerHooks {
    void* context = nullptr;
    void (*on_transmitter_reboot_detected)(void* context) = nullptr;
    void (*on_heartbeat_payload)(void* context, const heartbeat_t* hb, const uint8_t* mac) = nullptr;
    void (*on_heartbeat_ack_enqueue_failure)(void* context, esp_err_t err) = nullptr;
    uint8_t (*get_connection_state)(void* context) = nullptr;
    uint32_t (*get_link_activity_time_ms)(void* context) = nullptr;
};

struct RxHeartbeatManagerConfig {
    uint32_t temperature_tick_interval_ms = 10000;
    bool use_link_activity_as_keepalive = false;
};

struct RxHeartbeatAckEnqueueStats {
    uint32_t enqueue_attempts = 0;
    uint32_t enqueue_success = 0;
    uint32_t enqueue_no_mem_failures = 0;
    uint32_t enqueue_other_failures = 0;
};

class RxHeartbeatManager {
public:
    static RxHeartbeatManager& instance();

    void configure_hooks(const RxHeartbeatManagerHooks& hooks);
    void configure(const RxHeartbeatManagerConfig& config);

    void init();
    void tick();
    void on_heartbeat(const heartbeat_t* hb, const uint8_t* mac);
    void reset();
    void on_connection_established();

    uint32_t get_received_count() const { return m_heartbeats_received; }
    uint32_t get_sent_ack_count() const { return m_acks_sent; }
    uint32_t get_last_seq() const { return m_last_heartbeat_seq; }
    uint32_t get_time_since_last() const { return millis() - m_last_rx_time_ms; }
    bool read_ack_enqueue_stats(RxHeartbeatAckEnqueueStats& out_stats) const;
    void reset_ack_enqueue_stats();

private:
    RxHeartbeatManager() = default;
    RxHeartbeatManager(const RxHeartbeatManager&) = delete;
    RxHeartbeatManager& operator=(const RxHeartbeatManager&) = delete;

    void send_ack(uint32_t ack_seq, const uint8_t* mac);

    RxHeartbeatManagerHooks hooks_{};
    RxHeartbeatManagerConfig config_{};

    uint32_t m_last_heartbeat_seq = 0;
    uint32_t m_last_rx_time_ms = 0;
    uint32_t m_heartbeats_received = 0;
    uint32_t m_acks_sent = 0;
    RxHeartbeatAckEnqueueStats m_ack_enqueue_stats{};
    bool m_initialized = false;
};
