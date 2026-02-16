#include "espnow_callbacks.h"
#include "../common.h"
#include <esp_now.h>
#include <espnow_common.h>
#include "../../lib/webserver/utils/transmitter_manager.h"

void on_espnow_sent(const uint8_t *mac, esp_now_send_status_t status) {
    TransmitterManager::updateSendStatus(status == ESP_NOW_SEND_SUCCESS);
    if (status != ESP_NOW_SEND_SUCCESS) {
        Serial.printf("[ESP-NOW] Send failed to %02X:%02X:%02X:%02X:%02X:%02X\n",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
}

void on_data_recv(const uint8_t *mac, const uint8_t *data, int len) {
    // MINIMAL ISR WORK - just validate and queue the raw message
    if (!data || len < 1 || len > 250) return;
    
    // Prepare queue message with raw data
    espnow_queue_msg_t queue_msg;
    memcpy(queue_msg.data, data, len);
    memcpy(queue_msg.mac, mac, 6);
    queue_msg.len = len;
    queue_msg.timestamp = millis();
    
    // Queue message for processing (non-blocking, from ISR context)
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (xQueueSendFromISR(ESPNow::queue, &queue_msg, &xHigherPriorityTaskWoken) != pdTRUE) {
        // Queue full - message dropped
    }
    
    // Yield to higher priority task if woken
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
