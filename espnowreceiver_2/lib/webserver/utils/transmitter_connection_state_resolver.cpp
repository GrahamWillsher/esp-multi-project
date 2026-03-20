#include "transmitter_connection_state_resolver.h"

#include "transmitter_active_mac_resolver.h"
#include "transmitter_status_cache.h"

namespace TransmitterConnectionStateResolver {

bool is_transmitter_connected() {
    // Connection manager reports live link state; also require known transmitter identity.
    return TransmitterStatusCache::is_transmitter_connected() &&
           (TransmitterActiveMacResolver::get_active_mac() != nullptr);
}

} // namespace TransmitterConnectionStateResolver
