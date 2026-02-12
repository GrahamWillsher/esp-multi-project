#pragma once
#include <ETH.h>
#include <WiFi.h>
#include <Preferences.h>

/**
 * @brief Manages Ethernet connectivity for Olimex ESP32-POE-ISO
 * 
 * Singleton class that handles Ethernet initialization, event management,
 * connection status tracking, and network configuration management (DHCP/Static IP).
 */
class EthernetManager {
public:
    static EthernetManager& instance();
    
    /**
     * @brief Initialize Ethernet with hardware-specific configuration
     * 
     * Configures PHY reset, sets up event handlers, loads network configuration
     * from NVS, and starts Ethernet interface with static IP or DHCP.
     * @return true if initialization successful, false otherwise
     */
    bool init();
    
    /**
     * @brief Check if Ethernet is currently connected
     * @return true if link is up and IP assigned, false otherwise
     */
    bool is_connected() const { return connected_; }
    
    /**
     * @brief Get the current local IP address
     * @return IPAddress object with current IP (0.0.0.0 if not connected)
     */
    IPAddress get_local_ip() const;
    
    /**
     * @brief Get the gateway IP address
     * @return IPAddress object with gateway IP
     */
    IPAddress get_gateway_ip() const;
    
    /**
     * @brief Get the subnet mask
     * @return IPAddress object with subnet mask
     */
    IPAddress get_subnet_mask() const;
    
    // =========================================================================
    // Network Configuration Management
    // =========================================================================
    
    /**
     * @brief Check if using static IP (vs DHCP)
     * @return true if static IP is configured, false if using DHCP
     */
    bool isStaticIP() const { return use_static_ip_; }
    
    /**
     * @brief Get current network configuration version
     * @return Version number (increments on each save)
     */
    uint32_t getNetworkConfigVersion() const { return network_config_version_; }
    
    /**
     * @brief Load network configuration from NVS
     * 
     * Reads static IP settings from NVS storage. Called during init().
     * Falls back to DHCP if NVS read fails or is empty.
     * @return true if config loaded successfully, false if using defaults/DHCP
     */
    bool loadNetworkConfig();
    
    /**
     * @brief Save network configuration to NVS
     * 
     * Stores static IP settings to NVS and increments version number.
     * Does NOT apply configuration - reboot required.
     * @param use_static True for static IP, false for DHCP
     * @param ip Static IP address (4 bytes)
     * @param gateway Gateway IP address (4 bytes)
     * @param subnet Subnet mask (4 bytes)
     * @param dns_primary Primary DNS server (4 bytes)
     * @param dns_secondary Secondary DNS server (4 bytes)
     * @return true if saved successfully, false on NVS error
     */
    bool saveNetworkConfig(bool use_static, const uint8_t ip[4], 
                          const uint8_t gateway[4], const uint8_t subnet[4],
                          const uint8_t dns_primary[4], const uint8_t dns_secondary[4]);
    
    /**
     * @brief Test if a static IP configuration is reachable
     * 
     * Temporarily applies the proposed static IP and pings the gateway.
     * Reverts to previous config if ping fails. BLOCKS for 2-4 seconds.
     * @param ip Proposed static IP (4 bytes)
     * @param gateway Proposed gateway IP (4 bytes)
     * @param subnet Proposed subnet mask (4 bytes)
     * @param dns_primary Proposed primary DNS (4 bytes)
     * @return true if gateway is reachable, false otherwise
     */
    bool testStaticIPReachability(const uint8_t ip[4], const uint8_t gateway[4],
                                  const uint8_t subnet[4], const uint8_t dns_primary[4]);
    
    /**
     * @brief Check if a proposed IP address is already in use
     * 
     * Pings the proposed IP to detect active devices. BLOCKS for ~500ms.
     * WARNING: Only detects LIVE devices currently on the network!
     * Offline/powered-down devices will NOT be detected.
     * @param ip IP address to check (4 bytes)
     * @return true if IP is in use by active device, false if appears available
     */
    bool checkIPConflict(const uint8_t ip[4]);
    
    /**
     * @brief Get configured static IP address
     * @return IPAddress object (0.0.0.0 if DHCP mode)
     */
    IPAddress getStaticIP() const { return static_ip_; }
    
    /**
     * @brief Get configured gateway address
     * @return IPAddress object
     */
    IPAddress getGateway() const { return static_gateway_; }
    
    /**
     * @brief Get configured subnet mask
     * @return IPAddress object
     */
    IPAddress getSubnetMask() const { return static_subnet_; }
    
    /**
     * @brief Get configured primary DNS server
     * @return IPAddress object
     */
    IPAddress getDNSPrimary() const { return static_dns_primary_; }
    
    /**
     * @brief Get configured secondary DNS server
     * @return IPAddress object
     */
    IPAddress getDNSSecondary() const { return static_dns_secondary_; }
    
private:
    EthernetManager() = default;
    ~EthernetManager() = default;
    
    // Prevent copying
    EthernetManager(const EthernetManager&) = delete;
    EthernetManager& operator=(const EthernetManager&) = delete;
    
    /**
     * @brief WiFi event handler for Ethernet events
     * @param event WiFi event type
     */
    static void event_handler(WiFiEvent_t event);
    
    volatile bool connected_{false};
    
    // Network configuration state
    bool use_static_ip_{false};
    uint32_t network_config_version_{0};
    
    IPAddress static_ip_{0, 0, 0, 0};
    IPAddress static_gateway_{0, 0, 0, 0};
    IPAddress static_subnet_{0, 0, 0, 0};
    IPAddress static_dns_primary_{0, 0, 0, 0};
    IPAddress static_dns_secondary_{0, 0, 0, 0};
};
