#include "battery_data_store.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {

constexpr uint32_t kBatteryStatusStaleMs = 5000;
constexpr uint32_t kBatteryInfoStaleMs = 300000;
constexpr uint32_t kChargerStatusStaleMs = 10000;
constexpr uint32_t kInverterStatusStaleMs = 10000;
constexpr uint32_t kSystemStatusStaleMs = 15000;

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
}

void sync_legacy_globals_from_snapshot_locked() {
    using namespace BatteryData;

    soc_percent = g_snapshot.soc_percent;
    voltage_V = g_snapshot.voltage_V;
    current_A = g_snapshot.current_A;
    temperature_C = g_snapshot.temperature_C;
    power_W = g_snapshot.power_W;
    max_charge_power_W = g_snapshot.max_charge_power_W;
    max_discharge_power_W = g_snapshot.max_discharge_power_W;
    bms_status = g_snapshot.bms_status;
    status_received = g_snapshot.battery_status.received;

    total_capacity_Wh = g_snapshot.total_capacity_Wh;
    reported_capacity_Wh = g_snapshot.reported_capacity_Wh;
    max_design_voltage_V = g_snapshot.max_design_voltage_V;
    min_design_voltage_V = g_snapshot.min_design_voltage_V;
    max_cell_voltage_mV = g_snapshot.max_cell_voltage_mV;
    min_cell_voltage_mV = g_snapshot.min_cell_voltage_mV;
    number_of_cells = g_snapshot.number_of_cells;
    chemistry = g_snapshot.chemistry;
    info_received = g_snapshot.battery_info.received;

    charger_hv_voltage_V = g_snapshot.charger_hv_voltage_V;
    charger_hv_current_A = g_snapshot.charger_hv_current_A;
    charger_lv_voltage_V = g_snapshot.charger_lv_voltage_V;
    charger_ac_voltage_V = g_snapshot.charger_ac_voltage_V;
    charger_power_W = g_snapshot.charger_power_W;
    charger_status = g_snapshot.charger_status;
    charger_received = g_snapshot.charger_status_meta.received;

    inverter_ac_voltage_V = g_snapshot.inverter_ac_voltage_V;
    inverter_ac_frequency_Hz = g_snapshot.inverter_ac_frequency_Hz;
    inverter_ac_current_A = g_snapshot.inverter_ac_current_A;
    inverter_power_W = g_snapshot.inverter_power_W;
    inverter_status = g_snapshot.inverter_status;
    inverter_received = g_snapshot.inverter_status_meta.received;

    contactor_state = g_snapshot.contactor_state;
    error_flags = g_snapshot.error_flags;
    warning_flags = g_snapshot.warning_flags;
    uptime_seconds = g_snapshot.uptime_seconds;
    system_received = g_snapshot.system_status_meta.received;
}

}  // namespace

namespace BatteryData {

// Legacy global definitions (compatibility during migration)
volatile float soc_percent = 0.0f;
volatile float voltage_V = 0.0f;
volatile float current_A = 0.0f;
volatile float temperature_C = 0.0f;
volatile int32_t power_W = 0;
volatile uint16_t max_charge_power_W = 0;
volatile uint16_t max_discharge_power_W = 0;
volatile uint8_t bms_status = BMS_OFFLINE;
volatile bool status_received = false;

uint32_t total_capacity_Wh = 0;
uint32_t reported_capacity_Wh = 0;
uint16_t max_design_voltage_V = 0;
uint16_t min_design_voltage_V = 0;
uint16_t max_cell_voltage_mV = 0;
uint16_t min_cell_voltage_mV = 0;
uint8_t number_of_cells = 0;
uint8_t chemistry = 0;
bool info_received = false;

volatile float charger_hv_voltage_V = 0.0f;
volatile float charger_hv_current_A = 0.0f;
volatile float charger_lv_voltage_V = 0.0f;
volatile uint16_t charger_ac_voltage_V = 0;
volatile uint16_t charger_power_W = 0;
volatile uint8_t charger_status = 0;
volatile bool charger_received = false;

volatile uint16_t inverter_ac_voltage_V = 0;
volatile float inverter_ac_frequency_Hz = 0.0f;
volatile float inverter_ac_current_A = 0.0f;
volatile int32_t inverter_power_W = 0;
volatile uint8_t inverter_status = 0;
volatile bool inverter_received = false;

volatile uint8_t contactor_state = 0;
volatile uint8_t error_flags = 0;
volatile uint8_t warning_flags = 0;
volatile uint32_t uptime_seconds = 0;
volatile bool system_received = false;

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
    sync_legacy_globals_from_snapshot_locked();
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
    g_snapshot.voltage_V = voltage_mv / 1000.0f;

    g_snapshot.battery_status.received = true;
    g_snapshot.battery_status.last_update_ms = millis();
    g_snapshot.battery_status.stale = false;

    ++g_snapshot_seq;
    sync_legacy_globals_from_snapshot_locked();
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
    sync_legacy_globals_from_snapshot_locked();
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
    sync_legacy_globals_from_snapshot_locked();
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
    sync_legacy_globals_from_snapshot_locked();
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
    sync_legacy_globals_from_snapshot_locked();
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
    sync_legacy_globals_from_snapshot_locked();
    unlock_snapshot();
}

}  // namespace BatteryData
