#ifndef TELEMETRY_SNAPSHOT_UTILS_H
#define TELEMETRY_SNAPSHOT_UTILS_H

#include <Arduino.h>
#include <cstdint>

#include "../../src/espnow/battery_data_store.h"
#include "transmitter_manager.h"
#include "cell_data_cache.h"

namespace TelemetrySnapshotUtils {

inline void fill_snapshot_telemetry(uint8_t& out_soc, int32_t& out_power, uint32_t& out_voltage_mv) {
    BatteryData::TelemetrySnapshot snapshot;
    if (!BatteryData::read_snapshot(snapshot) || !snapshot.battery_status.received) {
        out_soc = 0;
        out_power = 0;
        out_voltage_mv = 0;
        return;
    }

    int soc_int = static_cast<int>(snapshot.soc_percent + 0.5f);
    if (soc_int < 0) soc_int = 0;
    if (soc_int > 100) soc_int = 100;

    out_soc = static_cast<uint8_t>(soc_int);
    out_power = snapshot.power_W;
    out_voltage_mv = static_cast<uint32_t>(snapshot.voltage_V * 1000.0f);
}

inline String serialize_cell_data(const CellDataCache::CellDataSnapshot& snapshot) {
    String json = "{\"success\":true,\"cells\":[";
    json.reserve(180 + (snapshot.cell_count * 16));

    for (uint16_t i = 0; i < snapshot.cell_count; i++) {
        if (i > 0) json += ",";
        json += String(snapshot.voltages_mV[i]);
    }

    json += "],\"balancing\":[";
    for (uint16_t i = 0; i < snapshot.cell_count; i++) {
        if (i > 0) json += ",";
        json += snapshot.balancing_status[i] ? "true" : "false";
    }

    json += "],\"cell_min_voltage_mV\":";
    json += String(snapshot.min_voltage_mV);
    json += ",\"cell_max_voltage_mV\":";
    json += String(snapshot.max_voltage_mV);
    json += ",\"balancing_active\":";
    json += snapshot.balancing_active ? "true" : "false";
    json += ",\"mode\":\"";
    json += snapshot.data_source;
    json += "\"}";

    return json;
}

} // namespace TelemetrySnapshotUtils

#endif // TELEMETRY_SNAPSHOT_UTILS_H