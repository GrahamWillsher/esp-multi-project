#pragma once

#include <Arduino.h>
#include <cstdint>

namespace InverterRuntimeCache {

struct Snapshot {
    bool known = false;
    uint16_t ac_voltage_V = 0;
    uint16_t ac_frequency_dHz = 0;
    int16_t ac_current_dA = 0;
    int32_t power_W = 0;
    uint8_t inverter_status = 0;
    uint32_t last_update_ms = 0;
};

void update(uint16_t ac_voltage_V,
            uint16_t ac_frequency_dHz,
            int16_t ac_current_dA,
            int32_t power_W,
            uint8_t inverter_status);

bool read_snapshot(Snapshot& out_snapshot);

}  // namespace InverterRuntimeCache
