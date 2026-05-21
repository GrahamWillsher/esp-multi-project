#include "api_debug_handlers.h"

#include "../../src/mqtt/mqtt_command_client.h"
#include "../../src/mqtt/mqtt_client.h"
#include "../../src/mqtt/control_state_compat.h"
#include "api_response_utils.h"
#include "../utils/transmitter_manager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp32common/config/timing_config.h>
#include <cstdio>
#include <esp32common/mqtt/mqtt_feature_flags.h>

namespace {
constexpr const char* MQTT_TOPIC_RX_CMD_CONTROL_DEBUG_LEVEL = "batt-emu/mqtt-v1/rx/cmd/control/debug_level";
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

#if MQTT_FEATURE_COMMANDS
            if (MqttClient::isEnabled() && MqttClient::isConnected()) {
                MqttAckTracker::AckResult ack_result;
                MqttCommandClient::CommandResult command_result = MqttCommandClient::CommandResult::ChannelUnavailable;
                const bool command_sent = MqttCommandClient::sendDebugLevel(level,
                                                                            TimingConfig::MQTT_COMMAND_ACK_WAIT_TIMEOUT_MS,
                                                                            &ack_result,
                                                                            &command_result);
                if (command_sent && !ack_result.success) {
                    return ApiResponseUtils::send_error_message(req,
                                                                ack_result.message[0] != '\0'
                                                                    ? ack_result.message
                                                                    : "Debug level rejected by transmitter");
                }
                if (command_sent) {
                    set_last_debug_level(level);
                    const char* level_names[] = {"EMERG", "ALERT", "CRIT", "ERROR", "WARNING", "NOTICE", "INFO", "DEBUG"};
                    return ApiResponseUtils::send_jsonf(req,
                                                        "{\"success\":true,\"message\":\"Debug level command sent: %d (%s)\",\"level\":%d}",
                                                        level,
                                                        level_names[level],
                                                        level);
                }

                if (command_result == MqttCommandClient::CommandResult::AckTimeout) {
                    return ApiResponseUtils::send_error_message(req, "Timed out waiting for transmitter ACK");
                }
                if (command_result == MqttCommandClient::CommandResult::PublishFailed) {
                    return ApiResponseUtils::send_error_message(req, "Failed to publish MQTT command");
                }
            }
#endif

            return ApiResponseUtils::send_error_message(req, "MQTT command channel unavailable");
        }
    }

    return ApiResponseUtils::send_error_with_status(req, "400 Bad Request", "Missing level parameter");
}
