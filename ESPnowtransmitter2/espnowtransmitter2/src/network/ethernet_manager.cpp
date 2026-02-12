#include "ethernet_manager.h"
#include "../config/hardware_config.h"
#include "../config/network_config.h"
#include "../config/logging_config.h"
#include "../espnow/version_beacon_manager.h"
#include <Arduino.h>
#include <ESP32Ping.h>

// Forward declaration - implemented in message_handler.cpp
void send_ip_to_receiver();

EthernetManager& EthernetManager::instance() {
    static EthernetManager instance;
    return instance;
}

void EthernetManager::event_handler(WiFiEvent_t event) {
    auto& mgr = instance();
    
    switch (event) {
        case ARDUINO_EVENT_ETH_START:
            LOG_INFO("[ETH] Ethernet Started");
            ETH.setHostname("espnow-transmitter");
            break;
            
        case ARDUINO_EVENT_ETH_CONNECTED:
            LOG_INFO("[ETH] Ethernet Link Connected");
            // Notify version beacon manager that Ethernet link is up
            VersionBeaconManager::instance().notify_ethernet_changed(true);
            break;
            
        case ARDUINO_EVENT_ETH_GOT_IP:
            LOG_INFO("[ETH] IP Address: %s", ETH.localIP().toString().c_str());
            LOG_INFO("[ETH] Gateway: %s", ETH.gatewayIP().toString().c_str());
            LOG_INFO("[ETH] Link Speed: %d Mbps", ETH.linkSpeed());
            mgr.connected_ = true;
            
            // Automatically send IP to receiver when we get IP address
            send_ip_to_receiver();
            break;
            
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            LOG_WARN("[ETH] Ethernet Disconnected");
            mgr.connected_ = false;
            // Notify version beacon manager that Ethernet link is down
            VersionBeaconManager::instance().notify_ethernet_changed(false);
            break;
            
        case ARDUINO_EVENT_ETH_STOP:
            LOG_WARN("[ETH] Ethernet Stopped");
            mgr.connected_ = false;
            break;
            
        default:
            break;
    }
}

bool EthernetManager::init() {
    LOG_DEBUG("Initializing Ethernet for Olimex ESP32-POE-ISO (WROVER)...");
    
    // Load network configuration from NVS
    loadNetworkConfig();
    
    // Register event handler
    WiFi.onEvent(event_handler);
    
    // Hardware reset sequence for PHY
    pinMode(hardware::ETH_POWER_PIN, OUTPUT);
    digitalWrite(hardware::ETH_POWER_PIN, LOW);
    delay(10);
    digitalWrite(hardware::ETH_POWER_PIN, HIGH);
    delay(150);
    
    // Initialize Ethernet with GPIO0 clock (WROVER requirement)
    if (!ETH.begin(hardware::PHY_ADDR, 
                   hardware::ETH_POWER_PIN, 
                   hardware::ETH_MDC_PIN, 
                   hardware::ETH_MDIO_PIN, 
                   ETH_PHY_LAN8720,
                   ETH_CLOCK_GPIO0_OUT)) {
        LOG_ERROR("[ETH] Failed to initialize Ethernet");
        return false;
    }
    
    // Configure IP settings from NVS (or DHCP as fallback)
    LOG_INFO("[ETH] ========== APPLYING NETWORK CONFIGURATION ==========");
    if (use_static_ip_) {
        LOG_INFO("[ETH] Applying STATIC IP configuration:");
        LOG_INFO("[ETH]   IP: %s", static_ip_.toString().c_str());
        LOG_INFO("[ETH]   Gateway: %s", static_gateway_.toString().c_str());
        LOG_INFO("[ETH]   Subnet: %s", static_subnet_.toString().c_str());
        LOG_INFO("[ETH]   DNS: %s", static_dns_primary_.toString().c_str());
        if (!ETH.config(static_ip_, static_gateway_, static_subnet_, static_dns_primary_)) {
            LOG_ERROR("[ETH] Failed to apply static IP configuration");
            return false;
        }
    } else {
        LOG_INFO("[ETH] Applying DHCP configuration");
        // Reset to DHCP by passing all zeros (this clears any previous static config)
        if (!ETH.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0))) {
            LOG_WARN("[ETH] Failed to reset to DHCP, but continuing...");
        }
        LOG_INFO("[ETH] DHCP enabled - waiting for IP assignment from server");
    }
    LOG_INFO("[ETH] ======================================================");
    
    LOG_INFO("[ETH] Ethernet initialization started (async)");
    delay(1000);  // Give time for initialization
    return true;
}

IPAddress EthernetManager::get_local_ip() const {
    return connected_ ? ETH.localIP() : IPAddress(0, 0, 0, 0);
}

IPAddress EthernetManager::get_gateway_ip() const {
    return connected_ ? ETH.gatewayIP() : IPAddress(0, 0, 0, 0);
}

IPAddress EthernetManager::get_subnet_mask() const {
    return connected_ ? ETH.subnetMask() : IPAddress(0, 0, 0, 0);
}

// =============================================================================
// Network Configuration Management Implementation
// =============================================================================

bool EthernetManager::loadNetworkConfig() {
    Preferences prefs;
    if (!prefs.begin("network", true)) {  // true = read-only
        LOG_WARN("[NET_CFG] Failed to open NVS namespace 'network' - using DHCP");
        use_static_ip_ = false;
        network_config_version_ = 0;
        return false;
    }
    
    // Load configuration
    use_static_ip_ = prefs.getBool("use_static", false);
    network_config_version_ = prefs.getUInt("version", 0);
    
    LOG_INFO("[NET_CFG] ========== NETWORK CONFIGURATION LOADED ==========");
    LOG_INFO("[NET_CFG] Mode from NVS: %s", use_static_ip_ ? "STATIC IP" : "DHCP");
    LOG_INFO("[NET_CFG] Config version: %u", network_config_version_);
    
    if (use_static_ip_) {
        uint8_t ip[4], gateway[4], subnet[4], dns_primary[4], dns_secondary[4];
        
        prefs.getBytes("ip", ip, 4);
        prefs.getBytes("gateway", gateway, 4);
        prefs.getBytes("subnet", subnet, 4);
        prefs.getBytes("dns_primary", dns_primary, 4);
        prefs.getBytes("dns_secondary", dns_secondary, 4);
        
        static_ip_ = IPAddress(ip[0], ip[1], ip[2], ip[3]);
        static_gateway_ = IPAddress(gateway[0], gateway[1], gateway[2], gateway[3]);
        static_subnet_ = IPAddress(subnet[0], subnet[1], subnet[2], subnet[3]);
        static_dns_primary_ = IPAddress(dns_primary[0], dns_primary[1], dns_primary[2], dns_primary[3]);
        static_dns_secondary_ = IPAddress(dns_secondary[0], dns_secondary[1], dns_secondary[2], dns_secondary[3]);
        
        LOG_INFO("[NET_CFG] Loaded static IP config from NVS (version %u):", network_config_version_);
        LOG_INFO("[NET_CFG]   IP: %s", static_ip_.toString().c_str());
        LOG_INFO("[NET_CFG]   Gateway: %s", static_gateway_.toString().c_str());
        LOG_INFO("[NET_CFG]   Subnet: %s", static_subnet_.toString().c_str());
        LOG_INFO("[NET_CFG]   DNS Primary: %s", static_dns_primary_.toString().c_str());
        LOG_INFO("[NET_CFG]   DNS Secondary: %s", static_dns_secondary_.toString().c_str());
    } else {
        LOG_INFO("[NET_CFG] Using DHCP (version %u)", network_config_version_);
    }
    
    prefs.end();
    return true;
}

bool EthernetManager::saveNetworkConfig(bool use_static, const uint8_t ip[4],
                                        const uint8_t gateway[4], const uint8_t subnet[4],
                                        const uint8_t dns_primary[4], const uint8_t dns_secondary[4]) {
    Preferences prefs;
    if (!prefs.begin("network", false)) {  // false = read-write
        LOG_ERROR("[NET_CFG] Failed to open NVS namespace 'network' for writing");
        return false;
    }
    
    // Increment version before saving
    network_config_version_++;
    
    // Save all settings
    prefs.putBool("use_static", use_static);
    prefs.putUInt("version", network_config_version_);
    
    if (use_static) {
        prefs.putBytes("ip", ip, 4);
        prefs.putBytes("gateway", gateway, 4);
        prefs.putBytes("subnet", subnet, 4);
        prefs.putBytes("dns_primary", dns_primary, 4);
        prefs.putBytes("dns_secondary", dns_secondary, 4);
        
        LOG_INFO("[NET_CFG] Saved static IP config to NVS (version %u):", network_config_version_);
        LOG_INFO("[NET_CFG]   IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        LOG_INFO("[NET_CFG]   Gateway: %d.%d.%d.%d", gateway[0], gateway[1], gateway[2], gateway[3]);
        LOG_INFO("[NET_CFG]   Subnet: %d.%d.%d.%d", subnet[0], subnet[1], subnet[2], subnet[3]);
        LOG_INFO("[NET_CFG]   DNS Primary: %d.%d.%d.%d", dns_primary[0], dns_primary[1], dns_primary[2], dns_primary[3]);
        LOG_INFO("[NET_CFG]   DNS Secondary: %d.%d.%d.%d", dns_secondary[0], dns_secondary[1], dns_secondary[2], dns_secondary[3]);
        
        // Update internal state (won't apply until reboot)
        static_ip_ = IPAddress(ip[0], ip[1], ip[2], ip[3]);
        static_gateway_ = IPAddress(gateway[0], gateway[1], gateway[2], gateway[3]);
        static_subnet_ = IPAddress(subnet[0], subnet[1], subnet[2], subnet[3]);
        static_dns_primary_ = IPAddress(dns_primary[0], dns_primary[1], dns_primary[2], dns_primary[3]);
        static_dns_secondary_ = IPAddress(dns_secondary[0], dns_secondary[1], dns_secondary[2], dns_secondary[3]);
    } else {
        LOG_INFO("[NET_CFG] Saved DHCP config to NVS (version %u)", network_config_version_);
    }
    
    use_static_ip_ = use_static;
    
    LOG_INFO("[NET_CFG] ========== CONFIGURATION SAVED TO NVS ==========");
    LOG_INFO("[NET_CFG] Mode saved: %s", use_static ? "STATIC IP" : "DHCP");
    LOG_INFO("[NET_CFG] Version: %u", network_config_version_);
    LOG_INFO("[NET_CFG] ** REBOOT REQUIRED FOR CHANGES TO TAKE EFFECT **");
    LOG_INFO("[NET_CFG] ====================================================");
    
    prefs.end();
    return true;
}

bool EthernetManager::testStaticIPReachability(const uint8_t ip[4], const uint8_t gateway[4],
                                                const uint8_t subnet[4], const uint8_t dns_primary[4]) {
    LOG_INFO("[NET_TEST] Testing static IP reachability...");
    
    // 1. Save current DHCP/static config for rollback
    IPAddress current_ip = ETH.localIP();
    IPAddress current_gateway = ETH.gatewayIP();
    IPAddress current_subnet = ETH.subnetMask();
    IPAddress current_dns = ETH.dnsIP();
    bool was_static = use_static_ip_;
    
    // 2. Temporarily apply static IP
    IPAddress test_ip(ip[0], ip[1], ip[2], ip[3]);
    IPAddress test_gateway(gateway[0], gateway[1], gateway[2], gateway[3]);
    IPAddress test_subnet(subnet[0], subnet[1], subnet[2], subnet[3]);
    IPAddress test_dns(dns_primary[0], dns_primary[1], dns_primary[2], dns_primary[3]);
    
    if (!ETH.config(test_ip, test_gateway, test_subnet, test_dns)) {
        LOG_ERROR("[NET_TEST] ✗ Failed to apply test config");
        return false;
    }
    
    LOG_INFO("[NET_TEST] Temporarily applied: %s, gateway: %s", 
             test_ip.toString().c_str(), test_gateway.toString().c_str());
    
    // 3. Wait for network stack to settle
    delay(2000);
    
    // 4. Ping gateway using ICMP
    bool ping_success = Ping.ping(test_gateway, 3);  // 3 attempts
    
    if (ping_success) {
        LOG_INFO("[NET_TEST] ✓ Gateway is reachable (%s)", test_gateway.toString().c_str());
    } else {
        LOG_WARN("[NET_TEST] ✗ Gateway not reachable, reverting to previous config");
        
        // Rollback to previous config
        if (was_static) {
            ETH.config(current_ip, current_gateway, current_subnet, current_dns);
        } else {
            ETH.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);  // Re-enable DHCP
        }
        delay(2000);
    }
    
    return ping_success;
}

bool EthernetManager::checkIPConflict(const uint8_t ip[4]) {
    IPAddress test_ip(ip[0], ip[1], ip[2], ip[3]);
    
    LOG_INFO("[NET_CONFLICT] Pinging %s to check availability...", test_ip.toString().c_str());
    LOG_INFO("[NET_CONFLICT] Note: Can only detect live devices currently on network");
    
    // Ping the IP - if it responds, it's in use
    bool responds = Ping.ping(test_ip, 2);  // 2 attempts
    
    if (responds) {
        LOG_WARN("[NET_CONFLICT] ✗ IP is in use by live device (ping successful)");
        return true;  // Conflict detected
    }
    
    LOG_INFO("[NET_CONFLICT] ✓ No live device responded (IP appears available)");
    LOG_INFO("[NET_CONFLICT] Warning: Offline devices with this IP will not be detected");
    return false;  // No conflict detected (but could exist offline)
}
