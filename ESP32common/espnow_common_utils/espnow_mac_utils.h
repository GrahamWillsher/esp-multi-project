/**
 * @file espnow_mac_utils.h
 * @brief Shared MAC-address utility predicates for ESP-NOW connection handlers
 *
 * Contains simple inline helpers that are needed identically by both the
 * transmitter (tx_connection_handler) and receiver (rx_connection_handler).
 * Extracting them here eliminates the duplicate static functions that
 * previously lived in each file (Section 4.1 of the whole-codebase review).
 *
 * These functions carry NO state and have no device-specific logic.
 *
 * Public include path: <esp32common/espnow/mac_utils.h>
 */

#pragma once

#include <cstdint>

namespace EspNowMacUtils {

/**
 * @brief Return true if the given 6-byte MAC address is non-zero.
 *
 * A MAC of all-zeroes is used as a sentinel "no peer known" value
 * throughout both the TX and RX connection handlers.
 *
 * @param mac Pointer to a 6-byte MAC address buffer; nullptr is treated as invalid.
 * @return true  if at least one byte is non-zero
 * @return false if mac is nullptr or all bytes are 0x00
 */
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

/**
 * @brief Return true if the given 6-byte MAC address is the broadcast address.
 *
 * The ESP-NOW broadcast address is FF:FF:FF:FF:FF:FF.  Both connection
 * handlers skip peer-removal logic for the broadcast MAC when cleaning up
 * on connection loss.
 *
 * @param mac Pointer to a 6-byte MAC address buffer; nullptr returns false.
 * @return true  if all six bytes are 0xFF
 * @return false if mac is nullptr or any byte differs from 0xFF
 */
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

} // namespace EspNowMacUtils
