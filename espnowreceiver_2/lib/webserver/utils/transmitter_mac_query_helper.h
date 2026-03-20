#ifndef TRANSMITTER_MAC_QUERY_HELPER_H
#define TRANSMITTER_MAC_QUERY_HELPER_H

#include <Arduino.h>
#include <stdint.h>

namespace TransmitterMacQueryHelper {

const uint8_t* get_active_mac();
bool is_mac_known();
String get_mac_string();

} // namespace TransmitterMacQueryHelper

#endif // TRANSMITTER_MAC_QUERY_HELPER_H
