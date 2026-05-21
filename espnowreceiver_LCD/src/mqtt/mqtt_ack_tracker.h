#ifndef MQTT_ACK_TRACKER_H
#define MQTT_ACK_TRACKER_H

#include <Arduino.h>
#include <cstddef>
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class MqttAckTracker {
public:
    struct AckResult {
        bool received{false};
        bool success{false};
        char request_id[40]{};
        char topic[80]{};
        char code[32]{};
        char message[128]{};
        char payload[512]{};
        uint32_t version{0};
    };

    static void handleAckMessage(const char* topic, const char* json_payload, size_t length);
    static bool waitForAck(const char* request_id, uint32_t timeout_ms, AckResult* out_result = nullptr);

private:
    struct AckEntry {
        bool in_use{false};
        AckResult result{};
        uint32_t ts_ms{0};
    };

    static AckEntry entries_[8];
    static SemaphoreHandle_t mutex_;
    static void ensureMutex();
    static void storeAck(const AckResult& result);
    static bool findAck(const char* request_id, AckResult* out_result);
};

#endif
