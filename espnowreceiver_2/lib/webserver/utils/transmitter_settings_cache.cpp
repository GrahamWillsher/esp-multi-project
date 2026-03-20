#include "transmitter_settings_cache.h"

#include <Preferences.h>

namespace {
    constexpr const char* kKeyBatteryKnown = "batt_known";
    constexpr const char* kKeyBatterySettings = "batt_settings";

    constexpr const char* kKeyBatteryEmuKnown = "batt_emu_known";
    constexpr const char* kKeyBatteryEmuSettings = "batt_emu_set";

    constexpr const char* kKeyPowerKnown = "power_known";
    constexpr const char* kKeyPowerSettings = "power_settings";

    constexpr const char* kKeyInverterKnown = "inv_known";
    constexpr const char* kKeyInverterSettings = "inv_settings";

    constexpr const char* kKeyCanKnown = "can_known";
    constexpr const char* kKeyCanSettings = "can_settings";

    constexpr const char* kKeyContactorKnown = "contactor_known";
    constexpr const char* kKeyContactorSettings = "contactor_set";

    struct SettingsCache {
        BatterySettings battery_settings = {
            .capacity_wh = 30000,
            .max_voltage_mv = 58000,
            .min_voltage_mv = 46000,
            .max_charge_current_a = 100.0f,
            .max_discharge_current_a = 100.0f,
            .soc_high_limit = 95,
            .soc_low_limit = 20,
            .cell_count = 16,
            .chemistry = 2,
            .version = 0
        };
        bool battery_settings_known = false;

        BatteryEmulatorSettings battery_emulator_settings = {
            .double_battery = false,
            .pack_max_voltage_dV = 580,
            .pack_min_voltage_dV = 460,
            .cell_max_voltage_mV = 4200,
            .cell_min_voltage_mV = 3000,
            .soc_estimated = false,
            .led_mode = 0
        };
        bool battery_emulator_settings_known = false;

        PowerSettings power_settings = {
            .charge_w = 3000,
            .discharge_w = 3000,
            .max_precharge_ms = 15000,
            .precharge_duration_ms = 100
        };
        bool power_settings_known = false;

        InverterSettings inverter_settings = {
            .cells = 0,
            .modules = 0,
            .cells_per_module = 0,
            .voltage_level = 0,
            .capacity_ah = 0,
            .battery_type = 0
        };
        bool inverter_settings_known = false;

        CanSettings can_settings = {
            .frequency_khz = 8,
            .fd_frequency_mhz = 40,
            .sofar_id = 0,
            .pylon_send_interval_ms = 0
        };
        bool can_settings_known = false;

        ContactorSettings contactor_settings = {
            .control_enabled = false,
            .nc_contactor = false,
            .pwm_frequency_hz = 20000
        };
        bool contactor_settings_known = false;
    };

    SettingsCache settings_cache;
}

namespace TransmitterSettingsCache {

void load_from_prefs(void* prefs_ptr) {
    if (prefs_ptr == nullptr) return;
    Preferences& prefs = *static_cast<Preferences*>(prefs_ptr);

    size_t batt_len = prefs.getBytes(kKeyBatterySettings, &settings_cache.battery_settings, sizeof(settings_cache.battery_settings));
    settings_cache.battery_settings_known = prefs.getBool(kKeyBatteryKnown, false) && batt_len == sizeof(settings_cache.battery_settings);

    size_t batt_emu_len = prefs.getBytes(kKeyBatteryEmuSettings, &settings_cache.battery_emulator_settings, sizeof(settings_cache.battery_emulator_settings));
    settings_cache.battery_emulator_settings_known = prefs.getBool(kKeyBatteryEmuKnown, false) && batt_emu_len == sizeof(settings_cache.battery_emulator_settings);

    size_t power_len = prefs.getBytes(kKeyPowerSettings, &settings_cache.power_settings, sizeof(settings_cache.power_settings));
    settings_cache.power_settings_known = prefs.getBool(kKeyPowerKnown, false) && power_len == sizeof(settings_cache.power_settings);

    size_t inverter_len = prefs.getBytes(kKeyInverterSettings, &settings_cache.inverter_settings, sizeof(settings_cache.inverter_settings));
    settings_cache.inverter_settings_known = prefs.getBool(kKeyInverterKnown, false) && inverter_len == sizeof(settings_cache.inverter_settings);

    size_t can_len = prefs.getBytes(kKeyCanSettings, &settings_cache.can_settings, sizeof(settings_cache.can_settings));
    settings_cache.can_settings_known = prefs.getBool(kKeyCanKnown, false) && can_len == sizeof(settings_cache.can_settings);

    size_t contactor_len = prefs.getBytes(kKeyContactorSettings, &settings_cache.contactor_settings, sizeof(settings_cache.contactor_settings));
    settings_cache.contactor_settings_known = prefs.getBool(kKeyContactorKnown, false) && contactor_len == sizeof(settings_cache.contactor_settings);
}

void save_to_prefs(void* prefs_ptr) {
    if (prefs_ptr == nullptr) return;
    Preferences& prefs = *static_cast<Preferences*>(prefs_ptr);

    prefs.putBool(kKeyBatteryKnown, settings_cache.battery_settings_known);
    prefs.putBytes(kKeyBatterySettings, &settings_cache.battery_settings, sizeof(settings_cache.battery_settings));

    prefs.putBool(kKeyBatteryEmuKnown, settings_cache.battery_emulator_settings_known);
    prefs.putBytes(kKeyBatteryEmuSettings, &settings_cache.battery_emulator_settings, sizeof(settings_cache.battery_emulator_settings));

    prefs.putBool(kKeyPowerKnown, settings_cache.power_settings_known);
    prefs.putBytes(kKeyPowerSettings, &settings_cache.power_settings, sizeof(settings_cache.power_settings));

    prefs.putBool(kKeyInverterKnown, settings_cache.inverter_settings_known);
    prefs.putBytes(kKeyInverterSettings, &settings_cache.inverter_settings, sizeof(settings_cache.inverter_settings));

    prefs.putBool(kKeyCanKnown, settings_cache.can_settings_known);
    prefs.putBytes(kKeyCanSettings, &settings_cache.can_settings, sizeof(settings_cache.can_settings));

    prefs.putBool(kKeyContactorKnown, settings_cache.contactor_settings_known);
    prefs.putBytes(kKeyContactorSettings, &settings_cache.contactor_settings, sizeof(settings_cache.contactor_settings));
}

void store_battery_settings(const BatterySettings& settings) {
    settings_cache.battery_settings = settings;
    settings_cache.battery_settings_known = true;
}

BatterySettings get_battery_settings() {
    return settings_cache.battery_settings;
}

bool has_battery_settings() {
    return settings_cache.battery_settings_known;
}

void store_battery_emulator_settings(const BatteryEmulatorSettings& settings) {
    settings_cache.battery_emulator_settings = settings;
    settings_cache.battery_emulator_settings_known = true;
}

BatteryEmulatorSettings get_battery_emulator_settings() {
    return settings_cache.battery_emulator_settings;
}

bool has_battery_emulator_settings() {
    return settings_cache.battery_emulator_settings_known;
}

void store_power_settings(const PowerSettings& settings) {
    settings_cache.power_settings = settings;
    settings_cache.power_settings_known = true;
}

PowerSettings get_power_settings() {
    return settings_cache.power_settings;
}

bool has_power_settings() {
    return settings_cache.power_settings_known;
}

void store_inverter_settings(const InverterSettings& settings) {
    settings_cache.inverter_settings = settings;
    settings_cache.inverter_settings_known = true;
}

InverterSettings get_inverter_settings() {
    return settings_cache.inverter_settings;
}

bool has_inverter_settings() {
    return settings_cache.inverter_settings_known;
}

void store_can_settings(const CanSettings& settings) {
    settings_cache.can_settings = settings;
    settings_cache.can_settings_known = true;
}

CanSettings get_can_settings() {
    return settings_cache.can_settings;
}

bool has_can_settings() {
    return settings_cache.can_settings_known;
}

void store_contactor_settings(const ContactorSettings& settings) {
    settings_cache.contactor_settings = settings;
    settings_cache.contactor_settings_known = true;
}

ContactorSettings get_contactor_settings() {
    return settings_cache.contactor_settings;
}

bool has_contactor_settings() {
    return settings_cache.contactor_settings_known;
}

void update_battery_cell_count(uint16_t cell_count) {
    if (cell_count > 0 && cell_count <= 255) {
        settings_cache.battery_settings.cell_count = static_cast<uint8_t>(cell_count);
    }
}

} // namespace TransmitterSettingsCache
