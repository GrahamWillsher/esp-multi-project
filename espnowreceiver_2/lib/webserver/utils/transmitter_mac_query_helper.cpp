#include "transmitter_mac_query_helper.h"

#include "transmitter_active_mac_resolver.h"
#include "transmitter_identity_cache.h"

namespace TransmitterMacQueryHelper {

const uint8_t* get_active_mac() {
    return TransmitterActiveMacResolver::get_active_mac();
}

bool is_mac_known() {
    return get_active_mac() != nullptr;
}

String get_mac_string() {
    return TransmitterIdentityCache::format_mac(get_active_mac());
}

} // namespace TransmitterMacQueryHelper
