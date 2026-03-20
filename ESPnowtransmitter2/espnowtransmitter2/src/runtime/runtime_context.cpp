#include "runtime/runtime_context.h"

// ISR/library compatibility exports (required by esp32common/espnow_transmitter)
QueueHandle_t espnow_message_queue = nullptr;
QueueHandle_t espnow_discovery_queue = nullptr;
QueueHandle_t espnow_rx_queue = nullptr;

RuntimeContext& RuntimeContext::instance() {
    static RuntimeContext instance;
    return instance;
}

void RuntimeContext::bind_espnow_queues(QueueHandle_t message,
                                        QueueHandle_t discovery,
                                        QueueHandle_t rx) {
    espnow_message_queue_ = message;
    espnow_discovery_queue_ = discovery;
    espnow_rx_queue_ = rx;

    // Keep legacy/global exports synchronized for ISR callbacks in shared lib.
    ::espnow_message_queue = message;
    ::espnow_discovery_queue = discovery;
    ::espnow_rx_queue = rx;
}

void RuntimeContext::set_tx_soc(uint8_t soc) {
    tx_data.soc = soc;
}

espnow_payload_t& RuntimeContext::tx_payload() {
    return tx_data;
}
