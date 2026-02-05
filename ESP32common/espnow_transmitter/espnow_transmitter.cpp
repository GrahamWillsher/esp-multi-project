#include "espnow_transmitter.h"
#include "../logging_utilities/mqtt_logger.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

// VERSION MARKER - Force rebuild detection
#define ESPNOW_TRANSMITTER_VERSION "v2.0-request-abort-20260122"

// Initialize receiver_mac to broadcast - will be updated when receiver sends PROBE
uint8_t receiver_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const uint8_t k_channels[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
volatile bool g_ack_received = false;
volatile uint32_t g_ack_seq = 0;
volatile uint8_t g_lock_channel = 0;
espnow_payload_t tx_data;
// espnow_rx_queue is defined in main.cpp (project-specific)

uint8_t requester_mac[6] = {0};  // Track who requested data

uint16_t calculate_checksum(espnow_payload_t* data) {
    return (uint16_t)(data->soc + data->power);
}

bool set_channel(uint8_t ch) {
    return esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE) == ESP_OK;
}

bool ensure_peer_added(uint8_t channel) {
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, receiver_mac, 6);
    peer.ifidx = WIFI_IF_STA;
    peer.channel = channel;
    peer.encrypt = false;
    if (esp_now_is_peer_exist(receiver_mac)) {
        esp_now_del_peer(receiver_mac);
    }
    esp_err_t result = esp_now_add_peer(&peer);
    if (result == ESP_OK) {
        MQTT_LOG_DEBUG("ESPNOW_TX", "Peer added on channel %d", channel);
    } else {
        MQTT_LOG_ERROR("ESPNOW_TX", "Failed to add peer (error %d)", result);
    }
    return result == ESP_OK;
}

bool send_probe(uint32_t seq) {
    probe_t p{ msg_probe, seq };
    return esp_now_send(receiver_mac, reinterpret_cast<uint8_t*>(&p), sizeof(p)) == ESP_OK;
}

int hop_and_lock_channel(uint8_t* out_channel, uint8_t attempts_per_channel, uint16_t ack_wait_ms) {
    MQTT_LOG_INFO("ESPNOW_TX", "Starting full channel sweep...");
    for (uint8_t ch : k_channels) {
        MQTT_LOG_DEBUG("ESPNOW_TX", "Trying channel %d...", ch);
        if (!set_channel(ch)) {
            MQTT_LOG_ERROR("ESPNOW_TX", "Failed to set channel");
            continue;
        }
        if (!ensure_peer_added(ch)) {
            MQTT_LOG_ERROR("ESPNOW_TX", "Failed to add peer");
            continue;
        }
        for (uint8_t a = 0; a < attempts_per_channel; ++a) {
            g_ack_received = false;
            g_ack_seq = (uint32_t)esp_random();
            if (!send_probe(g_ack_seq)) {
                MQTT_LOG_ERROR("ESPNOW_TX", "Send probe failed");
                continue;
            }
            MQTT_LOG_DEBUG("ESPNOW_TX", "Probe sent (seq=%u), waiting...", g_ack_seq);
            uint32_t t0 = millis();
            while (!g_ack_received && (millis() - t0) < ack_wait_ms) {
                delay(1);
            }
            if (g_ack_received) {
                MQTT_LOG_INFO("ESPNOW_TX", "ACK received! Locking to channel %d", g_lock_channel);
                *out_channel = g_lock_channel;
                return g_lock_channel;
            }
        }
        MQTT_LOG_DEBUG("ESPNOW_TX", "No ACK on channel %d", ch);
    }
    MQTT_LOG_WARNING("ESPNOW_TX", "Channel sweep complete - no gateway found");
    return 0;
}

void on_espnow_recv(const uint8_t *mac_addr, const uint8_t *data, int len) {
    if (!data || len < 1) return;
    if (espnow_rx_queue != NULL) {
        espnow_queue_msg_t msg;
        memcpy(msg.data, data, min(len, 250));
        memcpy(msg.mac, mac_addr, 6);
        msg.len = len;
        msg.timestamp = millis();
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(espnow_rx_queue, &msg, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    }
}

void on_data_sent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    
    if (status == ESP_NOW_SEND_SUCCESS) {
        MQTT_LOG_DEBUG("ESPNOW_TX", "✓ Delivery success to %s", mac_str);
    } else {
        MQTT_LOG_WARNING("ESPNOW_TX", "✗ Delivery FAILED to %s (status=%d)", mac_str, status);
        
        // Check if peer still exists
        if (!esp_now_is_peer_exist(receiver_mac)) {
            MQTT_LOG_ERROR("ESPNOW_TX", "Peer %s lost! Re-adding...", mac_str);
            ensure_peer_added(g_lock_channel);
        } else {
            MQTT_LOG_INFO("ESPNOW_TX", "Peer %s still registered (channel=%d)", mac_str, g_lock_channel);
        }
    }
}

// ============================================================================
// UTILITY FUNCTIONS FOR APPLICATIONS
// ============================================================================

void send_test_data() {
    // Note: Application should check g_data_transmission_active before calling
    static bool soc_increasing = true;
    tx_data.type = msg_data;
    if (soc_increasing) {
        tx_data.soc += 1;
        if (tx_data.soc >= 80) soc_increasing = false;
    } else {
        tx_data.soc -= 1;
        if (tx_data.soc <= 20) soc_increasing = true;
    }
    tx_data.power = random(-4000, 4001);
    tx_data.checksum = calculate_checksum(&tx_data);
    uint8_t current_ch = 0;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&current_ch, &second);
    MQTT_LOG_DEBUG("ESPNOW_TX", "Sending data - Ch:%d Lock:%d SOC:%d%% Power:%dW Chk:%u", 
                   current_ch, g_lock_channel, tx_data.soc, tx_data.power, tx_data.checksum);
    esp_err_t result = esp_now_send(receiver_mac, (uint8_t*)&tx_data, sizeof(tx_data));
    if (result == ESP_OK) {
        MQTT_LOG_DEBUG("ESPNOW_TX", "Sent with success");
    } else {
        MQTT_LOG_ERROR("ESPNOW_TX", "Error sending the data");
    }
}

// ============================================================================
// INITIALIZATION AND UTILITY FUNCTIONS
// ============================================================================

void init_wifi() {
    MQTT_LOG_INFO("ESPNOW_TX", "ESPNOW TRANSMITTER LIBRARY VERSION: %s", ESPNOW_TRANSMITTER_VERSION);
    WiFi.mode(WIFI_STA);
    delay(100);
    MQTT_LOG_INFO("ESPNOW_TX", "Transmitter MAC Address: %s", WiFi.macAddress().c_str());
    esp_wifi_set_ps(WIFI_PS_NONE);
}

void init_espnow(QueueHandle_t rx_queue) {
    if (esp_now_init() != ESP_OK) {
        MQTT_LOG_CRIT("ESPNOW_TX", "Error initializing ESP-NOW - restarting");
        ESP.restart();
    }
    MQTT_LOG_INFO("ESPNOW_TX", "ESP-NOW initialized successfully");
    
    // Use queue provided by application
    espnow_rx_queue = rx_queue;
    if (espnow_rx_queue == NULL) {
        MQTT_LOG_CRIT("ESPNOW_TX", "ESP-NOW RX queue is NULL - restarting");
        ESP.restart();
    }
    
    esp_now_register_recv_cb(on_espnow_recv);
    esp_now_register_send_cb(on_data_sent);
}

void discover_and_lock_channel() {
    uint8_t locked = 0;
    int found = hop_and_lock_channel(&locked);
    if (found > 0) {
        MQTT_LOG_INFO("ESPNOW_TX", "Locked to channel %d", found);
        g_lock_channel = locked;
        
        // Ensure we're on the correct channel
        if (!set_channel(locked)) {
            MQTT_LOG_ERROR("ESPNOW_TX", "Failed to set channel to %d", locked);
        }
        
        // Verify channel was actually set
        uint8_t current_ch = 0;
        wifi_second_chan_t second;
        esp_wifi_get_channel(&current_ch, &second);
        MQTT_LOG_INFO("ESPNOW_TX", "Current WiFi channel: %d (locked: %d)", current_ch, locked);
        
        delay(100);
        
        // Re-add peer with correct channel (removes old peer first)
        ensure_peer_added(locked);
        
        MQTT_LOG_INFO("ESPNOW_TX", "Channel lock complete - using channel %d", locked);
    } else {
        MQTT_LOG_WARNING("ESPNOW_TX", "No receiver found during initial discovery");
        MQTT_LOG_INFO("ESPNOW_TX", "Using WiFi channel - bidirectional announcements will establish connection");
        // Use current WiFi channel instead of forcing channel 1
        uint8_t current_ch = 0;
        wifi_second_chan_t second;
        esp_wifi_get_channel(&current_ch, &second);
        g_lock_channel = current_ch;
        MQTT_LOG_INFO("ESPNOW_TX", "Using WiFi channel %d for ESP-NOW", current_ch);
        delay(100);
        // Don't add peer yet - will be added when receiver responds to our announcements
    }
}
