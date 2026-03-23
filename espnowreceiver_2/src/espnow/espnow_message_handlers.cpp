// ESP-NOW data, packet, LED, and debug message handler implementations.
// Companion translation unit to espnow_tasks.cpp.
// See espnow_tasks_internal.h for shared declarations.

#include "espnow_tasks_internal.h"
#include "rx_state_machine.h"
#include "../common.h"
#include "../display/display_led.h"
#include <esp32common/espnow/packet_utils.h>

static constexpr const char* kLogTag = "ESPNOW";

void handle_flash_led_message(const espnow_queue_msg_t* msg) {
    if (msg->len >= (int)sizeof(flash_led_t)) {
        const flash_led_t* flash_msg = reinterpret_cast<const flash_led_t*>(msg->data);
        
        // Validate color code (wire format matches enum: 0=RED, 1=GREEN, 2=ORANGE, 3=BLUE)
        if (flash_msg->color > LED_BLUE) {
            LOG_WARN(kLogTag, "Invalid LED color code: %d", flash_msg->color);
            return;
        }

        // Validate effect code (0=CONTINUOUS, 1=FLASH, 2=HEARTBEAT)
        if (flash_msg->effect > LED_EFFECT_HEARTBEAT) {
            LOG_WARN(kLogTag, "Invalid LED effect code: %d", flash_msg->effect);
            return;
        }
        
        LEDColor color = static_cast<LEDColor>(flash_msg->color);
        LEDEffect effect = static_cast<LEDEffect>(flash_msg->effect);
        
        static const char* color_names[] = {"RED", "GREEN", "ORANGE", "BLUE"};
        static const char* effect_names[] = {"CONTINUOUS", "FLASH", "HEARTBEAT"};
        LOG_DEBUG(kLogTag, "LED request: color=%d (%s), effect=%d (%s)",
                 flash_msg->color,
                 color_names[flash_msg->color],
                 flash_msg->effect,
                 effect_names[flash_msg->effect]);
        
        // Store the current LED color for status indicator task to use
        ESPNow::current_led_color = color;
        ESPNow::current_led_effect = effect;
    }
}

void handle_debug_ack_message(const espnow_queue_msg_t* msg) {
    if (msg->len >= (int)sizeof(debug_ack_t)) {
        const debug_ack_t* ack = reinterpret_cast<const debug_ack_t*>(msg->data);
        
        static const char* level_names[] = {"EMERG", "ALERT", "CRIT", "ERROR", "WARNING", "NOTICE", "INFO", "DEBUG"};
        static const char* status_names[] = {"Success", "Invalid level", "Error"};
        
        LOG_INFO(kLogTag, "Debug ACK received: applied=%s (%d), previous=%s (%d), status=%s",
                 ack->applied <= 7 ? level_names[ack->applied] : "UNKNOWN",
                 ack->applied,
                 ack->previous <= 7 ? level_names[ack->previous] : "UNKNOWN",
                 ack->previous,
                 ack->status <= 2 ? status_names[ack->status] : "UNKNOWN");
        
        if (ack->status != 0) {
            LOG_WARN(kLogTag, "Transmitter reported error changing debug level");
        }
    } else {
        LOG_WARN(kLogTag, "Debug ACK packet too short: %d bytes (expected %d)", 
                 msg->len, sizeof(debug_ack_t));
    }
}

void handle_data_message(const espnow_queue_msg_t* msg) {
    if (msg->len >= (int)sizeof(espnow_payload_t)) {
        const espnow_payload_t* payload = reinterpret_cast<const espnow_payload_t*>(msg->data);
        
        uint16_t calc_checksum = payload->soc + (uint16_t)payload->power;
        
        if (calc_checksum == payload->checksum) {
            // Mark RxStateMachine as active when actual data arrives (not just keep-alive)
            RxStateMachine::instance().on_activity();

            apply_telemetry_sample(msg->mac, payload->soc, payload->power, "DATA", true);
        } else {
            // CRC mismatch
            LOG_WARN(kLogTag, "CRC failed: expected 0x%04X, got 0x%04X", 
                         calc_checksum, payload->checksum);
        }
    } else {
        LOG_WARN(kLogTag, "DATA packet too short: %d bytes (expected %d)",
                 msg->len, sizeof(espnow_payload_t));
    }
}

void handle_packet_events(const espnow_queue_msg_t* msg) {
    EspnowPacketUtils::PacketInfo info;
    if (!EspnowPacketUtils::get_packet_info(msg, info)) {
        LOG_WARN(kLogTag, "Invalid packet structure");
        return;
    }
    
    LOG_DEBUG(kLogTag, "PACKET/EVENTS: seq=%u, frag=%u/%u, len=%u, checksum=0x%04X",
              info.seq, info.frag_index, info.frag_total, info.payload_len, info.checksum);

    if (info.payload_len >= 5) {
        uint8_t soc = info.payload[0];
        int32_t power;
        memcpy(&power, &info.payload[1], sizeof(int32_t));

        apply_telemetry_sample(msg->mac, soc, power, "EVENTS", false);
    }
}

void handle_packet_logs(const espnow_queue_msg_t* msg) {
    EspnowPacketUtils::PacketInfo info;
    if (EspnowPacketUtils::get_packet_info(msg, info)) {
        LOG_DEBUG(kLogTag, "PACKET/LOGS: seq=%u, frag=%u/%u, len=%u, checksum=0x%04X",
                  info.seq, info.frag_index, info.frag_total, info.payload_len, info.checksum);
    }
}

void handle_packet_cell_info(const espnow_queue_msg_t* msg) {
    EspnowPacketUtils::PacketInfo info;
    if (EspnowPacketUtils::get_packet_info(msg, info)) {
        LOG_DEBUG(kLogTag, "PACKET/CELL_INFO: seq=%u, frag=%u/%u, len=%u, checksum=0x%04X",
                  info.seq, info.frag_index, info.frag_total, info.payload_len, info.checksum);
    }
}

void handle_packet_unknown(const espnow_queue_msg_t* msg, uint8_t subtype) {
    EspnowPacketUtils::PacketInfo info;
    if (EspnowPacketUtils::get_packet_info(msg, info)) {
        LOG_WARN(kLogTag, "PACKET/UNKNOWN: subtype=%u, seq=%u", 
                     subtype, info.seq);
    }
}
