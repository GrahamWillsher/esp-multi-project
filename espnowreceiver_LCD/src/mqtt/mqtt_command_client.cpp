#include "mqtt_command_client.h"

#include "mqtt_client.h"
#include "../../include/mqtt/mqtt_topics_receiver.h"
#include "logging_config.h"
#include <esp32common/espnow/common.h>
#include <ArduinoJson.h>
#include <cstdio>

namespace {
void format_ipv4(char* out, size_t out_len, const uint8_t ip[4]) {
    snprintf(out, out_len, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

void set_command_result(MqttCommandClient::CommandResult* out_result,
                        MqttCommandClient::CommandResult result) {
    if (out_result != nullptr) {
        *out_result = result;
    }
}
}

void MqttCommandClient::makeRequestId(const char* prefix, char* out, size_t out_len) {
    snprintf(out, out_len, "%s-%lu", prefix ? prefix : "req", static_cast<unsigned long>(millis()));
}

bool MqttCommandClient::sendNetworkUpdate(bool use_static_ip,
                                          const uint8_t ip[4],
                                          const uint8_t gateway[4],
                                          const uint8_t subnet[4],
                                          const uint8_t dns_primary[4],
                                          const uint8_t dns_secondary[4],
                                          uint32_t timeout_ms,
                                          MqttAckTracker::AckResult* out_ack,
                                          CommandResult* out_result) {
    if (!MqttClient::isEnabled() || !MqttClient::isConnected()) {
        set_command_result(out_result, CommandResult::ChannelUnavailable);
        return false;
    }

    StaticJsonDocument<384> cmd;
    char request_id[32];
    makeRequestId("net", request_id, sizeof(request_id));

    cmd["request_id"] = request_id;
    cmd["origin"] = "receiver_lcd";
    cmd["schema"] = 1;
    cmd["use_static_ip"] = use_static_ip;

    char ip_str[16], gateway_str[16], subnet_str[16], dns1_str[16], dns2_str[16];
    if (use_static_ip) {
        format_ipv4(ip_str, sizeof(ip_str), ip);
        format_ipv4(gateway_str, sizeof(gateway_str), gateway);
        format_ipv4(subnet_str, sizeof(subnet_str), subnet);
        format_ipv4(dns1_str, sizeof(dns1_str), dns_primary);
        format_ipv4(dns2_str, sizeof(dns2_str), dns_secondary);
        cmd["ip"] = ip_str;
        cmd["gateway"] = gateway_str;
        cmd["subnet"] = subnet_str;
        cmd["dns_primary"] = dns1_str;
        cmd["dns_secondary"] = dns2_str;
    }

    char payload[384];
    const size_t n = serializeJson(cmd, payload, sizeof(payload));
    if (n == 0 || !MqttClient::publishJson(MqttTopicsReceiver::RxCmd::UPDATE_NETWORK, payload, false)) {
        set_command_result(out_result, CommandResult::PublishFailed);
        return false;
    }

    const bool ack_received = MqttAckTracker::waitForAck(request_id, timeout_ms, out_ack);
    set_command_result(out_result, ack_received ? CommandResult::AckReceived : CommandResult::AckTimeout);
    return ack_received;
}

bool MqttCommandClient::sendMqttUpdate(bool enabled,
                                       const uint8_t server[4],
                                       uint16_t port,
                                       const char* username,
                                       const char* password,
                                       const char* client_id,
                                       uint32_t timeout_ms,
                                       MqttAckTracker::AckResult* out_ack,
                                       CommandResult* out_result) {
    if (!MqttClient::isEnabled() || !MqttClient::isConnected()) {
        set_command_result(out_result, CommandResult::ChannelUnavailable);
        return false;
    }

    StaticJsonDocument<384> cmd;
    char request_id[32];
    makeRequestId("mqcfg", request_id, sizeof(request_id));

    char server_ip[16];
    format_ipv4(server_ip, sizeof(server_ip), server);

    cmd["request_id"] = request_id;
    cmd["origin"] = "receiver_lcd";
    cmd["schema"] = 1;
    cmd["enabled"] = enabled;
    cmd["server"] = server_ip;
    cmd["port"] = port;
    cmd["username"] = username ? username : "";
    cmd["password"] = password ? password : "";
    cmd["client_id"] = client_id ? client_id : "";

    char payload[384];
    const size_t n = serializeJson(cmd, payload, sizeof(payload));
    if (n == 0 || !MqttClient::publishJson(MqttTopicsReceiver::RxCmd::UPDATE_MQTT, payload, false)) {
        set_command_result(out_result, CommandResult::PublishFailed);
        return false;
    }

    const bool ack_received = MqttAckTracker::waitForAck(request_id, timeout_ms, out_ack);
    set_command_result(out_result, ack_received ? CommandResult::AckReceived : CommandResult::AckTimeout);
    return ack_received;
}

bool MqttCommandClient::sendSettingsUpdate(uint8_t category,
                                           uint8_t field,
                                           JsonVariantConst value,
                                           uint32_t timeout_ms,
                                           MqttAckTracker::AckResult* out_ack,
                                           CommandResult* out_result) {
    if (!MqttClient::isEnabled() || !MqttClient::isConnected()) {
        set_command_result(out_result, CommandResult::ChannelUnavailable);
        return false;
    }

    StaticJsonDocument<256> cmd;
    char request_id[32];
    makeRequestId("set", request_id, sizeof(request_id));

    cmd["request_id"] = request_id;
    cmd["origin"] = "receiver_lcd";
    cmd["schema"] = 1;
    cmd["category"] = category;
    cmd["field"] = field;

    const bool battery_float_field =
        (category == SETTINGS_BATTERY) &&
        (field == BATTERY_MAX_CHARGE_CURRENT_A || field == BATTERY_MAX_DISCHARGE_CURRENT_A);
    if (battery_float_field) {
        cmd["value"] = value.as<float>();
    } else {
        cmd["value"] = value;
    }

    char payload[256];
    const size_t n = serializeJson(cmd, payload, sizeof(payload));
    if (n == 0 || !MqttClient::publishJson(MqttTopicsReceiver::RxCmd::UPDATE_BATTERY, payload, false)) {
        set_command_result(out_result, CommandResult::PublishFailed);
        return false;
    }

    const bool ack_received = MqttAckTracker::waitForAck(request_id, timeout_ms, out_ack);
    set_command_result(out_result, ack_received ? CommandResult::AckReceived : CommandResult::AckTimeout);
    return ack_received;
}

bool MqttCommandClient::sendDebugLevel(int level,
                                       uint32_t timeout_ms,
                                       MqttAckTracker::AckResult* out_ack,
                                       CommandResult* out_result) {
    if (!MqttClient::isEnabled() || !MqttClient::isConnected()) {
        set_command_result(out_result, CommandResult::ChannelUnavailable);
        return false;
    }

    StaticJsonDocument<160> cmd;
    char request_id[32];
    makeRequestId("dbg", request_id, sizeof(request_id));

    cmd["request_id"] = request_id;
    cmd["origin"] = "receiver_lcd";
    cmd["schema"] = 1;
    cmd["level"] = level;

    char payload[160];
    const size_t n = serializeJson(cmd, payload, sizeof(payload));
    if (n == 0 || !MqttClient::publishJson(MqttTopicsReceiver::RxCmd::CONTROL_DEBUG_LEVEL, payload, false)) {
        set_command_result(out_result, CommandResult::PublishFailed);
        return false;
    }

    const bool ack_received = MqttAckTracker::waitForAck(request_id, timeout_ms, out_ack);
    set_command_result(out_result, ack_received ? CommandResult::AckReceived : CommandResult::AckTimeout);
    return ack_received;
}

bool MqttCommandClient::sendTestDataMode(uint8_t mode) {
    if (!MqttClient::isEnabled() || !MqttClient::isConnected()) {
        return false;
    }

    StaticJsonDocument<160> cmd;
    char request_id[32];
    makeRequestId("tdm", request_id, sizeof(request_id));

    cmd["request_id"] = request_id;
    cmd["origin"] = "receiver_lcd";
    cmd["schema"] = 1;
    cmd["mode"] = mode;

    char payload[160];
    const size_t n = serializeJson(cmd, payload, sizeof(payload));
    if (n == 0) {
        return false;
    }

    return MqttClient::publishJson(MqttTopicsReceiver::RxCmd::CONTROL_TEST_DATA_MODE, payload, false);
}

bool MqttCommandClient::sendEventLogsClear() {
    if (!MqttClient::isEnabled() || !MqttClient::isConnected()) {
        return false;
    }

    StaticJsonDocument<160> cmd;
    char request_id[32];
    makeRequestId("elogclr", request_id, sizeof(request_id));

    cmd["request_id"] = request_id;
    cmd["origin"] = "receiver_lcd";
    cmd["schema"] = 1;
    cmd["confirm"] = true;

    char payload[160];
    const size_t n = serializeJson(cmd, payload, sizeof(payload));
    if (n == 0) {
        return false;
    }

    return MqttClient::publishJson(MqttTopicsReceiver::RxCmd::CONTROL_EVENT_LOGS_CLEAR, payload, false);
}

bool MqttCommandClient::sendReboot(bool confirm,
                                   uint32_t timeout_ms,
                                   MqttAckTracker::AckResult* out_ack,
                                   CommandResult* out_result) {
    if (!MqttClient::isEnabled() || !MqttClient::isConnected()) {
        set_command_result(out_result, CommandResult::ChannelUnavailable);
        return false;
    }

    StaticJsonDocument<160> cmd;
    char request_id[32];
    makeRequestId("rb", request_id, sizeof(request_id));

    cmd["request_id"] = request_id;
    cmd["origin"] = "receiver_lcd";
    cmd["schema"] = 1;
    cmd["confirm"] = confirm;

    char payload[160];
    const size_t n = serializeJson(cmd, payload, sizeof(payload));
    if (n == 0 || !MqttClient::publishJson(MqttTopicsReceiver::RxCmd::CONTROL_REBOOT, payload, false)) {
        set_command_result(out_result, CommandResult::PublishFailed);
        return false;
    }

    const bool ack_received = MqttAckTracker::waitForAck(request_id, timeout_ms, out_ack);
    set_command_result(out_result, ack_received ? CommandResult::AckReceived : CommandResult::AckTimeout);
    return ack_received;
}

bool MqttCommandClient::sendComponentApply(uint32_t numeric_request_id,
                                           uint8_t apply_mask,
                                           uint8_t battery_type,
                                           uint8_t inverter_type,
                                           uint8_t battery_interface,
                                           uint8_t inverter_interface,
                                           uint32_t timeout_ms,
                                           MqttAckTracker::AckResult* out_ack,
                                           CommandResult* out_result) {
    if (!MqttClient::isEnabled() || !MqttClient::isConnected()) {
        set_command_result(out_result, CommandResult::ChannelUnavailable);
        return false;
    }

    StaticJsonDocument<256> cmd;
    char request_id[16];
    snprintf(request_id, sizeof(request_id), "%lu", static_cast<unsigned long>(numeric_request_id));

    cmd["request_id"] = request_id;
    cmd["origin"] = "receiver_lcd";
    cmd["schema"] = 1;
    cmd["apply_mask"] = apply_mask;
    cmd["battery_type"] = battery_type;
    cmd["inverter_type"] = inverter_type;
    cmd["battery_interface"] = battery_interface;
    cmd["inverter_interface"] = inverter_interface;
    cmd["confirm"] = true;

    char payload[256];
    const size_t n = serializeJson(cmd, payload, sizeof(payload));
    if (n == 0 || !MqttClient::publishJson(MqttTopicsReceiver::RxCmd::CONTROL_COMPONENT_APPLY, payload, false)) {
        set_command_result(out_result, CommandResult::PublishFailed);
        return false;
    }

    const bool ack_received = MqttAckTracker::waitForAck(request_id, timeout_ms, out_ack);
    set_command_result(out_result, ack_received ? CommandResult::AckReceived : CommandResult::AckTimeout);
    return ack_received;
}
