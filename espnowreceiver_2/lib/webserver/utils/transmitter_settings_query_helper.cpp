#include "transmitter_settings_query_helper.h"
#include "transmitter_settings_cache.h"

namespace TransmitterSettingsQueryHelper {

BatterySettings get_battery_settings() {
    return TransmitterSettingsCache::get_battery_settings();
}

bool has_battery_settings() {
    return TransmitterSettingsCache::has_battery_settings();
}

BatteryEmulatorSettings get_battery_emulator_settings() {
    return TransmitterSettingsCache::get_battery_emulator_settings();
}

bool has_battery_emulator_settings() {
    return TransmitterSettingsCache::has_battery_emulator_settings();
}

PowerSettings get_power_settings() {
    return TransmitterSettingsCache::get_power_settings();
}

bool has_power_settings() {
    return TransmitterSettingsCache::has_power_settings();
}

InverterSettings get_inverter_settings() {
    return TransmitterSettingsCache::get_inverter_settings();
}

bool has_inverter_settings() {
    return TransmitterSettingsCache::has_inverter_settings();
}

CanSettings get_can_settings() {
    return TransmitterSettingsCache::get_can_settings();
}

bool has_can_settings() {
    return TransmitterSettingsCache::has_can_settings();
}

ContactorSettings get_contactor_settings() {
    return TransmitterSettingsCache::get_contactor_settings();
}

bool has_contactor_settings() {
    return TransmitterSettingsCache::has_contactor_settings();
}

} // namespace TransmitterSettingsQueryHelper
