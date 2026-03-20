#pragma once

#include <espnow_transmitter.h>
#include <cstddef>

namespace TxConfigHandlerCommon {

/**
 * @brief Validate common ESP-NOW config message preconditions.
 *
 * Checks packet length, connection state, and optionally copies sender MAC.
 *
 * @param msg Incoming ESP-NOW message
 * @param min_len Required minimum payload length
 * @param log_tag Logging tag (e.g. "NET_CFG", "MQTT_CFG")
 * @param operation Human-readable operation name for logs
 * @param receiver_mac Optional output buffer (6 bytes) to copy sender MAC
 * @return true if preconditions pass
 */
bool validate_connected_message(const espnow_queue_msg_t& msg,
                                size_t min_len,
                                const char* log_tag,
                                const char* operation,
                                uint8_t* receiver_mac = nullptr);

/**
 * @brief Ensure receiver peer exists before sending response packets.
 *
 * @param receiver_mac Receiver MAC address
 * @param log_tag Logging tag (e.g. "NET_CFG", "MQTT_CFG")
 * @return true if peer is registered or successfully added
 */
bool ensure_peer_registered(const uint8_t* receiver_mac, const char* log_tag);

} // namespace TxConfigHandlerCommon
