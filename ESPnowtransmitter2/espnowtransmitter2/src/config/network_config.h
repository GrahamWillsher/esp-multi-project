#pragma once
#include <IPAddress.h>
#include <ethernet_config.h>  // Common Ethernet configuration

namespace config {
    // Use common Ethernet configuration from esp32common/ethernet_config.h
    namespace ethernet = EthernetConfig::Network;
    
    // Use common NTP configuration from esp32common/ethernet_config.h
    namespace ntp = EthernetConfig::NTP;
    
    // ESP-NOW/WiFi configuration
    namespace network {
        constexpr uint8_t ESPNOW_WIFI_CHANNEL = 1;   // WiFi channel for ESP-NOW
        constexpr const char* NTP_SERVER_1 = "pool.ntp.org";
        constexpr const char* NTP_SERVER_2 = "time.nist.gov";
        constexpr long NTP_GMT_OFFSET_SEC = 0;
        constexpr int NTP_DAYLIGHT_OFFSET_SEC = 0;
    }
    
    // MQTT Configuration
    struct MqttConfig {
        const char* server{"YOUR_MQTT_BROKER_IP"};     // MQTT broker IP/hostname
        uint16_t port{1883};                        // MQTT broker port
        const char* username{"YOUR_MQTT_USERNAME"};          // Username (empty for none)
        const char* password{"YOUR_MQTT_PASSWORD"};      // Password (empty for none)
        const char* client_id{"espnow_transmitter"};
        
        struct Topics {
            const char* data{"espnow/transmitter/data"};      // Topic for battery data
            const char* status{"espnow/transmitter/status"};  // Topic for status
            const char* ota{"espnow/transmitter/ota"};        // Topic for OTA commands
        } topics;
    };
    
    // Static function to get MQTT config (avoids inline variable warning)
    static inline const MqttConfig& get_mqtt_config() {
        static const MqttConfig mqtt;
        return mqtt;
    }
    
    // Feature flags
    namespace features {
        constexpr bool MQTT_ENABLED = true;     // Set to true to enable MQTT publishing
    }
    
} // namespace config
