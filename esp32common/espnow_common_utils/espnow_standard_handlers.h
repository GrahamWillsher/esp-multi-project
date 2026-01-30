/**
 * @file espnow_standard_handlers.h
 * @brief Standard message handlers for common ESP-NOW messages
 * 
 * Provides reusable handler implementations for standard ESP-NOW protocol
 * messages like PROBE, ACK, etc. Projects can use these directly or wrap
 * them with custom logic.
 */

#pragma once

#include <espnow_common.h>
#include <functional>

namespace EspnowStandardHandlers {
    
    /**
     * @brief Callback for connection state changes
     * @param mac MAC address that connected/disconnected
     * @param connected true if connected, false if disconnected
     */
    using ConnectionCallback = std::function<void(const uint8_t* mac, bool connected)>;
    
    /**
     * @brief Configuration for standard PROBE handler
     */
    struct ProbeHandlerConfig {
        ConnectionCallback on_connection;  ///< Called when peer connects
        bool send_ack_response;           ///< true to automatically send ACK response
        volatile bool* connection_flag;   ///< Optional: pointer to connection status flag to update
        uint8_t* peer_mac_storage;        ///< Optional: pointer to 6-byte array to store peer MAC
    };
    
    /**
     * @brief Configuration for standard ACK handler
     */
    struct AckHandlerConfig {
        ConnectionCallback on_connection;  ///< Called when peer connects
        volatile bool* connection_flag;   ///< Optional: pointer to connection status flag to update
        uint8_t* peer_mac_storage;        ///< Optional: pointer to 6-byte array to store peer MAC
        volatile uint32_t* expected_seq;  ///< Optional: pointer to expected sequence number
        volatile uint8_t* lock_channel;   ///< Optional: pointer to channel lock variable
        volatile bool* ack_received_flag; ///< Optional: pointer to ACK received flag (for discovery)
        bool set_wifi_channel;            ///< true to automatically set WiFi to received channel
    };
    
    /**
     * @brief Standard PROBE message handler
     * 
     * Handles incoming PROBE announcements:
     * - Adds peer if not already registered
     * - Sends ACK response (if configured)
     * - Updates connection flag
     * - Calls connection callback
     * 
     * @param msg Incoming message
     * @param context Pointer to ProbeHandlerConfig
     */
    void handle_probe(const espnow_queue_msg_t* msg, void* context);
    
    /**
     * @brief Standard ACK message handler
     * 
     * Handles incoming ACK responses:
     * - Validates sequence number (if configured)
     * - Updates channel lock (if configured)
     * - Updates connection flag
     * - Calls connection callback
     * 
     * @param msg Incoming message
     * @param context Pointer to AckHandlerConfig
     */
    void handle_ack(const espnow_queue_msg_t* msg, void* context);
    
    /**
     * @brief Standard DATA message handler with checksum validation
     * 
     * Validates checksum and calls user callback with validated data.
     * 
     * @param msg Incoming message
     * @param context Pointer to std::function<void(const espnow_payload_t*)>
     */
    void handle_data(const espnow_queue_msg_t* msg, void* context);
    
    /**
     * @brief Helper: Send ACK response to peer
     * @param peer_mac MAC address to send to
     * @param seq Sequence number to echo back
     * @param channel Current WiFi channel
     * @return true if send successful
     */
    bool send_ack_response(const uint8_t* peer_mac, uint32_t seq, uint8_t channel);
    
    /**
     * @brief Helper: Send PROBE announcement
     * @param seq Sequence number for this probe
     * @return true if send successful
     */
    bool send_probe_announcement(uint32_t seq);
    
} // namespace EspnowStandardHandlers
