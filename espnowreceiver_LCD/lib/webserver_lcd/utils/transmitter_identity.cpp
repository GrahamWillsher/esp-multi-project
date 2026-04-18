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
    char mac_str[kMacStringLength] = {0};
    if (!format_mac(cached, mac_str, sizeof(mac_str))) {
        strncpy(mac_str, "Unknown", sizeof(mac_str) - 1);
        mac_str[sizeof(mac_str) - 1] = '\0';
    }
    LOG_INFO("TX_MGR", "MAC registered: %s", mac_str);

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

bool format_mac(const uint8_t* mac, char* out, size_t out_len) {
    if (out == nullptr || out_len == 0) {
        return false;
    }

    if (mac == nullptr) {
        static constexpr const char* kUnknown = "Unknown";
        if (out_len <= strlen(kUnknown)) {
            out[0] = '\0';
            return false;
        }
        strncpy(out, kUnknown, out_len - 1);
        out[out_len - 1] = '\0';
        return true;
    }

    int written = snprintf(out,
                           out_len,
                           "%02X:%02X:%02X:%02X:%02X:%02X",
                           mac[0],
                           mac[1],
                           mac[2],
                           mac[3],
                           mac[4],
                           mac[5]);
    if (written < 0 || static_cast<size_t>(written) >= out_len) {
        out[0] = '\0';
        return false;
    }

    return true;
}

String format_mac(const uint8_t* mac) {
    char str[kMacStringLength] = {0};
    if (!format_mac(mac, str, sizeof(str))) {
        return String("Unknown");
    }
    return String(str);
}

// ===== Query Helpers =====

bool is_mac_known() {
    return get_active_mac() != nullptr;
}

bool get_mac_string(char* out, size_t out_len) {
    return format_mac(get_active_mac(), out, out_len);
}

String get_mac_string() {
    char str[kMacStringLength] = {0};
    if (!get_mac_string(str, sizeof(str))) {
        return String("Unknown");
    }
    return String(str);
}

} // namespace TransmitterIdentity
