#include "config_handler_common.h"

#include "../config/logging_config.h"

#include <esp32common/espnow/connection_manager.h>
#include <espnow_peer_manager.h>

#include <cstring>

namespace TxConfigHandlerCommon {

bool validate_connected_message(const espnow_queue_msg_t& msg,
                                size_t min_len,
                                const char* log_tag,
                                const char* operation,
                                uint8_t* receiver_mac) {
    if (msg.len < static_cast<int>(min_len)) {
        LOG_ERROR(log_tag, "Invalid %s message size: %d bytes (need >= %u)",
                  operation,
                  msg.len,
                  static_cast<unsigned>(min_len));
        return false;
    }

    const auto state = EspNowConnectionManager::instance().get_state();
    if (state != EspNowConnectionState::CONNECTED) {
        LOG_WARN(log_tag,
                 "Cannot process %s - receiver state is %u (need CONNECTED)",
                 operation,
                 static_cast<uint8_t>(state));
        return false;
    }

    if (receiver_mac) {
        memcpy(receiver_mac, msg.mac, 6);
    }

    return true;
}

bool ensure_peer_registered(const uint8_t* receiver_mac, const char* log_tag) {
    if (!receiver_mac) {
        LOG_ERROR(log_tag, "Receiver MAC is null");
        return false;
    }

    if (EspnowPeerManager::is_peer_registered(receiver_mac)) {
        return true;
    }

    LOG_WARN(log_tag, "Receiver not registered as peer, adding now");
    if (!EspnowPeerManager::add_peer(receiver_mac)) {
        LOG_ERROR(log_tag, "Failed to add receiver as peer");
        return false;
    }

    return true;
}

} // namespace TxConfigHandlerCommon
