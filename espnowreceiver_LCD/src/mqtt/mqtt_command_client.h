#ifndef MQTT_COMMAND_CLIENT_H
#define MQTT_COMMAND_CLIENT_H

#include "mqtt_ack_tracker.h"
#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>

class MqttCommandClient {
public:
    enum class CommandResult : uint8_t {
        AckReceived,
        AckTimeout,
        PublishFailed,
        ChannelUnavailable,
    };

    static bool sendNetworkUpdate(bool use_static_ip,
                                  const uint8_t ip[4],
                                  const uint8_t gateway[4],
                                  const uint8_t subnet[4],
                                  const uint8_t dns_primary[4],
                                  const uint8_t dns_secondary[4],
                                  uint32_t timeout_ms,
                                  MqttAckTracker::AckResult* out_ack = nullptr,
                                  CommandResult* out_result = nullptr);

    static bool sendMqttUpdate(bool enabled,
                               const uint8_t server[4],
                               uint16_t port,
                               const char* username,
                               const char* password,
                               const char* client_id,
                               uint32_t timeout_ms,
                               MqttAckTracker::AckResult* out_ack = nullptr,
                               CommandResult* out_result = nullptr);

    static bool sendSettingsUpdate(uint8_t category,
                                   uint8_t field,
                                   JsonVariantConst value,
                                   uint32_t timeout_ms,
                                   MqttAckTracker::AckResult* out_ack = nullptr,
                                   CommandResult* out_result = nullptr);

    static bool sendDebugLevel(int level,
                               uint32_t timeout_ms,
                               MqttAckTracker::AckResult* out_ack = nullptr,
                               CommandResult* out_result = nullptr);

    static bool sendTestDataMode(uint8_t mode);

    static bool sendEventLogsClear();

    static bool sendReboot(bool confirm,
                           uint32_t timeout_ms,
                           MqttAckTracker::AckResult* out_ack = nullptr,
                           CommandResult* out_result = nullptr);

    static bool sendComponentApply(uint32_t numeric_request_id,
                                   uint8_t apply_mask,
                                   uint8_t battery_type,
                                   uint8_t inverter_type,
                                   uint8_t battery_interface,
                                   uint8_t inverter_interface,
                                   uint32_t timeout_ms,
                                   MqttAckTracker::AckResult* out_ack = nullptr,
                                   CommandResult* out_result = nullptr);

private:
    static void makeRequestId(const char* prefix, char* out, size_t out_len);
};

#endif
