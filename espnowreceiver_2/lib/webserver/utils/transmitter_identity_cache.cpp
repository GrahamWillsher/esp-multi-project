#include "transmitter_identity_cache.h"

#include <string.h>

namespace {
    uint8_t registered_mac[6] = {0};
    bool registered_mac_known = false;
}

namespace TransmitterIdentityCache {

void register_mac(const uint8_t* mac) {
    if (mac == nullptr) return;
    memcpy(registered_mac, mac, sizeof(registered_mac));
    registered_mac_known = true;
}

const uint8_t* get_registered_mac() {
    return registered_mac_known ? registered_mac : nullptr;
}

bool has_registered_mac() {
    return registered_mac_known;
}

String format_mac(const uint8_t* mac) {
    if (mac == nullptr) return "Unknown";

    char str[18];
    snprintf(str, sizeof(str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(str);
}

} // namespace TransmitterIdentityCache
