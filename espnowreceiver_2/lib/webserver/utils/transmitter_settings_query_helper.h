#ifndef TRANSMITTER_SETTINGS_QUERY_HELPER_H
#define TRANSMITTER_SETTINGS_QUERY_HELPER_H

#include "transmitter_settings_types.h"

namespace TransmitterSettingsQueryHelper {

    BatterySettings get_battery_settings();
    bool has_battery_settings();

    BatteryEmulatorSettings get_battery_emulator_settings();
    bool has_battery_emulator_settings();

    PowerSettings get_power_settings();
    bool has_power_settings();

    InverterSettings get_inverter_settings();
    bool has_inverter_settings();

    CanSettings get_can_settings();
    bool has_can_settings();

    ContactorSettings get_contactor_settings();
    bool has_contactor_settings();

} // namespace TransmitterSettingsQueryHelper

#endif // TRANSMITTER_SETTINGS_QUERY_HELPER_H
