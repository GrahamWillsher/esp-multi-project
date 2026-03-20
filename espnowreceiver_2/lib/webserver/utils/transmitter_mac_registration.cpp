#include "transmitter_mac_registration.h"

#include <Arduino.h>

#include "../logging.h"
#include "sse_notifier.h"
#include "transmitter_identity_cache.h"
#include "transmitter_peer_registry.h"

namespace TransmitterMacRegistration {

void register_mac(const uint8_t* transmitter_mac) {
    if (transmitter_mac == nullptr) {
        return;
    }

    TransmitterIdentityCache::register_mac(transmitter_mac);

    const uint8_t* registered_mac = TransmitterIdentityCache::get_registered_mac();
    LOG_INFO("[TX_MGR] MAC registered: %s",
             TransmitterIdentityCache::format_mac(registered_mac).c_str());

    SSENotifier::notifyDataUpdated();
    (void)TransmitterPeerRegistry::ensure_peer_registered(registered_mac);
}

} // namespace TransmitterMacRegistration