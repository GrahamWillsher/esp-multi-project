#pragma once
#include <ETH.h>
#include <WiFi.h>

/**
 * @brief Manages Ethernet connectivity for Olimex ESP32-POE-ISO
 * 
 * Singleton class that handles Ethernet initialization, event management,
 * and connection status tracking.
 */
class EthernetManager {
public:
    static EthernetManager& instance();
    
    /**
     * @brief Initialize Ethernet with hardware-specific configuration
     * 
     * Configures PHY reset, sets up event handlers, and starts Ethernet
     * interface with optional static IP or DHCP.
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
};
