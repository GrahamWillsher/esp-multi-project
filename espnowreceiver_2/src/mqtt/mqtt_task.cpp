#include "mqtt_client.h"
#include "../lib/webserver/utils/transmitter_manager.h"
#include "../lib/receiver_config/receiver_config_manager.h"
#include "../common.h"
#include "../espnow/rx_state_machine.h"
#include <esp32common/config/timing_config.h>
#include <esp32common/espnow/connection_manager.h>

/**
 * @brief FreeRTOS task for MQTT client
 * Connects to broker and processes incoming messages
 * 
 * Uses receiver's own MQTT configuration from ReceiverNetworkConfig.
 * The receiver subscribes to spec topics published by the transmitter.
 */
void task_mqtt_client(void* parameter) {
    LOG_INFO("MQTT_TASK", "Started");

    // Wait for WiFi and ReceiverNetworkConfig to fully initialise before
    // attempting the first MQTT connection.
    vTaskDelay(pdMS_TO_TICKS(TimingConfig::MQTT_TASK_STARTUP_DELAY_MS));

    // Config fingerprint — tracks the last-applied broker address and port.
    // All-zero initial values differ from any valid address, so the client
    // is always initialised on first run without a separate flag.
    static uint8_t  last_server[4] = {0, 0, 0, 0};
    static uint16_t last_port      = 0;

    static uint32_t last_not_ready_log_ms = 0;
    static uint8_t  last_not_ready_state = 0xFF;

    while (true) {
        // ── ESP-NOW coexistence gate (connection-manager-owned) ────────────
        {
            const bool mqtt_allowed = EspNowConnectionManager::instance().is_connected();

            if (!mqtt_allowed) {
                const uint32_t now_ms = millis();
                const auto espnow_state = RxStateMachine::instance().connection_state();
                const uint32_t hb_age = EspNowConnectionManager::instance().ms_since_last_heartbeat();
                if (static_cast<uint8_t>(espnow_state) != last_not_ready_state ||
                    (now_ms - last_not_ready_log_ms) >= 2000U) {
                    LOG_INFO("MQTT_TASK", "Connection gate blocked MQTT (state=%u hb_age=%lu ms)",
                             static_cast<unsigned>(espnow_state),
                             static_cast<unsigned long>(hb_age));
                    last_not_ready_log_ms = now_ms;
                    last_not_ready_state = static_cast<uint8_t>(espnow_state);
                }
                MqttClient::disconnect();
                vTaskDelay(pdMS_TO_TICKS(TimingConfig::MQTT_TASK_POLL_MS));
                continue;
            }

            last_not_ready_state = 0xFF;
        }
        // ────────────────────────────────────────────────────────────────────

        if (ReceiverNetworkConfig::isMqttEnabled()) {
            const uint8_t* mqtt_server = ReceiverNetworkConfig::getMqttServer();

            // Skip if server is still 0.0.0.0
            if (mqtt_server[0] != 0 || mqtt_server[1] != 0 ||
                mqtt_server[2] != 0 || mqtt_server[3] != 0) {

                const uint16_t mqtt_port = ReceiverNetworkConfig::getMqttPort();
                const bool config_changed =
                    memcmp(last_server, mqtt_server, 4) != 0 || last_port != mqtt_port;

                if (config_changed) {
                    LOG_INFO("MQTT_TASK", "MQTT config changed — (re)initialising client");
                    MqttClient::init(mqtt_server, mqtt_port, "espnow_receiver");

                    const char* username = ReceiverNetworkConfig::getMqttUsername();
                    if (username && username[0] != '\0') {
                        MqttClient::setAuth(username, ReceiverNetworkConfig::getMqttPassword());
                    }

                    MqttClient::setEnabled(true);
                    memcpy(last_server, mqtt_server, 4);
                    last_port = mqtt_port;
                }

                MqttClient::loop();
            } else {
                MqttClient::setEnabled(false);
            }
        } else {
            MqttClient::setEnabled(false);
        }

        // Yield — poll at TimingConfig::MQTT_TASK_POLL_MS cadence (10x/sec).
        vTaskDelay(pdMS_TO_TICKS(TimingConfig::MQTT_TASK_POLL_MS));
    }
}
