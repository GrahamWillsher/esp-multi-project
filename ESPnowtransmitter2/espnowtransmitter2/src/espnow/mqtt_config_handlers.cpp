#include "mqtt_config_handlers.h"

#include "tx_send_guard.h"
#include "../config/logging_config.h"

#include <esp32common/espnow/connection_manager.h>
#include <espnow_peer_manager.h>
#include <mqtt_manager.h>
#include <Arduino.h>
#include <cstring>
#include <esp32common/config/timing_config.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace TxMqttConfigHandlers {

void handle_mqtt_config_request(const espnow_queue_msg_t& msg, uint8_t* receiver_mac) {
    if (msg.len < (int)sizeof(mqtt_config_request_t)) {
        LOG_ERROR("MQTT_CFG", "Invalid request message size: %d bytes", msg.len);
        return;
    }

    auto& conn_mgr = EspNowConnectionManager::instance();
    auto state = conn_mgr.get_state();

    if (state != EspNowConnectionState::CONNECTED) {
        LOG_WARN("MQTT_CFG", "Cannot respond to MQTT config request - receiver state is %u (need CONNECTED)",
                 (uint8_t)state);
        return;
    }

    memcpy(receiver_mac, msg.mac, 6);

    LOG_INFO("MQTT_CFG", "Received MQTT config request from receiver");

    send_mqtt_config_ack(receiver_mac, true, "Current configuration");
}

void handle_mqtt_config_update(const espnow_queue_msg_t& msg, uint8_t* receiver_mac) {
    if (msg.len < (int)sizeof(mqtt_config_update_t)) {
        LOG_ERROR("MQTT_CFG", "Invalid message size: %d bytes", msg.len);
        return;
    }

    auto& conn_mgr = EspNowConnectionManager::instance();
    auto state = conn_mgr.get_state();

    if (state != EspNowConnectionState::CONNECTED) {
        LOG_WARN("MQTT_CFG", "Cannot respond to MQTT config update - receiver state is %u (need CONNECTED)",
                 (uint8_t)state);
        return;
    }

    const mqtt_config_update_t* config = reinterpret_cast<const mqtt_config_update_t*>(msg.data);

    memcpy(receiver_mac, msg.mac, 6);

    LOG_INFO("MQTT_CFG", "Received MQTT config update:");
    LOG_INFO("MQTT_CFG", "  Enabled: %s", config->enabled ? "YES" : "NO");
    LOG_INFO("MQTT_CFG", "  Server: %d.%d.%d.%d:%d",
             config->server[0], config->server[1], config->server[2], config->server[3],
             config->port);
    LOG_INFO("MQTT_CFG", "  Username: %s", strlen(config->username) > 0 ? config->username : "(none)");
    LOG_INFO("MQTT_CFG", "  Client ID: %s", config->client_id);

    if (config->enabled) {
        if ((config->server[0] == 0 && config->server[1] == 0 &&
             config->server[2] == 0 && config->server[3] == 0) ||
            (config->server[0] == 255 && config->server[1] == 255 &&
             config->server[2] == 255 && config->server[3] == 255)) {
            LOG_ERROR("MQTT_CFG", "Invalid MQTT server address");
            send_mqtt_config_ack(receiver_mac, false, "Invalid server IP");
            return;
        }

        if (config->port < 1 || config->port > 65535) {
            LOG_ERROR("MQTT_CFG", "Invalid port: %d", config->port);
            send_mqtt_config_ack(receiver_mac, false, "Invalid port number");
            return;
        }

        if (strlen(config->client_id) == 0) {
            LOG_ERROR("MQTT_CFG", "Client ID cannot be empty");
            send_mqtt_config_ack(receiver_mac, false, "Client ID required");
            return;
        }
    }

    IPAddress server(config->server[0], config->server[1], config->server[2], config->server[3]);
    if (MqttConfigManager::saveConfig(config->enabled, server, config->port,
                                      config->username, config->password, config->client_id)) {
        LOG_INFO("MQTT_CFG", "✓ Configuration saved to NVS");

        LOG_INFO("MQTT_CFG", "Applying configuration (hot-reload)");
        MqttConfigManager::applyConfig();

        vTaskDelay(pdMS_TO_TICKS(TimingConfig::SETTINGS_UPDATE_DELAY_MS));

        send_mqtt_config_ack(receiver_mac, true, "Config saved and applied");
    } else {
        LOG_ERROR("MQTT_CFG", "✗ Failed to save configuration");
        send_mqtt_config_ack(receiver_mac, false, "NVS save failed");
    }
}

void send_mqtt_config_ack(const uint8_t* receiver_mac, bool success, const char* message) {
    mqtt_config_ack_t ack;
    memset(&ack, 0, sizeof(ack));

    ack.type = msg_mqtt_config_ack;
    ack.success = success ? 1 : 0;
    ack.enabled = MqttConfigManager::isEnabled() ? 1 : 0;

    IPAddress server = MqttConfigManager::getServer();
    ack.server[0] = server[0];
    ack.server[1] = server[1];
    ack.server[2] = server[2];
    ack.server[3] = server[3];

    ack.port = MqttConfigManager::getPort();

    strncpy(ack.username, MqttConfigManager::getUsername(), sizeof(ack.username) - 1);
    ack.username[sizeof(ack.username) - 1] = '\0';

    strncpy(ack.password, MqttConfigManager::getPassword(), sizeof(ack.password) - 1);
    ack.password[sizeof(ack.password) - 1] = '\0';

    strncpy(ack.client_id, MqttConfigManager::getClientId(), sizeof(ack.client_id) - 1);
    ack.client_id[sizeof(ack.client_id) - 1] = '\0';

    ack.connected = MqttConfigManager::isConnected() ? 1 : 0;
    ack.config_version = MqttConfigManager::getConfigVersion();

    strncpy(ack.message, message, sizeof(ack.message) - 1);
    ack.message[sizeof(ack.message) - 1] = '\0';

    ack.checksum = 0;

    if (!EspnowPeerManager::is_peer_registered(receiver_mac)) {
        LOG_WARN("MQTT_CFG", "Receiver not registered as peer, adding now");
        if (!EspnowPeerManager::add_peer(receiver_mac)) {
            LOG_ERROR("MQTT_CFG", "Failed to add receiver as peer");
            return;
        }
    }

    esp_err_t result = TxSendGuard::send_to_receiver_guarded(
        receiver_mac,
        (const uint8_t*)&ack,
        sizeof(ack),
        "mqtt_config_ack"
    );
    if (result == ESP_OK) {
        LOG_INFO("MQTT_CFG", "✓ ACK sent to receiver (success=%d, connected=%d)",
                 ack.success, ack.connected);
    } else {
        LOG_ERROR("MQTT_CFG", "✗ Failed to send ACK: %s", esp_err_to_name(result));
    }
}

} // namespace TxMqttConfigHandlers
