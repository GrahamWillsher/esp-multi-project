#pragma once

#include <Arduino.h>
#include <cstdint>

namespace ChargerRuntimeCache {

struct Snapshot {
    bool known = false;
    float hv_voltage_V = 0.0f;
    float hv_current_A = 0.0f;
    float lv_voltage_V = 0.0f;
    float lv_current_A = 0.0f;
    uint16_t ac_voltage_V = 0;
    float ac_current_A = 0.0f;
    uint16_t power_W = 0;
    uint8_t charger_status = 0;
    uint32_t last_update_ms = 0;
};

void update(float hv_voltage_V,
            float hv_current_A,
            float lv_voltage_V,
            float lv_current_A,
            uint16_t ac_voltage_V,
            float ac_current_A,
            uint16_t power_W,
            uint8_t charger_status);

bool read_snapshot(Snapshot& out_snapshot);

}  // namespace ChargerRuntimeCache
