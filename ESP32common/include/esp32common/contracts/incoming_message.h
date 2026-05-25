#pragma once

#include <cstdint>

// Transport-neutral incoming control/config message envelope.
//
// Keeps compatibility with the legacy espnow_queue_msg_t memory layout while
// decoupling handler signatures from ESP-NOW-specific headers.
typedef struct {
    uint8_t data[250];
    uint8_t mac[6];
    int len;
    uint32_t timestamp;
} incoming_msg_t;
