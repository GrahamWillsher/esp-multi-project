#ifndef TRANSMITTER_SPEC_CACHE_H
#define TRANSMITTER_SPEC_CACHE_H

#include <Arduino.h>
#include <ArduinoJson.h>

namespace TransmitterSpecCache {

void store_static_specs(const JsonObject& specs);
void store_battery_specs(const JsonObject& specs);
void store_inverter_specs(const JsonObject& specs);
void store_charger_specs(const JsonObject& specs);
void store_system_specs(const JsonObject& specs);

bool has_static_specs();
String get_static_specs_json();
String get_battery_specs_json();
String get_inverter_specs_json();
String get_charger_specs_json();
String get_system_specs_json();

} // namespace TransmitterSpecCache

#endif // TRANSMITTER_SPEC_CACHE_H
