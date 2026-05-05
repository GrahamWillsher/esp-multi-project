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
    uint32_t send_fail_no_mem = 0;
    uint32_t enqueue_drop = 0;
    uint32_t no_mem_retry = 0;
    uint32_t cadence_defer = 0;
    uint32_t enqueue_drop_control = 0;
    uint32_t enqueue_drop_discovery = 0;
    uint32_t enqueue_drop_data = 0;
    uint32_t enqueue_drop_monitoring = 0;
    uint32_t send_fail_control = 0;
    uint32_t send_fail_discovery = 0;
    uint32_t send_fail_data = 0;
    uint32_t send_fail_monitoring = 0;
};

struct QueueDepths {
    uint32_t control = 0;
    uint32_t discovery = 0;
    uint32_t data = 0;
    uint32_t monitoring = 0;
};

bool init(const InitOptions& options = InitOptions{});
bool is_ready();
void set_control_only_mode(bool enabled, bool purge_non_control = false);
bool is_control_only_mode();
uint32_t purge_non_control_queues();
uint32_t purge_all_queues();   ///< Flush ALL priority queues (use before esp_now_deinit to prevent stale sends).

/**
 * Discovery ACK pacing token management.
 *
 * Only one in-flight discovery ACK per peer is allowed at a time.
 * - Acquire before enqueueing a discovery ACK.
 * - Release in ESP-NOW send callback or watchdog timeout path.
 */
bool try_acquire_ack_token(const uint8_t* peer_mac, uint32_t* held_ms = nullptr);
void release_ack_token(const uint8_t* peer_mac, const char* reason = nullptr);
void on_send_complete(const uint8_t* peer_mac, bool success);
uint32_t check_ack_token_watchdogs();
void clear_ack_tokens();

/**
 * Queue ESP-NOW frame for asynchronous send.
 *
 * Return semantics:
 * - ESP_OK: frame accepted by queue (or sent successfully via direct fallback)
 * - ESP_ERR_ESPNOW_NO_MEM: queue full or sender saturated
 * - ESP_ERR_INVALID_ARG: invalid MAC/data/len
 * - ESP_ERR_INVALID_STATE: non-control frame rejected while control-only mode is active
 */
esp_err_t send(const uint8_t* mac, const void* data, size_t len, const char* context = nullptr);

bool read_stats(Stats& out_stats);
bool read_queue_depths(QueueDepths& out_depths);
void reset_stats();
/**
 * @brief Returns the number of consecutive ACK sends that failed with
 *        ESP_ERR_ESPNOW_NO_MEM after all retry attempts.
 *
 * When this exceeds a project-defined threshold it indicates the ESP-NOW TX
 * buffer pool is permanently exhausted (callbacks never fired).  The caller
 * should reinitialise the ESP-NOW stack (esp_now_deinit + esp_now_init) and
 * then call reset_consecutive_no_mem_count() to reset the counter.
 */
uint32_t get_consecutive_no_mem_count();
void reset_consecutive_no_mem_count();
}  // namespace EspnowTxScheduler
