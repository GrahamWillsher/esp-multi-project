#include "transmitter_battery_spec_sync.h"

#include <Arduino.h>

#include "../logging.h"
#include "transmitter_settings_cache.h"
#include "transmitter_spec_cache.h"

namespace TransmitterBatterySpecSync {

void store_battery_specs(const JsonObject& specs) {
    TransmitterSpecCache::store_battery_specs(specs);

    if (specs.containsKey("number_of_cells")) {
        uint16_t new_cell_count = specs["number_of_cells"];
        if (new_cell_count > 0) {
            TransmitterSettingsCache::update_battery_cell_count(new_cell_count);
            LOG_INFO("[TX_MGR] Updated battery_settings.cell_count from MQTT: %u", new_cell_count);
        }
    }
}

} // namespace TransmitterBatterySpecSync