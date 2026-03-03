/**
 * Battery Emulator Data Handlers - Core Helpers
 *
 * Contains shared helper functions and component config forwarding.
 * Domain-specific handlers are split into src/espnow/handlers/*.cpp
 */

#include "battery_handlers.h"
#include "component_config_handler.h"

bool validate_checksum(const void* data, size_t len) {
    const uint8_t* bytes = (const uint8_t*)data;
    uint16_t calculated_sum = 0;

    for (size_t i = 0; i < len - 2; i++) {
        calculated_sum += bytes[i];
    }

    uint16_t message_checksum = bytes[len - 2] | (bytes[len - 1] << 8);
    return (calculated_sum == message_checksum);
}

void handle_component_config(const espnow_queue_msg_t* msg) {
    ComponentConfigHandler::instance().handle_message(msg->data, msg->len);
}