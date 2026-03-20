#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <espnow_transmitter.h>

// ISR/library compatibility exports required by esp32common/espnow_transmitter.
// The library expects these symbols to exist as global queue handles.
extern QueueHandle_t espnow_message_queue;
extern QueueHandle_t espnow_discovery_queue;
extern QueueHandle_t espnow_rx_queue;

/**
 * @brief Process-wide runtime context for cross-module shared runtime handles.
 *
 * This starts by owning ESP-NOW queue handles and synchronizing them with the
 * ISR-required global exports above.
 */
class RuntimeContext {
public:
    static RuntimeContext& instance();

    void bind_espnow_queues(QueueHandle_t message,
                            QueueHandle_t discovery,
                            QueueHandle_t rx);

    QueueHandle_t espnow_message_queue() const { return espnow_message_queue_; }
    QueueHandle_t espnow_discovery_queue() const { return espnow_discovery_queue_; }
    QueueHandle_t espnow_rx_queue() const { return espnow_rx_queue_; }

    // TX payload helpers (wrap shared-library global access behind context API)
    void set_tx_soc(uint8_t soc);
    espnow_payload_t& tx_payload();

private:
    RuntimeContext() = default;
    ~RuntimeContext() = default;

    RuntimeContext(const RuntimeContext&) = delete;
    RuntimeContext& operator=(const RuntimeContext&) = delete;

    QueueHandle_t espnow_message_queue_{nullptr};
    QueueHandle_t espnow_discovery_queue_{nullptr};
    QueueHandle_t espnow_rx_queue_{nullptr};
};
