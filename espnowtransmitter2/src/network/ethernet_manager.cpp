#include "ethernet_manager.h"
#include "../config/hardware_config.h"
#include "../config/network_config.h"
#include "../config/logging_config.h"
#include <Arduino.h>

// Forward declaration - implemented in message_handler.cpp
void send_ip_to_receiver();

// Forward declaration
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
    
    // Configure IP settings
    if (config::ethernet::USE_STATIC_IP) {
        LOG_INFO("[ETH] Using static IP");
        ETH.config(config::ethernet::STATIC_IP, 
                   config::ethernet::GATEWAY, 
                   config::ethernet::SUBNET, 
                   config::ethernet::DNS);
    } else {
        LOG_INFO("[ETH] Using DHCP");
    }
    
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
