#include "config_handler_common.h"

#include "../config/logging_config.h"

#include <cstring>

namespace TxConfigHandlerCommon {

bool validate_connected_message(const incoming_msg_t& msg,
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

    if (receiver_mac) {
        memcpy(receiver_mac, msg.mac, 6);
    }

    return true;
}

} // namespace TxConfigHandlerCommon
