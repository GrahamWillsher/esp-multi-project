#include "transmitter_settings_store_workflow.h"

#include <Arduino.h>

#include "../logging.h"
#include "transmitter_settings_cache.h"
#include "transmitter_write_through.h"

namespace TransmitterSettingsStoreWorkflow {

void store_battery_settings(const BatterySettings& settings) {
    TransmitterSettingsCache::store_battery_settings(settings);

    LOG_INFO("[TX_MGR] Battery settings stored: %uWh, %uS, %umV-%umV",
             settings.capacity_wh, settings.cell_count,
             settings.min_voltage_mv, settings.max_voltage_mv);

    TransmitterWriteThrough::persist_to_nvs();
}

void store_battery_emulator_settings(const BatteryEmulatorSettings& settings) {
    TransmitterSettingsCache::store_battery_emulator_settings(settings);
    TransmitterWriteThrough::persist_to_nvs();
}

void store_power_settings(const PowerSettings& settings) {
    TransmitterSettingsCache::store_power_settings(settings);
    TransmitterWriteThrough::persist_to_nvs();
}

void store_inverter_settings(const InverterSettings& settings) {
    TransmitterSettingsCache::store_inverter_settings(settings);
    TransmitterWriteThrough::persist_to_nvs();
}

void store_can_settings(const CanSettings& settings) {
    TransmitterSettingsCache::store_can_settings(settings);
    TransmitterWriteThrough::persist_to_nvs();
}

void store_contactor_settings(const ContactorSettings& settings) {
    TransmitterSettingsCache::store_contactor_settings(settings);
    TransmitterWriteThrough::persist_to_nvs();
}

} // namespace TransmitterSettingsStoreWorkflow
