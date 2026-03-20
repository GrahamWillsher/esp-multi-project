#ifndef TRANSMITTER_SETTINGS_CACHE_H
#define TRANSMITTER_SETTINGS_CACHE_H

#include "transmitter_settings_types.h"

namespace TransmitterSettingsCache {

void load_from_prefs(void* prefs_ptr);
void save_to_prefs(void* prefs_ptr);

void store_battery_settings(const BatterySettings& settings);
BatterySettings get_battery_settings();
bool has_battery_settings();

void store_battery_emulator_settings(const BatteryEmulatorSettings& settings);
BatteryEmulatorSettings get_battery_emulator_settings();
bool has_battery_emulator_settings();

void store_power_settings(const PowerSettings& settings);
PowerSettings get_power_settings();
bool has_power_settings();

void store_inverter_settings(const InverterSettings& settings);
InverterSettings get_inverter_settings();
bool has_inverter_settings();

void store_can_settings(const CanSettings& settings);
CanSettings get_can_settings();
bool has_can_settings();

void store_contactor_settings(const ContactorSettings& settings);
ContactorSettings get_contactor_settings();
bool has_contactor_settings();

void update_battery_cell_count(uint16_t cell_count);

} // namespace TransmitterSettingsCache

#endif // TRANSMITTER_SETTINGS_CACHE_H
