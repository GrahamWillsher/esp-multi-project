#pragma once

#include <cstdint>

namespace RuntimeMacUtils {

inline bool has_valid_mac(const uint8_t* mac) {
    if (!mac) {
        return false;
    }
    for (int i = 0; i < 6; ++i) {
        if (mac[i] != 0) {
            return true;
        }
    }
    return false;
}

inline bool is_broadcast_mac(const uint8_t* mac) {
    if (!mac) {
        return false;
    }
    for (int i = 0; i < 6; ++i) {
        if (mac[i] != 0xFF) {
            return false;
        }
    }
    return true;
}

}  // namespace RuntimeMacUtils
