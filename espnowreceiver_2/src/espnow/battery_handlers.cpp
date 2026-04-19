/**
 * Battery Emulator Data Handlers - Core Helpers
 *
 * Contains shared helper functions and component config forwarding.
 * Domain-specific handlers are split into src/espnow/handlers/*.cpp
 */

#include "battery_handlers.h"
#include "component_config_handler.h"
#include <esp32common/espnow/packet_utils.h>
#include <cstring>

bool validate_checksum(const void* data, size_t len) {
    if (!data || len < sizeof(uint32_t)) {
        return false;
    }
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
    uint32_t stored = 0;
    memcpy(&stored, bytes + len - sizeof(uint32_t), sizeof(stored));
    return EspnowPacketUtils::crc32_packet(bytes, len - sizeof(uint32_t)) == stored;
}

void handle_component_config(const espnow_queue_msg_t* msg) {
    ComponentConfigHandler::instance().handle_message(msg->data, msg->len);
}