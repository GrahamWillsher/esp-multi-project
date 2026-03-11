/**
 * @file espnow_device_state.h
 * @brief Shared ESP-NOW device state enum — used by BOTH transmitter and receiver.
 *
 * This is the single source of truth for connection state vocabulary across the
 * cross-device ESP-NOW system.  Both TxStateMachine and RxStateMachine use this
 * enum as their ConnectionState type, and it is encoded in the heartbeat wire
 * format (heartbeat_t.state and heartbeat_ack_t.state) so each device can read
 * the peer's real application state from every heartbeat exchange.
 *
 * State map:
 *
 *   TX                          RX
 *   DISCONNECTED (boot)         DISCONNECTED (no transmitter seen)
 *      ↓ discovery starts
 *   DISCOVERING (hopping)       — (TX only, RX stays DISCONNECTED)
 *      ↓ peer found + registered
 *   CONNECTED (link up)         CONNECTED (peer registered)
 *      ↓ REQUEST_DATA received  ↓ first battery_status arrives
 *   ACTIVE (transmitting)       ACTIVE (receiving data)
 *      ↓ heartbeat timeout / data aged out
 *   STALE                       STALE
 *      ↓ reconnect event posted
 *   RECONNECTING                RECONNECTING (if connection fully lost)
 *      ↓ repeated failure
 *   FAILED                      — (TX only in current implementation)
 */

#pragma once
#include <cstdint>

enum class EspNowDeviceState : uint8_t {
    DISCONNECTED = 0,  ///< No peer. TX: pre-discovery. RX: no transmitter seen.
    DISCOVERING  = 1,  ///< TX only: actively channel-hopping for a receiver.
    CONNECTED    = 2,  ///< Peer registered; link established, no data flowing yet.
    ACTIVE       = 3,  ///< Data is flowing. TX: transmitting. RX: receiving.
    STALE        = 4,  ///< Link up but data aged out / heartbeat overdue.
    RECONNECTING = 5,  ///< Connection lost; attempting to re-establish.
    FAILED       = 6,  ///< Persistent failure; needs attention.
};

inline const char* espnow_device_state_to_string(EspNowDeviceState s) {
    switch (s) {
        case EspNowDeviceState::DISCONNECTED:  return "DISCONNECTED";
        case EspNowDeviceState::DISCOVERING:   return "DISCOVERING";
        case EspNowDeviceState::CONNECTED:     return "CONNECTED";
        case EspNowDeviceState::ACTIVE:        return "ACTIVE";
        case EspNowDeviceState::STALE:         return "STALE";
        case EspNowDeviceState::RECONNECTING:  return "RECONNECTING";
        case EspNowDeviceState::FAILED:        return "FAILED";
        default:                               return "UNKNOWN";
    }
}
