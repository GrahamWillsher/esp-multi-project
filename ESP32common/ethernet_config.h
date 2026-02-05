#ifndef ETHERNET_CONFIG_H
#define ETHERNET_CONFIG_H

#include <Arduino.h>
#include <IPAddress.h>

// ============================================================================
// COMMON ETHERNET CONFIGURATION
// Shared across ESP32-POE projects (transmitter, receiver, etc.)
// ============================================================================

namespace EthernetConfig {
    // Hardware configuration for Olimex ESP32-POE-ISO (WROVER variant)
    namespace Hardware {
        constexpr uint8_t PHY_ADDR = 0;
        constexpr int8_t POWER_PIN = 12;
        constexpr uint8_t MDC_PIN = 23;
        constexpr uint8_t MDIO_PIN = 18;
        
        // Clock mode - CRITICAL for WROVER boards!
        constexpr uint8_t CLK_MODE = ETH_CLOCK_GPIO0_OUT;
        constexpr uint8_t PHY_TYPE = ETH_PHY_LAN8720;
    }
    
    // Network configuration
    namespace Network {
        // Static IP mode toggle
        constexpr bool USE_STATIC_IP = false;  // Set to true for static IP
        
        // Static IP settings (only used if USE_STATIC_IP is true)
        const IPAddress STATIC_IP(192, 168, 1, 100);  // Change to your desired static IP
        const IPAddress GATEWAY(192, 168, 1, 1);      // Change to your network gateway
        const IPAddress SUBNET(255, 255, 255, 0);
        const IPAddress DNS(192, 168, 1, 1);          // Change to your DNS server
    }
    
    // NTP Time synchronization
    namespace NTP {
        constexpr const char* SERVER_1 = "pool.ntp.org";
        constexpr const char* SERVER_2 = "time.nist.gov";
        constexpr long GMT_OFFSET_SEC = 0;           // Adjust for your timezone
        constexpr int DAYLIGHT_OFFSET_SEC = 0;       // Adjust for daylight saving
    }
}

#endif // ETHERNET_CONFIG_H
