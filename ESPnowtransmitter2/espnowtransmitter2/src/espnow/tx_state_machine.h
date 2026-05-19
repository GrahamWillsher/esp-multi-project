#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstdint>
#include <espnow_device_state.h>

class TxStateMachine {
public:
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

    ConnectionState derive_state_locked() const;
    void note_transition_locked(ConnectionState next_state);

    mutable SemaphoreHandle_t mutex_{nullptr};
    Stats stats_{};
    uint8_t reconnect_exp_{0};
    bool transmission_active_{false};
    ConnectionState last_reported_state_{ConnectionState::DISCONNECTED};
};
