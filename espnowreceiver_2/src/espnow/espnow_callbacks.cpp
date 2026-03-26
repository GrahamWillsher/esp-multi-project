#include "espnow_callbacks.h"
#include "../common.h"
#include <esp_now.h>
#include <esp32common/espnow/common.h>
#include "../../lib/webserver/utils/transmitter_manager.h"

void on_espnow_sent(const uint8_t *mac, esp_now_send_status_t status) {
    TransmitterManager::updateSendStatus(status == ESP_NOW_SEND_SUCCESS);
    if (status != ESP_NOW_SEND_SUCCESS) {
        LOG_WARN("ESP-NOW", "Send failed to %02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
}

void on_data_recv(const uint8_t *mac, const uint8_t *data, int len) {
    // MINIMAL CALLBACK WORK - just validate and queue the raw message
    if (!data || len < 1 || len > 250) return;

    ESPNow::rx_callback_count++;
    
    // Prepare queue message with raw data
    espnow_queue_msg_t queue_msg;
    memcpy(queue_msg.data, data, len);
    memcpy(queue_msg.mac, mac, 6);
    queue_msg.len = len;
    queue_msg.timestamp = millis();
    
    // Queue message for processing (non-blocking, callback runs in Wi-Fi task context)
    if (xQueueSend(ESPNow::queue, &queue_msg, 0) != pdTRUE) {
        // Queue full - message dropped
        ESPNow::rx_queue_drop_count++;
        static uint32_t last_drop_log_ms = 0;
        uint32_t now = millis();
        if (now - last_drop_log_ms > 2000) {
            last_drop_log_ms = now;
            LOG_WARN("ESP-NOW", "RX queue full - message dropped");
        }
    } else {
        UBaseType_t waiting = uxQueueMessagesWaiting(ESPNow::queue);
        if (waiting > ESPNow::rx_queue_high_watermark) {
            ESPNow::rx_queue_high_watermark = waiting;
        }
    }
}
