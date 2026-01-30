#include "espnow_transmitter.h"
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
        Serial.printf("Peer added on channel %d\n", channel);
    } else {
        Serial.printf("ERROR: Failed to add peer (error %d)\n", result);
    }
    return result == ESP_OK;
}

bool send_probe(uint32_t seq) {
    probe_t p{ msg_probe, seq };
    return esp_now_send(receiver_mac, reinterpret_cast<uint8_t*>(&p), sizeof(p)) == ESP_OK;
}

int hop_and_lock_channel(uint8_t* out_channel, uint8_t attempts_per_channel, uint16_t ack_wait_ms) {
    Serial.println("Starting full channel sweep...");
    for (uint8_t ch : k_channels) {
        Serial.printf("Trying channel %d... ", ch);
        if (!set_channel(ch)) {
            Serial.println("failed to set channel");
            continue;
        }
        if (!ensure_peer_added(0)) {
            Serial.println("failed to add peer");
            continue;
        }
        for (uint8_t a = 0; a < attempts_per_channel; ++a) {
            g_ack_received = false;
            g_ack_seq = (uint32_t)esp_random();
            if (!send_probe(g_ack_seq)) {
                Serial.print("send fail ");
                continue;
            }
            Serial.printf("probe sent (seq=%u), waiting... ", g_ack_seq);
            uint32_t t0 = millis();
            while (!g_ack_received && (millis() - t0) < ack_wait_ms) {
                delay(1);
            }
            if (g_ack_received) {
                Serial.printf("\nACK received! Locking to channel %d\n", g_lock_channel);
                *out_channel = g_lock_channel;
                return g_lock_channel;
            }
        }
        Serial.println("no ACK");
    }
    Serial.println("Channel sweep complete - no gateway found");
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
    Serial.print("Last Packet Send Status: ");
    if (status == ESP_NOW_SEND_SUCCESS) {
        Serial.println("Delivery Success");
    } else {
        Serial.printf("Delivery Fail (status=%d)\n", status);
        if (!esp_now_is_peer_exist(receiver_mac)) {
            Serial.println("ERROR: Peer lost! Re-adding...");
            ensure_peer_added(g_lock_channel);
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
    Serial.println("\n--- Sending ESP-NOW Data ---");
    Serial.printf("Channel: %d (locked to: %d)\n", current_ch, g_lock_channel);
    Serial.printf("SOC: %d%%, Power: %d W, Checksum: %u\n", tx_data.soc, tx_data.power, tx_data.checksum);
    esp_err_t result = esp_now_send(receiver_mac, (uint8_t*)&tx_data, sizeof(tx_data));
    if (result == ESP_OK) {
        Serial.println("Sent with success");
    } else {
        Serial.println("Error sending the data");
    }
}

// ============================================================================
// INITIALIZATION AND UTILITY FUNCTIONS
// ============================================================================

void init_wifi() {
    Serial.println("═══════════════════════════════════════════════");
    Serial.print("ESPNOW TRANSMITTER LIBRARY VERSION: ");
    Serial.println(ESPNOW_TRANSMITTER_VERSION);
    Serial.println("═══════════════════════════════════════════════");
    WiFi.mode(WIFI_STA);
    delay(100);
    Serial.print("Transmitter MAC Address: ");
    Serial.println(WiFi.macAddress());
    esp_wifi_set_ps(WIFI_PS_NONE);
}

void init_espnow(QueueHandle_t rx_queue) {
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        ESP.restart();
    }
    Serial.println("ESP-NOW initialized successfully");
    
    // Use queue provided by application
    espnow_rx_queue = rx_queue;
    if (espnow_rx_queue == NULL) {
        Serial.println("ERROR: ESP-NOW RX queue is NULL");
        ESP.restart();
    }
    
    esp_now_register_recv_cb(on_espnow_recv);
    esp_now_register_send_cb(on_data_sent);
}

void discover_and_lock_channel() {
    uint8_t locked = 0;
    int found = hop_and_lock_channel(&locked);
    if (found > 0) {
        Serial.printf("Locked to channel %d\n", found);
        g_lock_channel = locked;
        if (!set_channel(locked)) {
            Serial.printf("ERROR: Failed to set channel to %d\n", locked);
        }
        delay(100);
        ensure_peer_added(locked);
    } else {
        Serial.println("No receiver found during initial discovery");
        Serial.println("Using WiFi channel - bidirectional announcements will establish connection");
        // Use current WiFi channel instead of forcing channel 1
        uint8_t current_ch = 0;
        wifi_second_chan_t second;
        esp_wifi_get_channel(&current_ch, &second);
        g_lock_channel = current_ch;
        Serial.printf("Using WiFi channel %d for ESP-NOW\n", current_ch);
        delay(100);
        // Don't add peer yet - will be added when receiver responds to our announcements
    }
}
