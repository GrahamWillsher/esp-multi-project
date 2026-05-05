#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cstdint>
#include <espnow_device_state.h>

class RxStateMachine {
public:
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
        uint32_t lock_failures = 0;
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
    uint32_t last_config_update_ms_{0};
    mutable volatile uint32_t lock_failure_count_{0};
};
