#pragma once

#include <Arduino.h>
#include <cstdint>

namespace LiveTelemetryCache {

struct Snapshot {
    bool known = false;
    uint8_t soc_percent = 0;
    int32_t power_w = 0;
    uint32_t voltage_mv = 0;
    uint32_t last_update_ms = 0;
};

void update_basic(uint8_t soc_percent, int32_t power_w, uint32_t voltage_mv);
bool read_snapshot(Snapshot& out_snapshot);

}  // namespace LiveTelemetryCache
