#include "ethernet_manager.h"
#include "../config/hardware_config.h"
#include "../config/network_config.h"
#include "../config/logging_config.h"
#include <Arduino.h>
#include <ESP32Ping.h>

// ============================================================================
// SINGLETON
// ============================================================================

static EthernetManager* g_ethernet_manager_instance = nullptr;

EthernetManager& EthernetManager::instance() {
    if (g_ethernet_manager_instance == nullptr) {
        g_ethernet_manager_instance = new EthernetManager();
    }
    return *g_ethernet_manager_instance;
}

EthernetManager::EthernetManager() {
    LOG_DEBUG("ETH", "EthernetManager constructor");
}

EthernetManager::~EthernetManager() {
    LOG_DEBUG("ETH", "EthernetManager destructor");
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool EthernetManager::init() {
    LOG_INFO("ETH", "Initializing Ethernet for Olimex ESP32-POE-ISO (WROVER)");
    
    // Validate state
    if (current_state_ != EthernetConnectionState::UNINITIALIZED) {
        LOG_WARN("ETH", "Already initialized (state: %s)", get_state_string());
        return true;
    }
    
    // Transition to PHY_RESET
    set_state(EthernetConnectionState::PHY_RESET);
    network_config_applied_ = false;
    
    // Load network configuration from NVS
    load_network_config();
    
    // Register event handler
    WiFi.onEvent(event_handler);
    LOG_DEBUG("ETH", "Event handler registered");
    
    // Hardware reset sequence for PHY
    LOG_DEBUG("ETH", "Performing PHY hardware reset...");
    pinMode(hardware::ETH_POWER_PIN, OUTPUT);
    digitalWrite(hardware::ETH_POWER_PIN, LOW);
    delay(10);
    digitalWrite(hardware::ETH_POWER_PIN, HIGH);
    delay(150);
    LOG_DEBUG("ETH", "PHY hardware reset complete");
    
    // Initialize Ethernet
    LOG_INFO("ETH", "Calling ETH.begin() for LAN8720 PHY");
    if (!ETH.begin(hardware::PHY_ADDR,
                   hardware::ETH_POWER_PIN,
                   hardware::ETH_MDC_PIN,
                   hardware::ETH_MDIO_PIN,
                   ETH_PHY_LAN8720,
                   ETH_CLOCK_GPIO0_OUT)) {
        LOG_ERROR("ETH", "Failed to initialize Ethernet hardware");
        set_state(EthernetConnectionState::ERROR_STATE);
        return false;
    }

    // Transition to LINK_ACQUIRING (wait for cable/link before applying DHCP/static config)
    if (current_state_ == EthernetConnectionState::PHY_RESET) {
        set_state(EthernetConnectionState::LINK_ACQUIRING);
    } else {
        LOG_WARN("ETH", "Skipping LINK_ACQUIRING transition (state: %s)", get_state_string());
    }
    
    metrics_.total_initialization_ms = millis();
    LOG_INFO("ETH", "Ethernet initialization complete (async, waiting for cable + IP)");
    
    return true;
}

// ============================================================================
// STATE MANAGEMENT
// ============================================================================

void EthernetManager::set_state(EthernetConnectionState new_state) {
    if (new_state == current_state_) {
        return;  // No change
    }
    
    previous_state_ = current_state_;
    current_state_ = new_state;
    state_enter_time_ms_ = millis();
    metrics_.state_transitions++;
    
    LOG_INFO("ETH_STATE", "State transition: %s → %s",
             ethernet_state_to_string(previous_state_),
             ethernet_state_to_string(new_state));
}

const char* EthernetManager::get_state_string() const {
    return ethernet_state_to_string(current_state_);
}

uint32_t EthernetManager::get_state_age_ms() const {
    return millis() - state_enter_time_ms_;
}

// ============================================================================
// EVENT HANDLER (Cable Detection)
// ============================================================================

void EthernetManager::event_handler(WiFiEvent_t event) {
    auto& mgr = instance();
    
    switch (event) {
        case ARDUINO_EVENT_ETH_START:
            LOG_INFO("ETH_EVENT", "Ethernet driver started");
            ETH.setHostname("espnow-transmitter");
            break;
            
        case ARDUINO_EVENT_ETH_CONNECTED:
            // ✅ PHYSICAL CABLE DETECTION
            LOG_INFO("ETH_EVENT", "✓ CABLE DETECTED: Ethernet link connected");
            if (mgr.current_state_ != EthernetConnectionState::CONNECTED &&
                mgr.current_state_ != EthernetConnectionState::ERROR_STATE) {
                // Ensure we move through LINK_ACQUIRING before applying DHCP/static config
                if (mgr.current_state_ == EthernetConnectionState::PHY_RESET ||
                    mgr.current_state_ == EthernetConnectionState::LINK_LOST ||
                    mgr.current_state_ == EthernetConnectionState::RECOVERING) {
                    mgr.set_state(EthernetConnectionState::LINK_ACQUIRING);
                }

                if (!mgr.network_config_applied_) {
                    mgr.set_state(EthernetConnectionState::CONFIG_APPLYING);
                    if (!mgr.apply_network_config()) {
                        mgr.set_state(EthernetConnectionState::ERROR_STATE);
                        break;
                    }
                    mgr.network_config_applied_ = true;
                }
                if (mgr.current_state_ == EthernetConnectionState::LINK_LOST ||
                    mgr.current_state_ == EthernetConnectionState::RECOVERING) {
                    LOG_INFO("ETH_EVENT", "Cable reconnected!");
                    mgr.metrics_.recoveries_attempted++;
                }

                mgr.last_link_time_ms_ = millis();
                mgr.metrics_.link_flaps++;
                LOG_INFO("ETH_EVENT", "Transitioning to IP_ACQUIRING (waiting for DHCP)...");
                // Immediately transition to IP_ACQUIRING to wait for DHCP
                mgr.set_state(EthernetConnectionState::IP_ACQUIRING);
            }
            break;
            
        case ARDUINO_EVENT_ETH_GOT_IP:
            LOG_INFO("ETH_EVENT", "✓ IP ASSIGNED: %s", ETH.localIP().toString().c_str());
            LOG_INFO("ETH_EVENT", "  Gateway: %s", ETH.gatewayIP().toString().c_str());
            LOG_INFO("ETH_EVENT", "  DNS: %s", ETH.dnsIP().toString().c_str());
            LOG_INFO("ETH_EVENT", "  Link Speed: %d Mbps", ETH.linkSpeed());

            if (mgr.current_state_ != EthernetConnectionState::CONNECTED &&
                mgr.current_state_ != EthernetConnectionState::ERROR_STATE) {
                EthernetConnectionState prior_state = mgr.current_state_;
                mgr.set_state(EthernetConnectionState::CONNECTED);
                mgr.last_ip_time_ms_ = millis();
                mgr.metrics_.connection_established_timestamp = millis();
                if (prior_state == EthernetConnectionState::LINK_LOST ||
                    prior_state == EthernetConnectionState::RECOVERING) {
                    mgr.metrics_.recoveries_successful++;
                }
                LOG_INFO("ETH_EVENT", "✓ ETHERNET FULLY READY (link + IP + gateway)");
                mgr.trigger_connected_callbacks();
            }
            break;
            
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            // ✅ PHYSICAL CABLE REMOVAL DETECTION
            LOG_WARN("ETH_EVENT", "✗ CABLE REMOVED: Ethernet link disconnected");

            if (ETH.linkUp()) {
                LOG_WARN("ETH_EVENT", "Disconnect event received but link is still up; ignoring");
                break;
            }

            if (mgr.current_state_ >= EthernetConnectionState::LINK_ACQUIRING &&
                mgr.current_state_ <= EthernetConnectionState::CONNECTED) {
                mgr.network_config_applied_ = false;
                mgr.set_state(EthernetConnectionState::LINK_LOST);
                mgr.metrics_.link_flaps++;
                LOG_WARN("ETH_EVENT", "Waiting for cable to be reconnected...");
                mgr.trigger_disconnected_callbacks();
            }
            break;
            
        case ARDUINO_EVENT_ETH_STOP:
            LOG_WARN("ETH_EVENT", "Ethernet driver stopped");
            if (mgr.current_state_ != EthernetConnectionState::ERROR_STATE) {
                mgr.set_state(EthernetConnectionState::ERROR_STATE);
            }
            break;
            
        default:
            break;
    }
}

// ============================================================================
// STATE MACHINE UPDATE
// ============================================================================

void EthernetManager::update_state_machine() {
    check_state_timeout();
    
    // Handle automatic transitions
    if (current_state_ == EthernetConnectionState::LINK_LOST) {
        // Check if we should move to RECOVERING
        uint32_t age = get_state_age_ms();
        if (age > 1000 && metrics_.recoveries_attempted == 0) {
            // Immediately move to RECOVERING after 1 second
            set_state(EthernetConnectionState::RECOVERING);
            metrics_.recoveries_attempted++;
            LOG_INFO("ETH", "Starting recovery sequence...");
        }
    }
}

void EthernetManager::check_state_timeout() {
    uint32_t age = get_state_age_ms();
    
    switch (current_state_) {
        case EthernetConnectionState::PHY_RESET:
            if (age > PHY_RESET_TIMEOUT_MS) {
                LOG_ERROR("ETH_TIMEOUT", "PHY reset timeout (%lu ms)", age);
                set_state(EthernetConnectionState::ERROR_STATE);
            }
            break;
            
        case EthernetConnectionState::CONFIG_APPLYING:
            if (age > CONFIG_APPLY_TIMEOUT_MS) {
                LOG_ERROR("ETH_TIMEOUT", "Config apply timeout (%lu ms)", age);
                set_state(EthernetConnectionState::ERROR_STATE);
            }
            break;
            
        case EthernetConnectionState::LINK_ACQUIRING:
            if (age > LINK_ACQUIRING_TIMEOUT_MS) {
                LOG_ERROR("ETH_TIMEOUT", "Link acquiring timeout - cable may not be present (%lu ms)", age);
                set_state(EthernetConnectionState::ERROR_STATE);
            }
            break;
            
        case EthernetConnectionState::IP_ACQUIRING:
            if (age > IP_ACQUIRING_TIMEOUT_MS) {
                LOG_ERROR("ETH_TIMEOUT", "IP acquiring timeout - DHCP server may be down (%lu ms)", age);
                set_state(EthernetConnectionState::ERROR_STATE);
            } else if (age % 5000 == 0) {
                LOG_INFO("ETH_TIMEOUT", "Still waiting for IP... (%lu ms)", age);
            }
            break;
            
        case EthernetConnectionState::RECOVERING:
            if (age > RECOVERY_TIMEOUT_MS) {
                LOG_ERROR("ETH_TIMEOUT", "Recovery timeout - cable may not be reconnected (%lu ms)", age);
                set_state(EthernetConnectionState::ERROR_STATE);
            }
            break;
            
        default:
            break;
    }
}

// ============================================================================
// NETWORK INFORMATION
// ============================================================================

IPAddress EthernetManager::get_local_ip() const {
    return is_fully_ready() ? ETH.localIP() : IPAddress(0, 0, 0, 0);
}

IPAddress EthernetManager::get_gateway_ip() const {
    return is_fully_ready() ? ETH.gatewayIP() : IPAddress(0, 0, 0, 0);
}

IPAddress EthernetManager::get_subnet_mask() const {
    return is_fully_ready() ? ETH.subnetMask() : IPAddress(0, 0, 0, 0);
}

IPAddress EthernetManager::get_dns_ip() const {
    return is_fully_ready() ? ETH.dnsIP() : IPAddress(0, 0, 0, 0);
}

int EthernetManager::get_link_speed() const {
    return is_link_present() ? ETH.linkSpeed() : 0;
}

// ============================================================================
// CALLBACKS
// ============================================================================

void EthernetManager::trigger_connected_callbacks() {
    LOG_DEBUG("ETH", "Triggering %zu connected callbacks", connected_callbacks_.size());
    for (auto& callback : connected_callbacks_) {
        if (callback) {
            callback();
        }
    }
}

void EthernetManager::trigger_disconnected_callbacks() {
    LOG_DEBUG("ETH", "Triggering %zu disconnected callbacks", disconnected_callbacks_.size());
    for (auto& callback : disconnected_callbacks_) {
        if (callback) {
            callback();
        }
    }
}

// =============================================================================
// Network Configuration Management Implementation
// =============================================================================

bool EthernetManager::apply_network_config() {
    LOG_INFO("ETH", "Applying network configuration...");
    if (use_static_ip_) {
        LOG_INFO("ETH", "Static IP Mode:");
        LOG_INFO("ETH", "  IP: %s", static_ip_.toString().c_str());
        LOG_INFO("ETH", "  Gateway: %s", static_gateway_.toString().c_str());
        LOG_INFO("ETH", "  Subnet: %s", static_subnet_.toString().c_str());
        LOG_INFO("ETH", "  DNS: %s", static_dns_primary_.toString().c_str());

        if (!ETH.config(static_ip_, static_gateway_, static_subnet_, static_dns_primary_)) {
            LOG_ERROR("ETH", "Failed to apply static IP configuration");
            return false;
        }
    } else {
        LOG_INFO("ETH", "DHCP Mode: Waiting for IP assignment from DHCP server...");
        if (!ETH.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0),
                       IPAddress(0, 0, 0, 0))) {
            LOG_WARN("ETH", "Failed to reset to DHCP, but continuing...");
        }
    }

    return true;
}

bool EthernetManager::loadNetworkConfig() {
    Preferences prefs;
    if (!prefs.begin("network", true)) {  // true = read-only
        LOG_WARN("NET_CFG", "Failed to open NVS namespace 'network' - using DHCP");
        use_static_ip_ = false;
        network_config_version_ = 0;
        return false;
    }
    
    // Load configuration
    use_static_ip_ = prefs.getBool("use_static", false);
    network_config_version_ = prefs.getUInt("version", 0);
    
    LOG_INFO("NET_CFG", "========== NETWORK CONFIGURATION LOADED ==========");
    LOG_INFO("NET_CFG", "Mode from NVS: %s", use_static_ip_ ? "STATIC IP" : "DHCP");
    LOG_INFO("NET_CFG", "Config version: %u", network_config_version_);
    
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
        
        LOG_INFO("NET_CFG", "Loaded static IP config from NVS (version %u):", network_config_version_);
        LOG_INFO("NET_CFG", "  IP: %s", static_ip_.toString().c_str());
        LOG_INFO("NET_CFG", "  Gateway: %s", static_gateway_.toString().c_str());
        LOG_INFO("NET_CFG", "  Subnet: %s", static_subnet_.toString().c_str());
        LOG_INFO("NET_CFG", "  DNS Primary: %s", static_dns_primary_.toString().c_str());
        LOG_INFO("NET_CFG", "  DNS Secondary: %s", static_dns_secondary_.toString().c_str());
    } else {
        LOG_INFO("NET_CFG", "Using DHCP (version %u)", network_config_version_);
    }
    
    prefs.end();
    return true;
}

bool EthernetManager::saveNetworkConfig(bool use_static, const uint8_t ip[4],
                                        const uint8_t gateway[4], const uint8_t subnet[4],
                                        const uint8_t dns_primary[4], const uint8_t dns_secondary[4]) {
    Preferences prefs;
    if (!prefs.begin("network", false)) {  // false = read-write
        LOG_ERROR("NET_CFG", "Failed to open NVS namespace 'network' for writing");
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
        
        LOG_INFO("NET_CFG", "Saved static IP config to NVS (version %u):", network_config_version_);
        LOG_INFO("NET_CFG", "  IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        LOG_INFO("NET_CFG", "  Gateway: %d.%d.%d.%d", gateway[0], gateway[1], gateway[2], gateway[3]);
        LOG_INFO("NET_CFG", "  Subnet: %d.%d.%d.%d", subnet[0], subnet[1], subnet[2], subnet[3]);
        LOG_INFO("NET_CFG", "  DNS Primary: %d.%d.%d.%d", dns_primary[0], dns_primary[1], dns_primary[2], dns_primary[3]);
        LOG_INFO("NET_CFG", "  DNS Secondary: %d.%d.%d.%d", dns_secondary[0], dns_secondary[1], dns_secondary[2], dns_secondary[3]);
        
        // Update internal state (won't apply until reboot)
        static_ip_ = IPAddress(ip[0], ip[1], ip[2], ip[3]);
        static_gateway_ = IPAddress(gateway[0], gateway[1], gateway[2], gateway[3]);
        static_subnet_ = IPAddress(subnet[0], subnet[1], subnet[2], subnet[3]);
        static_dns_primary_ = IPAddress(dns_primary[0], dns_primary[1], dns_primary[2], dns_primary[3]);
        static_dns_secondary_ = IPAddress(dns_secondary[0], dns_secondary[1], dns_secondary[2], dns_secondary[3]);
    } else {
        LOG_INFO("NET_CFG", "Saved DHCP config to NVS (version %u)", network_config_version_);
    }
    
    use_static_ip_ = use_static;
    
    LOG_INFO("NET_CFG", "========== CONFIGURATION SAVED TO NVS ==========");
    LOG_INFO("NET_CFG", "Mode saved: %s", use_static ? "STATIC IP" : "DHCP");
    LOG_INFO("NET_CFG", "Version: %u", network_config_version_);
    LOG_INFO("NET_CFG", "** REBOOT REQUIRED FOR CHANGES TO TAKE EFFECT **");
    LOG_INFO("NET_CFG", "====================================================");
    
    prefs.end();
    return true;
}

bool EthernetManager::testStaticIPReachability(const uint8_t ip[4], const uint8_t gateway[4],
                                                const uint8_t subnet[4], const uint8_t dns_primary[4]) {
    LOG_INFO("NET_TEST", "Testing static IP reachability...");
    
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
        LOG_ERROR("NET_TEST", "✗ Failed to apply test config");
        return false;
    }
    
    LOG_INFO("NET_TEST", "Temporarily applied: %s, gateway: %s", 
             test_ip.toString().c_str(), test_gateway.toString().c_str());
    
    // 3. Wait for network stack to settle
    delay(2000);
    
    // 4. Ping gateway using ICMP
    bool ping_success = Ping.ping(test_gateway, 3);  // 3 attempts
    
    if (ping_success) {
        LOG_INFO("NET_TEST", "✓ Gateway is reachable (%s)", test_gateway.toString().c_str());
    } else {
        LOG_WARN("NET_TEST", "✗ Gateway not reachable, reverting to previous config");
        
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
    
    LOG_INFO("NET_CONFLICT", "Pinging %s to check availability...", test_ip.toString().c_str());
    LOG_INFO("NET_CONFLICT", "Note: Can only detect live devices currently on network");
    
    // Ping the IP - if it responds, it's in use
    bool responds = Ping.ping(test_ip, 2);  // 2 attempts
    
    if (responds) {
        LOG_WARN("NET_CONFLICT", "✗ IP is in use by live device (ping successful)");
        return true;  // Conflict detected
    }
    
    LOG_INFO("NET_CONFLICT", "✓ No live device responded (IP appears available)");
    LOG_INFO("NET_CONFLICT", "Warning: Offline devices with this IP will not be detected");
    return false;  // No conflict detected (but could exist offline)
}

// ============================================================================
// Snake_case wrapper implementations
// ============================================================================

bool EthernetManager::load_network_config() {
    return loadNetworkConfig();
}

bool EthernetManager::save_network_config(bool use_static, const uint8_t ip[4],
                                         const uint8_t gateway[4], const uint8_t subnet[4],
                                         const uint8_t dns_primary[4], const uint8_t dns_secondary[4]) {
    return saveNetworkConfig(use_static, ip, gateway, subnet, dns_primary, dns_secondary);
}
