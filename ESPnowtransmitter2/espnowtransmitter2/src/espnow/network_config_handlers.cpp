#include "network_config_handlers.h"

#include "tx_send_guard.h"
#include "../network/ethernet_manager.h"
#include "../config/logging_config.h"

#include <esp32common/espnow/connection_manager.h>
#include <espnow_peer_manager.h>
#include <Arduino.h>
#include <cstring>

namespace TxNetworkConfigHandlers {

void handle_network_config_request(const espnow_queue_msg_t& msg, uint8_t* receiver_mac) {
    if (msg.len < (int)sizeof(network_config_request_t)) {
        LOG_ERROR("NET_CFG", "Invalid request message size: %d bytes", msg.len);
        return;
    }

    auto& conn_mgr = EspNowConnectionManager::instance();
    auto state = conn_mgr.get_state();

    if (state != EspNowConnectionState::CONNECTED) {
        LOG_WARN("NET_CFG", "Cannot respond to network config request - receiver state is %u (need CONNECTED)",
                 (uint8_t)state);
        return;
    }

    memcpy(receiver_mac, msg.mac, 6);

    LOG_INFO("NET_CFG", "Received network config request from receiver");

    send_network_config_ack(receiver_mac, true, "Current configuration");
}

void handle_network_config_update(const espnow_queue_msg_t& msg, uint8_t* receiver_mac, QueueHandle_t network_config_queue) {
    if (msg.len < (int)sizeof(network_config_update_t)) {
        LOG_ERROR("NET_CFG", "Invalid message size: %d bytes", msg.len);
        return;
    }

    auto& conn_mgr = EspNowConnectionManager::instance();
    auto state = conn_mgr.get_state();

    if (state != EspNowConnectionState::CONNECTED) {
        LOG_WARN("NET_CFG", "Cannot respond to network config update - receiver state is %u (need CONNECTED)",
                 (uint8_t)state);
        return;
    }

    const network_config_update_t* config = reinterpret_cast<const network_config_update_t*>(msg.data);

    memcpy(receiver_mac, msg.mac, 6);

    LOG_INFO("NET_CFG", "Received network config update:");
    LOG_INFO("NET_CFG", "  Mode: %s", config->use_static_ip ? "Static" : "DHCP");

    if (config->use_static_ip) {
        LOG_INFO("NET_CFG", "  IP: %d.%d.%d.%d",
                 config->ip[0], config->ip[1], config->ip[2], config->ip[3]);
        LOG_INFO("NET_CFG", "  Gateway: %d.%d.%d.%d",
                 config->gateway[0], config->gateway[1], config->gateway[2], config->gateway[3]);
        LOG_INFO("NET_CFG", "  Subnet: %d.%d.%d.%d",
                 config->subnet[0], config->subnet[1], config->subnet[2], config->subnet[3]);
        LOG_INFO("NET_CFG", "  DNS Primary: %d.%d.%d.%d",
                 config->dns_primary[0], config->dns_primary[1], config->dns_primary[2], config->dns_primary[3]);
        LOG_INFO("NET_CFG", "  DNS Secondary: %d.%d.%d.%d",
                 config->dns_secondary[0], config->dns_secondary[1], config->dns_secondary[2], config->dns_secondary[3]);

        if (config->ip[0] == 0) {
            LOG_ERROR("NET_CFG", "Invalid static IP (cannot be 0.0.0.0)");
            send_network_config_ack(msg.mac, false, "Invalid IP address");
            return;
        }
    }

    if (network_config_queue && xQueueSend(network_config_queue, &msg, 0) == pdTRUE) {
        LOG_DEBUG("NET_CFG", "Message queued for background processing");
    } else {
        LOG_ERROR("NET_CFG", "Failed to queue message (queue full or not initialized)");
        send_network_config_ack(msg.mac, false, "Processing queue full");
    }
}

void send_network_config_ack(const uint8_t* receiver_mac, bool success, const char* message) {
    network_config_ack_t ack;
    memset(&ack, 0, sizeof(ack));

    auto& eth = EthernetManager::instance();

    ack.type = msg_network_config_ack;
    ack.success = success ? 1 : 0;
    ack.use_static_ip = eth.isStaticIP() ? 1 : 0;

    IPAddress current_ip = eth.get_local_ip();
    IPAddress current_gateway = eth.get_gateway_ip();
    IPAddress current_subnet = eth.get_subnet_mask();

    ack.current_ip[0] = current_ip[0];
    ack.current_ip[1] = current_ip[1];
    ack.current_ip[2] = current_ip[2];
    ack.current_ip[3] = current_ip[3];

    ack.current_gateway[0] = current_gateway[0];
    ack.current_gateway[1] = current_gateway[1];
    ack.current_gateway[2] = current_gateway[2];
    ack.current_gateway[3] = current_gateway[3];

    ack.current_subnet[0] = current_subnet[0];
    ack.current_subnet[1] = current_subnet[1];
    ack.current_subnet[2] = current_subnet[2];
    ack.current_subnet[3] = current_subnet[3];

    IPAddress static_ip = eth.getStaticIP();
    IPAddress static_gateway = eth.getGateway();
    IPAddress static_subnet = eth.getSubnetMask();
    IPAddress static_dns_primary = eth.getDNSPrimary();
    IPAddress static_dns_secondary = eth.getDNSSecondary();

    ack.static_ip[0] = static_ip[0];
    ack.static_ip[1] = static_ip[1];
    ack.static_ip[2] = static_ip[2];
    ack.static_ip[3] = static_ip[3];

    ack.static_gateway[0] = static_gateway[0];
    ack.static_gateway[1] = static_gateway[1];
    ack.static_gateway[2] = static_gateway[2];
    ack.static_gateway[3] = static_gateway[3];

    ack.static_subnet[0] = static_subnet[0];
    ack.static_subnet[1] = static_subnet[1];
    ack.static_subnet[2] = static_subnet[2];
    ack.static_subnet[3] = static_subnet[3];

    ack.static_dns_primary[0] = static_dns_primary[0];
    ack.static_dns_primary[1] = static_dns_primary[1];
    ack.static_dns_primary[2] = static_dns_primary[2];
    ack.static_dns_primary[3] = static_dns_primary[3];

    ack.static_dns_secondary[0] = static_dns_secondary[0];
    ack.static_dns_secondary[1] = static_dns_secondary[1];
    ack.static_dns_secondary[2] = static_dns_secondary[2];
    ack.static_dns_secondary[3] = static_dns_secondary[3];

    ack.config_version = eth.getNetworkConfigVersion();

    strncpy(ack.message, message, sizeof(ack.message) - 1);
    ack.message[sizeof(ack.message) - 1] = '\0';

    if (!EspnowPeerManager::is_peer_registered(receiver_mac)) {
        LOG_WARN("NET_CFG", "Receiver not registered as peer, adding now");
        if (!EspnowPeerManager::add_peer(receiver_mac)) {
            LOG_ERROR("NET_CFG", "Failed to add receiver as peer");
            return;
        }
    }

    esp_err_t result = TxSendGuard::send_to_receiver_guarded(
        receiver_mac,
        (const uint8_t*)&ack,
        sizeof(ack),
        "network_config_ack"
    );
    if (result == ESP_OK) {
        LOG_INFO("NET_CFG", "Sent ACK: %s (success=%d)", message, success);
        LOG_DEBUG("NET_CFG", "  Current: %d.%d.%d.%d",
                  ack.current_ip[0], ack.current_ip[1], ack.current_ip[2], ack.current_ip[3]);
        LOG_DEBUG("NET_CFG", "  Static saved: %d.%d.%d.%d",
                  ack.static_ip[0], ack.static_ip[1], ack.static_ip[2], ack.static_ip[3]);
    } else {
        LOG_ERROR("NET_CFG", "Failed to send ACK: %s", esp_err_to_name(result));
    }
}

void process_network_config_update(const espnow_queue_msg_t& msg) {
    const network_config_update_t* config = reinterpret_cast<const network_config_update_t*>(msg.data);
    auto& eth = EthernetManager::instance();

    LOG_INFO("NET_CFG", "Processing configuration in background...");

    if (config->use_static_ip) {
        if (config->ip[0] == 255 && config->ip[1] == 255 &&
            config->ip[2] == 255 && config->ip[3] == 255) {
            LOG_ERROR("NET_CFG", "IP cannot be broadcast address");
            send_network_config_ack(msg.mac, false, "IP is broadcast");
            return;
        }

        if (config->ip[0] >= 224 && config->ip[0] <= 239) {
            LOG_ERROR("NET_CFG", "IP cannot be multicast address");
            send_network_config_ack(msg.mac, false, "IP is multicast");
            return;
        }

        bool same_subnet = true;
        for (int i = 0; i < 4; i++) {
            if ((config->ip[i] & config->subnet[i]) != (config->gateway[i] & config->subnet[i])) {
                same_subnet = false;
                break;
            }
        }
        if (!same_subnet) {
            LOG_WARN("NET_CFG", "IP and gateway not in same subnet - may cause routing issues");
        }

        uint32_t subnet_val = (config->subnet[0] << 24) | (config->subnet[1] << 16) |
                              (config->subnet[2] << 8) | config->subnet[3];
        uint32_t inverted = ~subnet_val + 1;
        if ((inverted & (inverted - 1)) != 0 && inverted != 0) {
            LOG_ERROR("NET_CFG", "Invalid subnet mask (not contiguous)");
            send_network_config_ack(msg.mac, false, "Invalid subnet mask");
            return;
        }

        if (eth.checkIPConflict(config->ip)) {
            LOG_ERROR("NET_CFG", "IP address conflict detected");
            send_network_config_ack(msg.mac, false, "IP in use by active device");
            return;
        }

        if (!eth.testStaticIPReachability(config->ip, config->gateway,
                                          config->subnet, config->dns_primary)) {
            LOG_ERROR("NET_CFG", "Gateway unreachable");
            send_network_config_ack(msg.mac, false, "Gateway unreachable");
            return;
        }
    }

    if (eth.saveNetworkConfig(config->use_static_ip, config->ip, config->gateway,
                              config->subnet, config->dns_primary, config->dns_secondary)) {
        LOG_INFO("NET_CFG", "✓ Configuration saved to NVS");
        send_network_config_ack(msg.mac, true, "OK - reboot required");
    } else {
        LOG_ERROR("NET_CFG", "✗ Failed to save configuration");
        send_network_config_ack(msg.mac, false, "NVS save failed");
    }
}

} // namespace TxNetworkConfigHandlers
