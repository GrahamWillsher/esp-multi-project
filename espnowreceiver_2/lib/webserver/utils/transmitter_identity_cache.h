#ifndef TRANSMITTER_IDENTITY_CACHE_H
#define TRANSMITTER_IDENTITY_CACHE_H

#include <Arduino.h>

namespace TransmitterIdentityCache {

void register_mac(const uint8_t* mac);
const uint8_t* get_registered_mac();
bool has_registered_mac();
String format_mac(const uint8_t* mac);

} // namespace TransmitterIdentityCache

#endif // TRANSMITTER_IDENTITY_CACHE_H
