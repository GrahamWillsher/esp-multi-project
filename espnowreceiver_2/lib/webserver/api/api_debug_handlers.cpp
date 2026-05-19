#include "api_debug_handlers.h"

#include "../../src/espnow/espnow_send.h"
#include "../../src/mqtt/mqtt_client.h"
#include "api_response_utils.h"
#include "../utils/transmitter_manager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <cstdio>
#include <esp32common/mqtt/mqtt_feature_flags.h>

namespace {
constexpr const char* MQTT_TOPIC_RX_CMD_CONTROL_DEBUG_LEVEL = "batt-emu/mqtt-v1/rx/cmd/control/debug_level";
constexpr const char* MQTT_TOPIC_TX_ACK_CONTROL = "batt-emu/mqtt-v1/tx/ack/control";
}

esp_err_t api_get_debug_level_handler(httpd_req_t *req) {
    uint8_t level = get_last_debug_level();
    const char* level_names[] = {"EMERG", "ALERT", "CRIT", "ERROR", "WARNING", "NOTICE", "INFO", "DEBUG"};

    return ApiResponseUtils::send_jsonf(req,
                                        "{\"success\":true,\"level\":%d,\"level_name\":\"%s\"}",
                                        level,
                                        level_names[level]);
}

esp_err_t api_set_debug_level_handler(httpd_req_t *req) {
    char buf[64];
    int ret = httpd_req_get_url_query_str(req, buf, sizeof(buf));

    if (ret == ESP_OK) {
        char param[32];
        if (httpd_query_key_value(buf, "level", param, sizeof(param)) == ESP_OK) {
            uint8_t level = atoi(param);

            if (level > 7) {
                return ApiResponseUtils::send_error_message(req, "Invalid debug level (must be 0-7)");
            }

            bool command_sent = false;

#if MQTT_FEATURE_COMMANDS
            if (MqttClient::isEnabled() && MqttClient::isConnected()) {
                StaticJsonDocument<128> cmd;
                char request_id[32];
                snprintf(request_id, sizeof(request_id), "dbg-%lu", static_cast<unsigned long>(millis()));
                cmd["request_id"] = request_id;
                cmd["origin"] = "receiver_tft";
                cmd["schema"] = 1;
                cmd["level"] = level;

                char payload[128];
                const size_t n = serializeJson(cmd, payload, sizeof(payload));
                if (n > 0) {
                    char ack_payload[384] = {0};
                    command_sent = MqttClient::publishJsonAndWaitForAck(MQTT_TOPIC_RX_CMD_CONTROL_DEBUG_LEVEL,
                                                                        payload,
                                                                        request_id,
                                                                        MQTT_TOPIC_TX_ACK_CONTROL,
                                                                        1500,
                                                                        ack_payload,
                                                                        sizeof(ack_payload));
                    if (command_sent) {
                        StaticJsonDocument<256> ack_doc;
                        if (deserializeJson(ack_doc, ack_payload) == DeserializationError::Ok) {
                            const bool success = ack_doc["success"] | false;
                            const char* message = ack_doc["message"] | "";
                            if (!success) {
                                return ApiResponseUtils::send_error_message(req,
                                                                            message[0] != '\0'
                                                                                ? message
                                                                                : "Debug level rejected by transmitter");
                            }
                        }
                    }
                }
            }
#endif

            if (command_sent) {
                const char* level_names[] = {"EMERG", "ALERT", "CRIT", "ERROR", "WARNING", "NOTICE", "INFO", "DEBUG"};
                return ApiResponseUtils::send_jsonf(req,
                                                    "{\"success\":true,\"message\":\"Debug level command sent: %d (%s)\",\"level\":%d}",
                                                    level,
                                                    level_names[level],
                                                    level);
            }

            return ApiResponseUtils::send_error_message(req, "MQTT command channel unavailable");
        }
    }

    return ApiResponseUtils::send_error_with_status(req, "400 Bad Request", "Missing level parameter");
}
