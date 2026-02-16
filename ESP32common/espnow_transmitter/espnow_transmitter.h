#ifndef ESPNOW_TRANSMITTER_H
#define ESPNOW_TRANSMITTER_H

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "espnow_common.h"

// ============================================================================
// EXTERNAL VARIABLES
// ============================================================================

extern volatile bool g_ack_received;
extern volatile uint32_t g_ack_seq;
extern volatile uint8_t g_lock_channel;
extern espnow_payload_t tx_data;
extern QueueHandle_t espnow_rx_queue;  // Project-specific queue

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

// Utility functions
uint16_t calculate_checksum(espnow_payload_t* data);
uint16_t calculate_crc16(const void* data, size_t len);
bool validate_crc16(const void* data, size_t len);
bool set_channel(uint8_t ch);

// ESP-NOW callbacks (ISR - only queue data)
void on_espnow_recv(const uint8_t *mac_addr, const uint8_t *data, int len);
void on_data_sent(const uint8_t *mac_addr, esp_now_send_status_t status);

// Health check for graceful retry
bool is_espnow_healthy();

// Initialization functions
void init_wifi();
void init_espnow(QueueHandle_t rx_queue);

#endif // ESPNOW_TRANSMITTER_H
