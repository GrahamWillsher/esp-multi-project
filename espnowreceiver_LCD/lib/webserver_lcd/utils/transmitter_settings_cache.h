#ifndef TRANSMITTER_SETTINGS_CACHE_H
#define TRANSMITTER_SETTINGS_CACHE_H

/**
 * CACHE / PERSISTENCE PATTERN CONTRACT (Section 4.4)
 *
 * This module is the canonical reference implementation for the
 * settings-cache / NVS-persistence pattern used in the receiver webserver.
 *
 * Pattern rules (follow these for any new settings module):
 *
 *   1. STORAGE:  All settings are held in plain-old-data structs defined in
 *                transmitter_settings_types.h.  The structs are value-typed —
 *                no heap pointers.
 *
 *   2. ACCESSORS: Each settings domain exposes a symmetric triplet:
 *                   store_<domain>_settings(const T&)
 *                   get_<domain>_settings() -> T
 *                   has_<domain>_settings() -> bool
 *                 has_*() returns true only after the first successful store.
 *
 *   3. PERSISTENCE: load_from_prefs() and save_to_prefs() serialise to/from
 *                   NVS via Preferences.  Call load on boot; call save after
 *                   any store that should survive reboot.
 *
 *   4. THREAD SAFETY: Callers are responsible for task-level serialisation.
 *                     This module does NOT use a mutex internally.
 *
 *   5. OWNERSHIP: This module owns the receiver-side cache of transmitter-reported
 *                 settings.  It does NOT own runtime MQTT/network state (see
 *                 transmitter_mqtt_specs.h / transmitter_state.h for those).
 */

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

// Version getters for cache validation
uint32_t get_battery_settings_version();
uint32_t get_power_settings_version();
uint32_t get_can_settings_version();
uint32_t get_contactor_settings_version();

} // namespace TransmitterSettingsCache

#endif // TRANSMITTER_SETTINGS_CACHE_H
