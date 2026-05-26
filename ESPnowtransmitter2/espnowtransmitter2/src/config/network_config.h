#pragma once
#include <IPAddress.h>
#include <ethernet_config.h>  // Common Ethernet configuration

namespace config {
    // Use common Ethernet configuration from esp32common/ethernet_config.h
    namespace ethernet = EthernetConfig::Network;
    
    // Use common NTP configuration from esp32common/ethernet_config.h
    namespace ntp = EthernetConfig::NTP;
    
    // Time/network defaults
    namespace network {
        constexpr const char* NTP_SERVER_1 = "pool.ntp.org";
        constexpr const char* NTP_SERVER_2 = "time.nist.gov";
        constexpr long NTP_GMT_OFFSET_SEC = 0;
        constexpr int NTP_DAYLIGHT_OFFSET_SEC = 0;
    }
    
    // MQTT Configuration
    struct MqttConfig {
        const char* server{"192.168.1.221"};     // MQTT broker IP/hostname
        uint16_t port{1883};                        // MQTT broker port
        const char* username{"Aintree34"};          // Username (empty for none)
        const char* password{"Shanghai17"};      // Password (empty for none)
        const char* client_id{"battery_emulator_transmitter"};
        
        struct Topics {
            const char* data{"batt-emu/mqtt-v1/tx/state/battery_live"};      // Topic for battery data
            const char* status{"batt-emu/mqtt-v1/tx/state/presence"};         // Topic for status
            const char* ota{"batt-emu/mqtt-v1/rx/cmd/control/ota_start"};     // Topic for OTA commands
        } topics;
    };

    // OTA authentication/security configuration
    namespace security {
        // Pre-shared key used for OTA session signature verification.
        // Must match receiver-side signer and should be rotated during provisioning.
        constexpr const char* OTA_PSK = "CHANGE_ME_OTA_PSK_32B_MIN";
        constexpr uint32_t OTA_SESSION_TTL_MS = 120000;      // 120 seconds
        constexpr uint8_t OTA_SESSION_MAX_ATTEMPTS = 3;
    }
    
    // Static function to get MQTT config (avoids inline variable warning)
    static inline const MqttConfig& get_mqtt_config() {
        static const MqttConfig mqtt;
        return mqtt;
    }
    
    // Feature flags
    namespace features {
        constexpr bool MQTT_ENABLED = true;     // Set to true to enable MQTT publishing
        constexpr bool CAN_ENABLED = true;      // Set to true to enable CAN driver (GPIO 4 MISO)
        constexpr bool BATTERY_EMULATOR_ENABLED = true;  // Set to true to enable Battery Emulator integration
    }
    
} // namespace config
