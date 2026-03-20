#ifndef TRANSMITTER_BATTERY_SPEC_SYNC_H
#define TRANSMITTER_BATTERY_SPEC_SYNC_H

#include <ArduinoJson.h>

namespace TransmitterBatterySpecSync {

void store_battery_specs(const JsonObject& specs);

} // namespace TransmitterBatterySpecSync

#endif // TRANSMITTER_BATTERY_SPEC_SYNC_H