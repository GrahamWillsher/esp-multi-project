#include "version_beacon_manager.h"
#include <esp_now.h>
#include <espnow_transmitter.h>
#include <connection_manager.h>
#include <mqtt_manager.h>
#include "../config/logging_config.h"
#include "../settings/settings_manager.h"
#include "../network/mqtt_task.h"
#include "../network/ethernet_manager.h"
#include "message_handler.h"
#include <firmware_version.h>
#include <firmware_metadata.h>

VersionBeaconManager& VersionBeaconManager::instance() {
    static VersionBeaconManager instance;
    return instance;
}

void VersionBeaconManager::init() {
    LOG_INFO("VERSION_BEACON", "Manager initialized");
    
    // Send initial beacon immediately
    send_version_beacon(true);
}

void VersionBeaconManager::notify_mqtt_connected(bool connected) {
    if (mqtt_connected_ != connected) {
        mqtt_connected_ = connected;
        LOG_INFO("VERSION_BEACON", "MQTT state changed: %s", 
                 connected ? "CONNECTED" : "DISCONNECTED");
        send_version_beacon(true);  // Force immediate beacon
    }
}

void VersionBeaconManager::notify_ethernet_changed(bool connected) {
    if (ethernet_connected_ != connected) {
        ethernet_connected_ = connected;
        LOG_INFO("VERSION_BEACON", "Ethernet state changed: %s", 
                 connected ? "CONNECTED" : "DISCONNECTED");
        send_version_beacon(true);
    }
}

void VersionBeaconManager::notify_config_version_changed(config_section_t section) {
    LOG_INFO("VERSION_BEACON", "Config version changed: section=%d", (int)section);
    send_version_beacon(true);  // Force immediate beacon
}

void VersionBeaconManager::update() {
    uint32_t now = millis();
    
    // Periodic heartbeat beacon - FORCE send every 15 seconds regardless of changes
    // This ensures receiver always has fresh runtime status (MQTT/Ethernet connected state)
    if (now - last_beacon_ms_ >= PERIODIC_INTERVAL_MS) {
        send_version_beacon(true);  // Force send - receiver needs periodic status updates
    }
}

bool VersionBeaconManager::has_runtime_state_changed() {
    // Check MQTT state
    if (mqtt_connected_ != prev_mqtt_connected_) return true;
    
    // Check Ethernet state
    if (ethernet_connected_ != prev_ethernet_connected_) return true;
    
    return false;
}

uint32_t VersionBeaconManager::get_config_version(config_section_t section) {
    switch (section) {
        case config_section_mqtt:
            return MqttConfigManager::getConfigVersion();
            
        case config_section_network:
            return EthernetManager::instance().getNetworkConfigVersion();
            
        case config_section_battery:
            return SettingsManager::instance().get_battery_settings_version();
            
        case config_section_power_profile:
                return SettingsManager::instance().get_power_settings_version();
        
        case config_section_metadata:
            return FW_VERSION_NUMBER;
            
        default:
            return 0;
    }
}

void VersionBeaconManager::send_version_beacon(bool force) {
    uint32_t now = millis();
    
    // Rate limiting (except for forced beacons)
    if (!force && now - last_beacon_ms_ < MIN_BEACON_INTERVAL_MS) {
        return;
    }
    
    // Update current runtime state
    mqtt_connected_ = MqttTask::instance().is_connected();
    ethernet_connected_ = EthernetManager::instance().is_connected();
    
    // Check if anything changed (unless forced)
    if (!force && !has_runtime_state_changed()) {
        return;  // No changes, skip beacon
    }
    
    // Build version beacon
    version_beacon_t beacon;
    beacon.type = msg_version_beacon;
    beacon.mqtt_config_version = get_config_version(config_section_mqtt);
    beacon.network_config_version = get_config_version(config_section_network);
    beacon.battery_settings_version = get_config_version(config_section_battery);
    beacon.power_profile_version = get_config_version(config_section_power_profile);
    beacon.metadata_config_version = get_config_version(config_section_metadata);
    beacon.mqtt_connected = mqtt_connected_;
    beacon.ethernet_connected = ethernet_connected_;
    
    // Populate firmware metadata directly (no separate request/response needed)
    if (FirmwareMetadata::isValid(FirmwareMetadata::metadata)) {
        strncpy(beacon.env_name, FirmwareMetadata::metadata.env_name, sizeof(beacon.env_name) - 1);
        beacon.env_name[sizeof(beacon.env_name) - 1] = '\0';
        beacon.version_major = FirmwareMetadata::metadata.version_major;
        beacon.version_minor = FirmwareMetadata::metadata.version_minor;
        beacon.version_patch = FirmwareMetadata::metadata.version_patch;
    } else {
        // Fallback to compile-time values if metadata invalid
        strncpy(beacon.env_name, DEVICE_NAME, sizeof(beacon.env_name) - 1);
        beacon.env_name[sizeof(beacon.env_name) - 1] = '\0';
        beacon.version_major = FW_VERSION_MAJOR;
        beacon.version_minor = FW_VERSION_MINOR;
        beacon.version_patch = FW_VERSION_PATCH;
    }
    beacon.reserved[0] = 0;
    
    // Send via ESP-NOW to receiver (if connected)
    if (EspNowConnectionManager::instance().is_connected()) {
        // Get receiver MAC from connection manager
        const uint8_t* peer_mac = EspNowConnectionManager::instance().get_peer_mac();
        
        esp_err_t result = esp_now_send(
            peer_mac,
            (const uint8_t*)&beacon,
            sizeof(beacon)
        );
        
        if (result == ESP_OK) {
            LOG_DEBUG("VERSION_BEACON", "Sent: MQTT:v%u, Net:v%u, Batt:v%u, Profile:v%u, Meta:v%u (MQTT:%s, ETH:%s)",
                     beacon.mqtt_config_version,
                     beacon.network_config_version,
                     beacon.battery_settings_version,
                     beacon.power_profile_version,
                     beacon.metadata_config_version,
                     beacon.mqtt_connected ? "CONN" : "DISC",
                     beacon.ethernet_connected ? "UP" : "DOWN");
        } else {
            LOG_ERROR("VERSION_BEACON", "Send failed: %s", esp_err_to_name(result));
        }
    }
    
    // Update previous state
    prev_mqtt_connected_ = mqtt_connected_;
    prev_ethernet_connected_ = ethernet_connected_;
    
    last_beacon_ms_ = now;
}

void VersionBeaconManager::send_config_section(config_section_t section, const uint8_t* receiver_mac) {
    LOG_INFO("VERSION_BEACON", "Sending config section: %d", (int)section);
    
    // Send the appropriate config message based on section requested
    switch (section) {
        case config_section_mqtt: {
            // Build and send MQTT config ACK message
            mqtt_config_ack_t mqtt_msg;
            mqtt_msg.type = msg_mqtt_config_ack;
            mqtt_msg.success = 1;  // Response to request (not an error)
            mqtt_msg.enabled = MqttConfigManager::isEnabled() ? 1 : 0;
            
            // Get MQTT server IP
            IPAddress mqtt_server = MqttConfigManager::getServer();
            mqtt_msg.server[0] = mqtt_server[0];
            mqtt_msg.server[1] = mqtt_server[1];
            mqtt_msg.server[2] = mqtt_server[2];
            mqtt_msg.server[3] = mqtt_server[3];
            
            mqtt_msg.port = MqttConfigManager::getPort();
            
            // Copy username, password, client_id
            strncpy(mqtt_msg.username, MqttConfigManager::getUsername(), sizeof(mqtt_msg.username) - 1);
            mqtt_msg.username[sizeof(mqtt_msg.username) - 1] = '\0';
            
            strncpy(mqtt_msg.password, MqttConfigManager::getPassword(), sizeof(mqtt_msg.password) - 1);
            mqtt_msg.password[sizeof(mqtt_msg.password) - 1] = '\0';
            
            strncpy(mqtt_msg.client_id, MqttConfigManager::getClientId(), sizeof(mqtt_msg.client_id) - 1);
            mqtt_msg.client_id[sizeof(mqtt_msg.client_id) - 1] = '\0';
            
            mqtt_msg.connected = MqttTask::instance().is_connected() ? 1 : 0;
            mqtt_msg.config_version = MqttConfigManager::getConfigVersion();
            
            strncpy(mqtt_msg.message, "Config sent in response to version mismatch", sizeof(mqtt_msg.message) - 1);
            mqtt_msg.message[sizeof(mqtt_msg.message) - 1] = '\0';
            
            mqtt_msg.checksum = 0;  // TODO: Calculate checksum if needed
            
            esp_now_send(receiver_mac, (const uint8_t*)&mqtt_msg, sizeof(mqtt_msg));
            break;
        }
        
        case config_section_network: {
            // Build and send network config ACK message
            network_config_ack_t net_msg;
            net_msg.type = msg_network_config_ack;
            net_msg.success = 1;  // Response to request (not an error)
            
            // Get current IP configuration from EthernetManager
            IPAddress current_ip = EthernetManager::instance().get_local_ip();
            IPAddress current_gateway = EthernetManager::instance().get_gateway_ip();
            IPAddress current_subnet = EthernetManager::instance().get_subnet_mask();
            
            net_msg.current_ip[0] = current_ip[0];
            net_msg.current_ip[1] = current_ip[1];
            net_msg.current_ip[2] = current_ip[2];
            net_msg.current_ip[3] = current_ip[3];
            
            net_msg.current_gateway[0] = current_gateway[0];
            net_msg.current_gateway[1] = current_gateway[1];
            net_msg.current_gateway[2] = current_gateway[2];
            net_msg.current_gateway[3] = current_gateway[3];
            
            net_msg.current_subnet[0] = current_subnet[0];
            net_msg.current_subnet[1] = current_subnet[1];
            net_msg.current_subnet[2] = current_subnet[2];
            net_msg.current_subnet[3] = current_subnet[3];
            
            // Get static IP configuration from EthernetManager
            IPAddress static_ip = EthernetManager::instance().getStaticIP();
            IPAddress static_gateway = EthernetManager::instance().getGateway();
            IPAddress static_subnet = EthernetManager::instance().getSubnetMask();
            IPAddress static_dns1 = EthernetManager::instance().getDNSPrimary();
            IPAddress static_dns2 = EthernetManager::instance().getDNSSecondary();
            
            net_msg.static_ip[0] = static_ip[0];
            net_msg.static_ip[1] = static_ip[1];
            net_msg.static_ip[2] = static_ip[2];
            net_msg.static_ip[3] = static_ip[3];
            
            net_msg.static_gateway[0] = static_gateway[0];
            net_msg.static_gateway[1] = static_gateway[1];
            net_msg.static_gateway[2] = static_gateway[2];
            net_msg.static_gateway[3] = static_gateway[3];
            
            net_msg.static_subnet[0] = static_subnet[0];
            net_msg.static_subnet[1] = static_subnet[1];
            net_msg.static_subnet[2] = static_subnet[2];
            net_msg.static_subnet[3] = static_subnet[3];
            
            net_msg.static_dns_primary[0] = static_dns1[0];
            net_msg.static_dns_primary[1] = static_dns1[1];
            net_msg.static_dns_primary[2] = static_dns1[2];
            net_msg.static_dns_primary[3] = static_dns1[3];
            
            net_msg.static_dns_secondary[0] = static_dns2[0];
            net_msg.static_dns_secondary[1] = static_dns2[1];
            net_msg.static_dns_secondary[2] = static_dns2[2];
            net_msg.static_dns_secondary[3] = static_dns2[3];
            
            net_msg.use_static_ip = EthernetManager::instance().isStaticIP() ? 1 : 0;
            net_msg.config_version = EthernetManager::instance().getNetworkConfigVersion();
            
            strncpy(net_msg.message, "Config sent in response to version mismatch", sizeof(net_msg.message) - 1);
            net_msg.message[sizeof(net_msg.message) - 1] = '\0';
            
            esp_now_send(receiver_mac, (const uint8_t*)&net_msg, sizeof(net_msg));
            LOG_INFO("VERSION_BEACON", "Sent network config (v%u) in response to request", net_msg.config_version);
            break;
        }
        
        case config_section_battery: {
            // Battery settings would be sent here
            // For now, log that this needs to be implemented  
            LOG_WARN("VERSION_BEACON", "Battery config section send not yet implemented");
            break;
        }
        
        case config_section_power_profile: {
            // Power profile would be sent here
            LOG_WARN("VERSION_BEACON", "Power profile section send not yet implemented");
            break;
        }

        case config_section_metadata: {
            // Metadata is now sent directly in VERSION_BEACON - no separate response needed
            LOG_INFO("VERSION_BEACON", "Metadata request received but metadata is now in VERSION_BEACON");
            break;
        }
    }
}

void VersionBeaconManager::handle_config_request(const config_section_request_t* request, const uint8_t* sender_mac) {
    LOG_INFO("VERSION_BEACON", "Config request received: section=%d, version=%u",
             (int)request->section, request->requested_version);
    
    // Verify the requested version matches current version
    uint32_t current_version = get_config_version(request->section);
    
    if (current_version != request->requested_version) {
        LOG_WARN("VERSION_BEACON", "Version mismatch: requested v%u, current v%u",
                 request->requested_version, current_version);
        // Send anyway - receiver wants to update
    }
    
    // Send the requested config section
    send_config_section(request->section, sender_mac);
}
