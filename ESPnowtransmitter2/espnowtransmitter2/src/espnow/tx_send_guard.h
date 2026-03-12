#pragma once

#include <cstddef>
#include <cstdint>
#include <esp_err.h>

namespace TxSendGuard {

bool is_peer_channel_coherent(const uint8_t* peer_mac, uint8_t* out_home = nullptr, uint8_t* out_peer = nullptr);

esp_err_t send_to_receiver_guarded(
    const uint8_t* mac,
    const uint8_t* data,
    size_t len,
    const char* tag
);

void notify_connection_state(bool connected);

} // namespace TxSendGuard
