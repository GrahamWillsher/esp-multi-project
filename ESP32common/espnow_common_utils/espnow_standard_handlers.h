/**
 * @file espnow_standard_handlers.h
 * @brief Standard message handlers for common ESP-NOW messages
 * 
 * Provides reusable handler implementations for standard ESP-NOW protocol
 * messages like PROBE, ACK, etc. Projects can use these directly or wrap
 * them with custom logic.
 */

#pragma once

#include <esp32common/espnow/common.h>
#include <esp_err.h>
#include <functional>

namespace EspnowStandardHandlers {

    struct AckSendStats {
        uint32_t direct_attempts = 0;
        uint32_t direct_success = 0;
        uint32_t direct_no_mem_failures = 0;
        uint32_t direct_other_failures = 0;
    };
    
    /**
     * @brief Callback for connection state changes
     * @param mac MAC address that connected/disconnected
     * @param connected true if connected, false if disconnected
     */
    using ConnectionCallback = std::function<void(const uint8_t* mac, bool connected)>;
    
    /**
     * @brief Callback for PROBE received (called every time)
     * @param mac MAC address of sender
     * @param seq Sequence number from PROBE
     */
    using ProbeReceivedCallback = std::function<void(const uint8_t* mac, uint32_t seq)>;

    /**
     * @brief Callback for discovery ACK send outcome.
     *
     * Called only when a PROBE handler actually attempts to send an ACK.
     * The callback can use this to update throttle state only after success.
     */
    using AckSendResultCallback = std::function<void(const uint8_t* mac,
                                                     uint32_t seq,
                                                     bool success,
                                                     esp_err_t result)>;
    
    /**
     * @brief Configuration for standard PROBE handler
     */
    struct ProbeHandlerConfig {
        ConnectionCallback on_connection;  ///< Called on false->true edge when connection_flag is provided
        ProbeReceivedCallback on_probe_received;  ///< Called every time a PROBE is received
        AckSendResultCallback on_ack_send_result;  ///< Called after ACK send attempt completes
        bool send_ack_response;           ///< true to automatically send ACK response
        volatile bool* connection_flag;   ///< Optional: pointer to connection status flag to update
        uint8_t* peer_mac_storage;        ///< Optional: pointer to 6-byte array to store peer MAC
    };
    
    using AckChannelCallback = std::function<void(uint8_t channel, bool set_wifi_channel)>;

    /**
     * @brief Configuration for standard ACK handler
     */
    struct AckHandlerConfig {
        ConnectionCallback on_connection;  ///< Called on false->true edge when connection_flag is provided
        volatile bool* connection_flag;   ///< Optional: pointer to connection status flag to update
        uint8_t* peer_mac_storage;        ///< Optional: pointer to 6-byte array to store peer MAC
        volatile uint32_t* expected_seq;  ///< Optional: pointer to expected sequence number
        volatile uint8_t* lock_channel;   ///< Optional: legacy pointer to channel lock variable
        AckChannelCallback on_channel_reported;  ///< Optional: shared channel-authority update hook
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
     * @brief Standard DATA message handler with CRC32 validation
     * 
     * Validates CRC32 and calls user callback with validated data.
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
    esp_err_t send_ack_response(const uint8_t* peer_mac, uint32_t seq, uint8_t channel);
    bool read_ack_send_stats(AckSendStats& out_stats);
    void reset_ack_send_stats();
    
} // namespace EspnowStandardHandlers
