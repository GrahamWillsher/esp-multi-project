#include "transmitter_identity.h"

#include <string.h>
#include "../logging.h"
#include "sse_notifier.h"
#include "transmitter_peer_registry.h"

namespace {
    uint8_t registered_mac[6] = {0};
    bool registered_mac_known = false;
}

namespace ESPNow {
    extern uint8_t transmitter_mac[6];
}

namespace TransmitterIdentity {

// ===== Registration =====

void register_mac(const uint8_t* transmitter_mac) {
    if (transmitter_mac == nullptr) {
        return;
    }

    cache_mac(transmitter_mac);

    const uint8_t* cached = get_registered_mac();
    LOG_INFO("[TX_MGR] MAC registered: %s", format_mac(cached).c_str());

    SSENotifier::notifyDataUpdated();
    (void)TransmitterPeerRegistry::ensure_peer_registered(cached);
}

// ===== Cache Management =====

void cache_mac(const uint8_t* mac) {
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

// ===== Resolution =====

const uint8_t* get_active_mac() {
    const uint8_t* reg_mac = get_registered_mac();
    if (reg_mac != nullptr) {
        return reg_mac;
    }

    // Fall back to runtime ESP-NOW MAC
    for (int i = 0; i < 6; ++i) {
        if (ESPNow::transmitter_mac[i] != 0) {
            return ESPNow::transmitter_mac;
        }
    }

    return nullptr;
}

// ===== Formatting =====

String format_mac(const uint8_t* mac) {
    if (mac == nullptr) return "Unknown";

    char str[18];
    snprintf(str, sizeof(str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(str);
}

// ===== Query Helpers =====

bool is_mac_known() {
    return get_active_mac() != nullptr;
}

String get_mac_string() {
    return format_mac(get_active_mac());
}

} // namespace TransmitterIdentity
