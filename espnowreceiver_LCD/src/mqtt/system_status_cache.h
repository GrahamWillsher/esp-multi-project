#pragma once

#include <Arduino.h>
#include <cstdint>

namespace SystemStatusCache {

struct Snapshot {
    bool known = false;
    uint8_t contactor_state = 0;
    uint8_t error_flags = 0;
    uint8_t warning_flags = 0;
    uint32_t uptime_seconds = 0;
    uint32_t last_update_ms = 0;
};

void update(uint8_t contactor_state,
            uint8_t error_flags,
            uint8_t warning_flags,
            uint32_t uptime_seconds);

bool read_snapshot(Snapshot& out_snapshot);

}  // namespace SystemStatusCache
