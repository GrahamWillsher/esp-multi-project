#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstdint>
#include <espnow_device_state.h>

class RxStateMachine {
public:
    // Shared vocabulary with TxStateMachine and the heartbeat wire format.
    // DISCONNECTED(0) DISCOVERING(1) CONNECTED(2) ACTIVE(3) STALE(4) RECONNECTING(5) FAILED(6)
    using ConnectionState = EspNowDeviceState;

    enum class MessageState : uint8_t {
        IDLE = 0,
        PROCESSING,
        VALID,
        ERROR,
    };

    struct Stats {
        uint32_t total_messages = 0;
        uint32_t valid_messages = 0;
        uint32_t error_messages = 0;
        uint32_t stale_transitions = 0;
        uint32_t last_message_seq = 0;
        uint32_t last_message_ms = 0;
        uint32_t lock_failures = 0;  ///< Mutex acquisition contention/timeout count
    };

    static RxStateMachine& instance();

    bool init();

    void on_message_processing(uint8_t msg_type, uint32_t sequence);
    void on_message_valid();
    void on_message_error();

    void on_connection_established();
    void on_connection_lost();
    void on_activity();
    void check_stale(uint32_t stale_timeout_ms, uint32_t grace_window_ms = 0);
    void on_config_update_sent();

    ConnectionState connection_state() const;
    MessageState message_state() const;
    Stats stats() const;

private:
    RxStateMachine() = default;

    mutable SemaphoreHandle_t mutex_{nullptr};
    ConnectionState connection_state_{ConnectionState::DISCONNECTED};
    MessageState message_state_{MessageState::IDLE};
    Stats stats_{};
    uint8_t last_msg_type_{0};
    uint32_t last_config_update_ms_{0};  // Timestamp of last config update for grace window calculation
    // Incremented without the mutex (benign on embedded single-core ISR paths;
    // only used as a diagnostic metric, not a safety invariant).
    mutable volatile uint32_t lock_failure_count_{0};
};
