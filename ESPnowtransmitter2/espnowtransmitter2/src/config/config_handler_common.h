#pragma once

#include <esp32common/contracts/incoming_message.h>
#include <cstddef>

namespace TxConfigHandlerCommon {

/**
 * @brief Validate common config message preconditions.
 *
 * Checks packet length, connection state, and optionally copies sender MAC.
 *
 * @param msg Incoming message envelope
 * @param min_len Required minimum payload length
 * @param log_tag Logging tag (e.g. "NET_CFG", "MQTT_CFG")
 * @param operation Human-readable operation name for logs
 * @param receiver_mac Optional output buffer (6 bytes) to copy sender MAC
 * @return true if preconditions pass
 */
bool validate_connected_message(const incoming_msg_t& msg,
                                size_t min_len,
                                const char* log_tag,
                                const char* operation,
                                uint8_t* receiver_mac = nullptr);

} // namespace TxConfigHandlerCommon
