#include "espnow/battery_data_store.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {

constexpr uint32_t kBatteryStatusStaleMs = 5000;
constexpr uint32_t kBatteryInfoStaleMs = 300000;
constexpr uint32_t kChargerStatusStaleMs = 10000;
constexpr uint32_t kInverterStatusStaleMs = 10000;
constexpr uint32_t kSystemStatusStaleMs = 15000;
constexpr uint32_t kComponentConfigStaleMs = 30000;

SemaphoreHandle_t g_snapshot_mutex = nullptr;
BatteryData::TelemetrySnapshot g_snapshot;
volatile uint32_t g_snapshot_seq = 0;

bool ensure_mutex() {
    if (g_snapshot_mutex != nullptr) {
        return true;
    }
    g_snapshot_mutex = xSemaphoreCreateMutex();
    return g_snapshot_mutex != nullptr;
}

bool lock_snapshot(uint32_t timeout_ms = 20) {
    if (!ensure_mutex()) {
        return false;
    }
    return xSemaphoreTake(g_snapshot_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void unlock_snapshot() {
    if (g_snapshot_mutex) {
        xSemaphoreGive(g_snapshot_mutex);
    }
}

void update_section_stale(BatteryData::SectionState& section,
                          uint32_t now_ms,
                          uint32_t stale_threshold_ms) {
    if (!section.received) {
        section.stale = true;
        return;
    }
    section.stale = (now_ms - section.last_update_ms) > stale_threshold_ms;
}

void refresh_stale_locked(uint32_t now_ms) {
    update_section_stale(g_snapshot.battery_status, now_ms, kBatteryStatusStaleMs);
    update_section_stale(g_snapshot.battery_info, now_ms, kBatteryInfoStaleMs);
    update_section_stale(g_snapshot.charger_status_meta, now_ms, kChargerStatusStaleMs);
    update_section_stale(g_snapshot.inverter_status_meta, now_ms, kInverterStatusStaleMs);
    update_section_stale(g_snapshot.system_status_meta, now_ms, kSystemStatusStaleMs);
    update_section_stale(g_snapshot.component_config_meta, now_ms, kComponentConfigStaleMs);
}

}  // namespace

namespace BatteryData {

void update_battery_status(const battery_status_msg_t& data) {
    if (!lock_snapshot()) {
        return;
    }

    g_snapshot.soc_percent = data.soc_percent_100 / 100.0f;
    g_snapshot.voltage_V = data.voltage_mV / 1000.0f;
    g_snapshot.current_A = data.current_mA / 1000.0f;
    g_snapshot.temperature_C = data.temperature_dC / 10.0f;
    g_snapshot.power_W = data.power_W;
    g_snapshot.max_charge_power_W = data.max_charge_power_W;
    g_snapshot.max_discharge_power_W = data.max_discharge_power_W;
    g_snapshot.bms_status = data.bms_status;

    g_snapshot.battery_status.received = true;
    g_snapshot.battery_status.last_update_ms = millis();
    g_snapshot.battery_status.stale = false;

    ++g_snapshot_seq;
    unlock_snapshot();
}

void update_basic_telemetry(uint8_t soc_percent, int32_t power_w, uint32_t voltage_mv) {
    if (!lock_snapshot()) {
        return;
    }

    int clamped_soc = soc_percent;
    if (clamped_soc < 0) clamped_soc = 0;
    if (clamped_soc > 100) clamped_soc = 100;

    g_snapshot.soc_percent = static_cast<float>(clamped_soc);
    g_snapshot.power_W = power_w;
    if (voltage_mv > 0) {
        g_snapshot.voltage_V = voltage_mv / 1000.0f;
    }

    g_snapshot.battery_status.received = true;
    g_snapshot.battery_status.last_update_ms = millis();
    g_snapshot.battery_status.stale = false;

    ++g_snapshot_seq;
    unlock_snapshot();
}

void update_battery_info(const battery_info_msg_t& data) {
    if (!lock_snapshot()) {
        return;
    }

    g_snapshot.total_capacity_Wh = data.total_capacity_Wh;
    g_snapshot.reported_capacity_Wh = data.reported_capacity_Wh;
    g_snapshot.max_design_voltage_V = static_cast<uint16_t>(data.max_design_voltage_dV / 10);
    g_snapshot.min_design_voltage_V = static_cast<uint16_t>(data.min_design_voltage_dV / 10);
    g_snapshot.max_cell_voltage_mV = data.max_cell_voltage_mV;
    g_snapshot.min_cell_voltage_mV = data.min_cell_voltage_mV;
    g_snapshot.number_of_cells = data.number_of_cells;
    g_snapshot.chemistry = data.chemistry;

    g_snapshot.battery_info.received = true;
    g_snapshot.battery_info.last_update_ms = millis();
    g_snapshot.battery_info.stale = false;

    ++g_snapshot_seq;
    unlock_snapshot();
}

void update_battery_info(const battery_settings_full_msg_t& data, uint32_t settings_version) {
    if (!lock_snapshot()) {
        return;
    }

    g_snapshot.total_capacity_Wh = data.capacity_wh;
    g_snapshot.reported_capacity_Wh = data.capacity_wh;
    g_snapshot.max_design_voltage_V = static_cast<uint16_t>(data.max_voltage_mv / 1000);
    g_snapshot.min_design_voltage_V = static_cast<uint16_t>(data.min_voltage_mv / 1000);
    g_snapshot.number_of_cells = data.cell_count;
    g_snapshot.chemistry = data.chemistry;

    g_snapshot.battery_info.received = true;
    g_snapshot.battery_info.last_update_ms = millis();
    g_snapshot.battery_info.version = settings_version;
    g_snapshot.battery_info.stale = false;

    ++g_snapshot_seq;
    unlock_snapshot();
}

void update_charger_status(const charger_status_msg_t& data) {
    if (!lock_snapshot()) {
        return;
    }

    g_snapshot.charger_hv_voltage_V = data.hv_voltage_dV / 10.0f;
    g_snapshot.charger_hv_current_A = data.hv_current_dA / 10.0f;
    g_snapshot.charger_lv_voltage_V = data.lv_voltage_dV / 10.0f;
    g_snapshot.charger_ac_voltage_V = data.ac_voltage_V;
    g_snapshot.charger_power_W = data.power_W;
    g_snapshot.charger_status = data.charger_status;

    g_snapshot.charger_status_meta.received = true;
    g_snapshot.charger_status_meta.last_update_ms = millis();
    g_snapshot.charger_status_meta.stale = false;

    ++g_snapshot_seq;
    unlock_snapshot();
}

void update_inverter_status(const inverter_status_msg_t& data) {
    if (!lock_snapshot()) {
        return;
    }

    g_snapshot.inverter_ac_voltage_V = data.ac_voltage_V;
    g_snapshot.inverter_ac_frequency_Hz = data.ac_frequency_dHz / 10.0f;
    g_snapshot.inverter_ac_current_A = data.ac_current_dA / 10.0f;
    g_snapshot.inverter_power_W = data.power_W;
    g_snapshot.inverter_status = data.inverter_status;

    g_snapshot.inverter_status_meta.received = true;
    g_snapshot.inverter_status_meta.last_update_ms = millis();
    g_snapshot.inverter_status_meta.stale = false;

    ++g_snapshot_seq;
    unlock_snapshot();
}

void update_system_status(const system_status_msg_t& data) {
    if (!lock_snapshot()) {
        return;
    }

    g_snapshot.contactor_state = data.contactor_state;
    g_snapshot.error_flags = data.error_flags;
    g_snapshot.warning_flags = data.warning_flags;
    g_snapshot.uptime_seconds = data.uptime_seconds;

    g_snapshot.system_status_meta.received = true;
    g_snapshot.system_status_meta.last_update_ms = millis();
    g_snapshot.system_status_meta.stale = false;

    ++g_snapshot_seq;
    unlock_snapshot();
}

void update_component_config(const component_config_msg_t& data) {
    if (!lock_snapshot()) {
        return;
    }

    g_snapshot.bms_type = data.bms_type;
    g_snapshot.secondary_bms_type = data.secondary_bms_type;
    g_snapshot.battery_type = data.battery_type;
    g_snapshot.inverter_type = data.inverter_type;
    g_snapshot.charger_type = data.charger_type;
    g_snapshot.shunt_type = data.shunt_type;
    g_snapshot.multi_battery_enabled = data.multi_battery_enabled;
    g_snapshot.component_config_version = data.config_version;

    g_snapshot.component_config_meta.received = true;
    g_snapshot.component_config_meta.last_update_ms = millis();
    g_snapshot.component_config_meta.version = data.config_version;
    g_snapshot.component_config_meta.stale = false;

    ++g_snapshot_seq;
    unlock_snapshot();
}

bool read_snapshot(TelemetrySnapshot& out_snapshot) {
    if (!lock_snapshot()) {
        return false;
    }

    refresh_stale_locked(millis());
    out_snapshot = g_snapshot;
    unlock_snapshot();
    return true;
}

uint32_t snapshot_sequence() {
    return g_snapshot_seq;
}

void refresh_staleness(uint32_t now_ms) {
    if (now_ms == 0) {
        now_ms = millis();
    }

    if (!lock_snapshot()) {
        return;
    }

    refresh_stale_locked(now_ms);
    unlock_snapshot();
}

}  // namespace BatteryData
