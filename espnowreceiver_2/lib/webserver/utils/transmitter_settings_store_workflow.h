#ifndef TRANSMITTER_SETTINGS_STORE_WORKFLOW_H
#define TRANSMITTER_SETTINGS_STORE_WORKFLOW_H

#include "transmitter_settings_types.h"

namespace TransmitterSettingsStoreWorkflow {

void store_battery_settings(const BatterySettings& settings);
void store_battery_emulator_settings(const BatteryEmulatorSettings& settings);
void store_power_settings(const PowerSettings& settings);
void store_inverter_settings(const InverterSettings& settings);
void store_can_settings(const CanSettings& settings);
void store_contactor_settings(const ContactorSettings& settings);

} // namespace TransmitterSettingsStoreWorkflow

#endif // TRANSMITTER_SETTINGS_STORE_WORKFLOW_H
