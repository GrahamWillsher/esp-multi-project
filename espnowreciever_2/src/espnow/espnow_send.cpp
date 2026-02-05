#include "espnow_send.h"
#include "../common.h"
#include <esp_now.h>
#include <espnow_packet_utils.h>

bool send_debug_level_control(uint8_t level) {
    // Validate level
    if (level > 7) {
        Serial.printf("[ESP-NOW] Invalid debug level: %d (must be 0-7)\n", level);
        return false;
    }
    
    // Check if transmitter is registered
    bool mac_is_zero = true;
    for (int i = 0; i < 6; i++) {
        if (ESPNow::transmitter_mac[i] != 0) {
            mac_is_zero = false;
            break;
        }
    }
    
    if (mac_is_zero) {
        Serial.println("[ESP-NOW] Transmitter MAC not registered - cannot send debug control");
        return false;
    }
    
    // Build debug_control_t
    debug_control_t packet;
    packet.type = msg_debug_control;
    packet.level = level;
    packet.flags = 0;  // Reserved for future use
    
    // Calculate checksum (simple XOR)
    packet.checksum = 0;
    uint8_t* data = (uint8_t*)&packet;
    for (size_t i = 0; i < sizeof(packet) - 1; i++) {
        packet.checksum ^= data[i];
    }
    
    // Send via ESP-NOW
    esp_err_t result = esp_now_send(ESPNow::transmitter_mac, (uint8_t*)&packet, sizeof(packet));
    
    if (result == ESP_OK) {
        Serial.printf("[ESP-NOW] Debug level control sent: level=%d to %02X:%02X:%02X:%02X:%02X:%02X\n",
                     level,
                     ESPNow::transmitter_mac[0], ESPNow::transmitter_mac[1], ESPNow::transmitter_mac[2],
                     ESPNow::transmitter_mac[3], ESPNow::transmitter_mac[4], ESPNow::transmitter_mac[5]);
        return true;
    } else {
        Serial.printf("[ESP-NOW] Failed to send debug control: %s\n", esp_err_to_name(result));
        return false;
    }
}
