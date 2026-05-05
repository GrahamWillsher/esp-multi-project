#pragma once

#include <cstddef>
#include <cstdint>
#include <esp32common/espnow/common.h>

namespace EspNowReceiver {

enum class RuntimeMessageClass : uint8_t {
    Discovery = 0,
    Heartbeat,
    Data,
    Control,
    Unknown,
};

struct RuntimeMessageView {
    uint8_t message_type{0};
    const uint8_t* payload{nullptr};
    size_t payload_length{0};
    const uint8_t* sender_mac{nullptr};
};

RuntimeMessageClass classify_message(uint8_t message_type);
bool is_discovery_message(uint8_t message_type);
bool is_heartbeat_message(uint8_t message_type);
bool participates_in_generic_activity(uint8_t message_type);

}  // namespace EspNowReceiver