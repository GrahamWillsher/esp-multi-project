#ifndef TELEMETRY_SNAPSHOT_UTILS_H
#define TELEMETRY_SNAPSHOT_UTILS_H

#include <Arduino.h>
#include <cstdint>

#include "../../src/mqtt/live_telemetry_cache.h"
#include "../../src/mqtt/system_status_cache.h"
#include "../../src/mqtt/charger_runtime_cache.h"
#include "../../src/mqtt/inverter_runtime_cache.h"
#include "transmitter_manager.h"
#include "cell_data_cache.h"

namespace TelemetrySnapshotUtils {

inline void fill_snapshot_telemetry(uint8_t& out_soc, int32_t& out_power, uint32_t& out_voltage_mv) {
    LiveTelemetryCache::Snapshot snapshot;
    if (!LiveTelemetryCache::read_snapshot(snapshot) || !snapshot.known) {
        out_soc = 0;
        out_power = 0;
        out_voltage_mv = 0;
        return;
    }

    out_soc = snapshot.soc_percent;
    out_power = snapshot.power_w;
    out_voltage_mv = snapshot.voltage_mv;
}

inline bool fill_system_status(uint8_t& out_contactor_state,
                                uint8_t& out_error_flags,
                                uint8_t& out_warning_flags,
                                uint32_t& out_uptime_seconds) {
    SystemStatusCache::Snapshot snapshot;
    if (!SystemStatusCache::read_snapshot(snapshot) || !snapshot.known) {
        out_contactor_state = 0;
        out_error_flags = 0;
        out_warning_flags = 0;
        out_uptime_seconds = 0;
        return false;
    }
    out_contactor_state = snapshot.contactor_state;
    out_error_flags = snapshot.error_flags;
    out_warning_flags = snapshot.warning_flags;
    out_uptime_seconds = snapshot.uptime_seconds;
    return true;
}

inline bool fill_charger_status(float& out_hv_voltage_V,
                                float& out_hv_current_A,
                                float& out_lv_voltage_V,
                                float& out_lv_current_A,
                                uint16_t& out_ac_voltage_V,
                                float& out_ac_current_A,
                                uint16_t& out_power_W,
                                uint8_t& out_charger_status) {
    ChargerRuntimeCache::Snapshot snapshot;
    if (!ChargerRuntimeCache::read_snapshot(snapshot) || !snapshot.known) {
        out_hv_voltage_V = 0.0f;
        out_hv_current_A = 0.0f;
        out_lv_voltage_V = 0.0f;
        out_lv_current_A = 0.0f;
        out_ac_voltage_V = 0;
        out_ac_current_A = 0.0f;
        out_power_W = 0;
        out_charger_status = 0;
        return false;
    }

    out_hv_voltage_V = snapshot.hv_voltage_V;
    out_hv_current_A = snapshot.hv_current_A;
    out_lv_voltage_V = snapshot.lv_voltage_V;
    out_lv_current_A = snapshot.lv_current_A;
    out_ac_voltage_V = snapshot.ac_voltage_V;
    out_ac_current_A = snapshot.ac_current_A;
    out_power_W = snapshot.power_W;
    out_charger_status = snapshot.charger_status;
    return true;
}

inline bool fill_inverter_status(uint16_t& out_ac_voltage_V,
                                 uint16_t& out_ac_frequency_dHz,
                                 int16_t& out_ac_current_dA,
                                 int32_t& out_power_W,
                                 uint8_t& out_inverter_status) {
    InverterRuntimeCache::Snapshot snapshot;
    if (!InverterRuntimeCache::read_snapshot(snapshot) || !snapshot.known) {
        out_ac_voltage_V = 0;
        out_ac_frequency_dHz = 0;
        out_ac_current_dA = 0;
        out_power_W = 0;
        out_inverter_status = 0;
        return false;
    }

    out_ac_voltage_V = snapshot.ac_voltage_V;
    out_ac_frequency_dHz = snapshot.ac_frequency_dHz;
    out_ac_current_dA = snapshot.ac_current_dA;
    out_power_W = snapshot.power_W;
    out_inverter_status = snapshot.inverter_status;
    return true;
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