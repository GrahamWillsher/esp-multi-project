#include "mqtt_client.h"
#include "../../lib/webserver_lcd/utils/transmitter_manager.h"
#include "../../lib/receiver_config/receiver_config_manager.h"
#include "../../include/common_lcd.h"
#include "logging_config.h"
#include <esp32common/config/timing_config.h>

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

    while (true) {
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
                    MqttClient::init(mqtt_server, mqtt_port, "battery_emulator_receiver");

                    const char* username = ReceiverNetworkConfig::getMqttUsername();
                    const bool has_auth = (username && username[0] != '\0');
                    MqttClient::setAuth(has_auth ? username : nullptr,
                                        has_auth ? ReceiverNetworkConfig::getMqttPassword() : nullptr);

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
