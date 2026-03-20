#pragma once

#include "../battery_handlers.h"

namespace BatteryHandlersInternal {

template <typename MessageT, typename ApplyFn, typename LogFn>
inline void process_status_message(const espnow_queue_msg_t* msg,
                                   const char* label,
                                   ApplyFn apply,
                                   LogFn log_fn) {
    if (!msg || msg->len < static_cast<int>(sizeof(MessageT))) {
        LOG_ERROR("BATTERY", "%s: Invalid message size %d, expected >= %d",
                  label,
                  msg ? msg->len : -1,
                  static_cast<int>(sizeof(MessageT)));
        return;
    }

    const MessageT* data = reinterpret_cast<const MessageT*>(msg->data);

    if (!validate_checksum(data, sizeof(*data))) {
        LOG_ERROR("BATTERY", "%s: Invalid checksum - message rejected", label);
        return;
    }

    apply(*data);
    log_fn();
}

}  // namespace BatteryHandlersInternal
