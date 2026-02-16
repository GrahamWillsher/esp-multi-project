/**
 * @file connection_event.h
 * @brief ESP-NOW Connection Event Types (Common - Used by Both TX and RX)
 * 
 * This file defines the generic event types and state machine used by both
 * transmitter and receiver. No device-specific code here.
 * 
 * Architecture: Event-driven state machine
 * - Events posted from callbacks
 * - Central manager processes events
 * - State transitions are deterministic
 */

#pragma once

#include <cstdint>
#include <cstring>

/**
 * @enum EspNowEvent
 * @brief Generic events that drive state transitions
 * 
 * Both transmitter and receiver use these events:
 * - CONNECTION_START: Begin connection process
 * - PEER_FOUND: Peer discovered/received (TX: got ACK, RX: got PROBE)
 * - PEER_REGISTERED: Peer added to ESP-NOW
 * - DATA_RECEIVED: Data packet from peer
 * - CONNECTION_LOST: No communication timeout
 * - RESET_CONNECTION: Manual reset to IDLE
 */
enum class EspNowEvent : uint8_t {
    CONNECTION_START = 0,    // Start connection (generic trigger)
    PEER_FOUND = 1,         // Peer discovered (TX: ACK, RX: PROBE)
    PEER_REGISTERED = 2,    // Peer added to ESP-NOW
    DATA_RECEIVED = 3,      // Data from peer
    CONNECTION_LOST = 4,    // Timeout detected
    RESET_CONNECTION = 5    // Manual reset
};

/**
 * @enum EspNowConnectionState
 * @brief 3-state machine - Simple, deterministic, reliable
 * 
 * IDLE (0)
 *   ↓ [CONNECTION_START or PEER_FOUND]
 * CONNECTING (1) - Waiting for peer registration
 *   ↓ [PEER_REGISTERED]
 * CONNECTED (2) - Ready to send/receive
 *   ↓ [CONNECTION_LOST or RESET_CONNECTION]
 * IDLE (back to start)
 */
enum class EspNowConnectionState : uint8_t {
    IDLE = 0,        // Not connected
    CONNECTING = 1,  // Discovery in progress or peer being registered
    CONNECTED = 2    // Peer registered, ready for data
};

/**
 * @struct EspNowStateChange
 * @brief Event data structure
 * 
 * Posted to event queue when state transition events occur.
 * Uses fixed-size arrays to avoid dynamic allocation.
 */
struct EspNowStateChange {
    EspNowEvent event;          // Event type
    uint8_t peer_mac[6];        // Peer MAC address (optional, depends on event)
    uint32_t timestamp;         // When event occurred (ms since boot)
    
    // Constructor for easier initialization
    EspNowStateChange() : event(EspNowEvent::CONNECTION_START), timestamp(0) {
        memset(peer_mac, 0, 6);
    }
    
    EspNowStateChange(EspNowEvent e, const uint8_t* mac = nullptr) 
        : event(e), timestamp(0) {
        if (mac) {
            memcpy(peer_mac, mac, 6);
        } else {
            memset(peer_mac, 0, 6);
        }
    }
};

/**
 * @brief Convert state enum to human-readable string
 * @param state The state to convert
 * @return Pointer to static string (do not free)
 */
inline const char* state_to_string(EspNowConnectionState state) {
    switch (state) {
        case EspNowConnectionState::IDLE:
            return "IDLE";
        case EspNowConnectionState::CONNECTING:
            return "CONNECTING";
        case EspNowConnectionState::CONNECTED:
            return "CONNECTED";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief Convert event enum to human-readable string
 * @param event The event to convert
 * @return Pointer to static string (do not free)
 */
inline const char* event_to_string(EspNowEvent event) {
    switch (event) {
        case EspNowEvent::CONNECTION_START:
            return "CONNECTION_START";
        case EspNowEvent::PEER_FOUND:
            return "PEER_FOUND";
        case EspNowEvent::PEER_REGISTERED:
            return "PEER_REGISTERED";
        case EspNowEvent::DATA_RECEIVED:
            return "DATA_RECEIVED";
        case EspNowEvent::CONNECTION_LOST:
            return "CONNECTION_LOST";
        case EspNowEvent::RESET_CONNECTION:
            return "RESET_CONNECTION";
        default:
            return "UNKNOWN";
    }
}
