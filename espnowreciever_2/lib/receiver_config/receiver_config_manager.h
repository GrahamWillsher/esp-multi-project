#ifndef RECEIVER_NETWORK_CONFIG_H
#define RECEIVER_NETWORK_CONFIG_H

#include <Arduino.h>
#include <Preferences.h>

/**
 * @brief Manages Receiver Network Configuration Storage
 * 
 * This class handles receiver network settings including:
 * - WiFi credentials (SSID, password)
 * - Network mode (DHCP vs Static IP)
 * - Static IP configuration (IP, gateway, subnet, DNS)
 * - Hostname for mDNS
 * 
 * Configuration is persisted in NVS using uint8_t[4] arrays for IP addresses.
 * 
 * Storage Pattern:
 * - All IP addresses stored as uint8_t[4] arrays (NOT strings)
 * - No hardcoded defaults - device boots to AP mode if no config exists
 * - AP mode fallback on connection failure (30-second timeout)
 */
class ReceiverNetworkConfig {
private:
    // WiFi credentials
    static char hostname_[32];
    static char ssid_[32];
    static char password_[64];
    
    // Network mode
    static bool use_static_ip_;
    
    // Static IP configuration (stored as arrays, not IPAddress objects)
    static uint8_t static_ip_[4];
    static uint8_t gateway_[4];
    static uint8_t subnet_[4];
    static uint8_t dns_primary_[4];
    static uint8_t dns_secondary_[4];
    
    static constexpr const char* NVS_NAMESPACE = "rx_net_cfg";
    static constexpr const char* NVS_KEY_HOSTNAME = "hostname";
    static constexpr const char* NVS_KEY_SSID = "ssid";
    static constexpr const char* NVS_KEY_PASSWORD = "password";
    static constexpr const char* NVS_KEY_USE_STATIC = "use_static";
    static constexpr const char* NVS_KEY_IP = "ip";
    static constexpr const char* NVS_KEY_GATEWAY = "gateway";
    static constexpr const char* NVS_KEY_SUBNET = "subnet";
    static constexpr const char* NVS_KEY_DNS_PRIMARY = "dns_primary";
    static constexpr const char* NVS_KEY_DNS_SECONDARY = "dns_secondary";
    
public:
    /**
     * @brief Load receiver network configuration from NVS
     * @return true if config loaded successfully, false if not found (requires AP setup)
     */
    static bool loadConfig();
    
    /**
     * @brief Save receiver network configuration to NVS
     * @param hostname Hostname for mDNS (empty for "esp32-receiver")
     * @param ssid WiFi SSID (required, must not be empty)
     * @param password WiFi password (minimum 8 characters for WPA2)
     * @param use_static_ip true for static IP, false for DHCP
     * @param static_ip IP address (4 bytes, required if use_static_ip)
     * @param gateway Gateway address (4 bytes, required if use_static_ip)
     * @param subnet Subnet mask (4 bytes, required if use_static_ip)
     * @param dns_primary Primary DNS (4 bytes, optional)
     * @param dns_secondary Secondary DNS (4 bytes, optional)
     * @return true if saved successfully, false on validation error
     */
    static bool saveConfig(
        const char* hostname,
        const char* ssid,
        const char* password,
        bool use_static_ip,
        const uint8_t static_ip[4],
        const uint8_t gateway[4],
        const uint8_t subnet[4],
        const uint8_t dns_primary[4],
        const uint8_t dns_secondary[4]
    );
    
    /**
     * @brief Check if valid configuration exists in NVS
     * @return true if SSID is configured, false if AP setup required
     */
    static bool hasValidConfig();
    
    /**
     * @brief Clear all configuration from NVS (factory reset)
     */
    static void clearConfig();
    
    // Getters for current configuration
    static const char* getHostname() { return hostname_; }
    static const char* getSSID() { return ssid_; }
    static const char* getPassword() { return password_; }
    static bool useStaticIP() { return use_static_ip_; }
    static const uint8_t* getStaticIP() { return static_ip_; }
    static const uint8_t* getGateway() { return gateway_; }
    static const uint8_t* getSubnet() { return subnet_; }
    static const uint8_t* getDNSPrimary() { return dns_primary_; }
    static const uint8_t* getDNSSecondary() { return dns_secondary_; }
};

#endif // RECEIVER_NETWORK_CONFIG_H
