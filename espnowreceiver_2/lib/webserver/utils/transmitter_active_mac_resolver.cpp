#include "transmitter_active_mac_resolver.h"

#include "transmitter_identity_cache.h"

namespace ESPNow {
    extern uint8_t transmitter_mac[6];
}

namespace TransmitterActiveMacResolver {

const uint8_t* get_active_mac() {
    const uint8_t* registered_mac = TransmitterIdentityCache::get_registered_mac();
    if (registered_mac != nullptr) {
        return registered_mac;
    }

    for (int i = 0; i < 6; ++i) {
        if (ESPNow::transmitter_mac[i] != 0) {
            return ESPNow::transmitter_mac;
        }
    }

    return nullptr;
}

} // namespace TransmitterActiveMacResolver