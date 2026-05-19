#pragma once

#include "link_truth.h"

#include <freertos/FreeRTOS.h>
#include <cstdint>

class ChannelManager;

namespace esp32common::espnow {

struct ChannelAuthoritySnapshot {
    uint8_t current_radio_channel = 0;
    uint8_t candidate_channel = 0;
    uint8_t operating_channel = 0;
    uint8_t last_good_channel = 0;
    bool locked = false;
    uint32_t last_update_ms = 0;
};

class ChannelAuthority {
public:
    static ChannelAuthority& instance();

    bool init();

    ChannelAuthoritySnapshot snapshot() const;
    uint8_t operating_channel() const;
    uint8_t candidate_channel() const;
    uint8_t last_good_channel() const;
    bool is_locked() const;
    uint8_t reconnect_hint_channel() const;

    void set_candidate_channel(uint8_t channel, const char* source = "unknown");
    bool apply_candidate_channel(const char* source = "unknown");
    bool commit_receiver_confirmed_channel(uint8_t channel,
                                           const char* source = "unknown",
                                           bool persist_last_good = true);
    void persist_last_good_channel(uint8_t channel, const char* source = "unknown");
    void unlock(const char* source = "unknown");
    void on_ack_channel_reported(uint8_t channel,
                                 bool apply_wifi_channel,
                                 const char* source = "ACK");

private:
    ChannelAuthority() = default;

    void sync_from_channel_manager(uint32_t now_ms);
    uint8_t load_last_good_channel() const;
    void save_last_good_channel(uint8_t channel) const;

    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    ChannelAuthoritySnapshot snapshot_{};
    bool initialized_ = false;
};

}  // namespace esp32common::espnow
