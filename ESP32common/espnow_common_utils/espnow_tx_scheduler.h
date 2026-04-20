#pragma once

#include <esp_err.h>
#include <freertos/FreeRTOS.h>

#include <cstddef>
#include <cstdint>

namespace EspnowTxScheduler {

struct InitOptions {
    uint8_t queue_depth = 24;
    uint8_t no_mem_retry_attempts = 6;
    uint32_t retry_base_delay_ms = 4;
    uint32_t inter_frame_delay_ms = 2;
    uint32_t queue_send_timeout_ms = 2;
    UBaseType_t task_priority = 1;
    uint32_t task_stack = 4096;
    BaseType_t task_core = 1;
    const char* task_name = "EspnowTx";
};

struct Stats {
    uint32_t enqueued = 0;
    uint32_t sent_ok = 0;
    uint32_t send_fail = 0;
    uint32_t enqueue_drop = 0;
    uint32_t no_mem_retry = 0;
};

bool init(const InitOptions& options = InitOptions{});
bool is_ready();

/**
 * Queue ESP-NOW frame for asynchronous send.
 *
 * Return semantics:
 * - ESP_OK: frame accepted by queue (or sent successfully via direct fallback)
 * - ESP_ERR_ESPNOW_NO_MEM: queue full or sender saturated
 * - ESP_ERR_INVALID_ARG: invalid MAC/data/len
 */
esp_err_t send(const uint8_t* mac, const void* data, size_t len, const char* context = nullptr);

bool read_stats(Stats& out_stats);
void reset_stats();

}  // namespace EspnowTxScheduler
