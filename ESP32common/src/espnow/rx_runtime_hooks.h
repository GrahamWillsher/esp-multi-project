#pragma once

#include "../../espnow_common_utils/espnow_device_state.h"
#include <cstddef>
#include <cstdint>

namespace EspNowReceiver {

class RuntimeHooks {
public:
    virtual ~RuntimeHooks() = default;

    virtual void on_runtime_initialized() {}
    virtual void on_runtime_reset() {}
    virtual void on_runtime_tick(uint32_t now_ms) {}
    virtual void on_connection_state_changed(EspNowDeviceState previous_state,
                                             EspNowDeviceState new_state) {}
    virtual void on_quiet_mode_changed(bool quiet_mode_active) {}
    virtual void on_message_processed(uint8_t message_type,
                                      const uint8_t* sender_mac,
                                      size_t payload_length) {}
};

}  // namespace EspNowReceiver