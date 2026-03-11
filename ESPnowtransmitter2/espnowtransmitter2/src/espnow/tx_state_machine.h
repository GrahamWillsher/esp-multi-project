#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstdint>
#include <espnow_device_state.h>

class TxStateMachine {
public:
    // Shared vocabulary with RxStateMachine and the heartbeat wire format.
    // DISCONNECTED(0) DISCOVERING(1) CONNECTED(2) ACTIVE(3) STALE(4) RECONNECTING(5) FAILED(6)
    using ConnectionState = EspNowDeviceState;

    struct Stats {
        uint32_t transitions = 0;
        uint32_t reconnect_attempts = 0;
        uint32_t reconnect_failures = 0;
        uint32_t last_heartbeat_ack_ms = 0;
        uint8_t last_known_channel = 0;
    };

    static TxStateMachine& instance();

    bool init();

    void set_state(ConnectionState state, const char* reason = nullptr);
    ConnectionState state() const;

    void on_discovery_started();
    void on_connected(uint8_t channel);
    void on_transmission_started();
    void on_transmission_stopped();
    void on_connection_lost();

    void on_heartbeat_ack();
    bool heartbeat_timed_out(uint32_t timeout_ms) const;

    uint8_t last_known_channel() const;
    bool is_transmission_active() const;

    uint32_t next_backoff_ms();
    void reset_backoff();

    Stats stats() const;

private:
    TxStateMachine() = default;

    mutable SemaphoreHandle_t mutex_{nullptr};
    ConnectionState state_{ConnectionState::DISCONNECTED};
    Stats stats_{};
    uint8_t reconnect_exp_{0};
};
