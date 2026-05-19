#pragma once

/**
 * @file tx_reconnect_manager.h
 * @brief Single-task ESP-NOW reconnect manager for the transmitter.
 *
 * ARCHITECTURE
 * ────────────
 * All reconnect state is owned by a single FreeRTOS task (Core 0).
 * A pure channel-hop worker task (Core 1) communicates ONLY via a
 * FreeRTOS queue — no shared variables, no cross-core cache issues.
 *
 * State machine:
 *   IDLE ──CONNECT──► SCANNING ──WORKER_FOUND──► IDLE (peer registered)
 *                        │
 *                    WORKER_MISS──► BACKOFF ──timer──► SCANNING
 *
 * External components post ReconnectEvent messages to the manager via
 * notify().  No other inter-task communication is needed.
 */

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <cstdint>
#include <cstring>

// ---------------------------------------------------------------------------
// Event type posted to the manager queue
// ---------------------------------------------------------------------------
enum class ReconnectEvent : uint8_t {
    CONNECT,        ///< EspNowConnectionManager entered CONNECTING state
    STOP,           ///< Connection established or shutdown requested
    WORKER_FOUND,   ///< Hop worker found receiver (channel + mac populated)
    WORKER_MISS,    ///< Hop worker completed full scan, receiver not found
    CONFIRM_ACK,    ///< connect_confirm_ack received from RX (session_id in mac[0:1])
};

// ---------------------------------------------------------------------------
// Message envelope
// ---------------------------------------------------------------------------
struct ReconnectMsg {
    ReconnectEvent event{ReconnectEvent::CONNECT};
    uint8_t        channel{0};
    uint8_t        rx_status{0};
    uint8_t        mac[6]{};
    uint16_t       session_id{0};  ///< Used by CONFIRM_ACK event
    uint16_t       session_boot_nonce{0};
};

// ---------------------------------------------------------------------------
// TxReconnectManager
// ---------------------------------------------------------------------------
class TxReconnectManager {
public:
    static TxReconnectManager& instance();

    /**
     * @brief Create queues, start manager task.
     *        Call from setup() AFTER EspNowConnectionManager::init().
     */
    void init();

    /**
     * @brief Post an event to the manager.
     *        Safe to call from any task or ISR.
     */
    void notify(ReconnectEvent event,
                uint8_t        channel = 0,
                const uint8_t* mac     = nullptr);

    /**
     * @brief Called from the msg_connect_confirm_ack route handler (ESP-NOW RX task).
     *        Thread-safe: posts a CONFIRM_ACK event to the manager queue.
     */
    void on_confirm_ack_received(const uint8_t* mac,
                                 uint16_t session_id,
                                 uint16_t session_boot_nonce,
                                 uint8_t rx_status);

    // Non-copyable singleton
    TxReconnectManager(const TxReconnectManager&) = delete;
    TxReconnectManager& operator=(const TxReconnectManager&) = delete;

private:
    TxReconnectManager() = default;

    // ── Task entry points ────────────────────────────────────────────────
    static void manager_task_entry(void* param);
    static void worker_task_entry(void* param);

    // ── Manager task body (Core 0 — single owner of all state below) ────
    void run_manager();
    void handle_connect();
    void handle_stop();
    void handle_worker_found(uint8_t channel, const uint8_t* mac);
    void handle_worker_miss();
    void handle_confirm_ack(uint16_t session_id,
                            uint16_t session_boot_nonce,
                            uint8_t rx_status,
                            const uint8_t* mac);
    void start_worker_scan();
    void send_connect_confirm(const uint8_t* mac);

    // ── Internal state — accessed ONLY from manager task ────────────────
    // CONFIRMING: connect_confirm sent to RX, waiting for connect_confirm_ack
    enum class ManagerState { IDLE, SCANNING, BACKOFF, CONFIRMING };
    ManagerState state_          {ManagerState::IDLE};
    uint32_t     scan_attempt_   {0};   ///< Number of full scan cycles attempted
    uint32_t     backoff_until_ms_{0};  ///< millis() target end of backoff

    // Confirmation handshake state (CONFIRMING only)
    uint16_t session_id_              {0};   ///< Incremented each time a confirm is sent
    uint16_t session_boot_nonce_      {0};   ///< Random per transmitter boot; pairs with session_id_
    uint32_t confirm_timeout_until_ms_{0};   ///< millis() deadline for confirm_ack
    uint8_t  confirm_retry_count_     {0};   ///< Retries before returning to SCANNING
    uint8_t  confirm_peer_mac_[6]     {};    ///< MAC of the peer being confirmed
    uint8_t  confirm_channel_         {0};   ///< Channel locked at start of CONFIRMING

    // ── FreeRTOS handles ─────────────────────────────────────────────────
    QueueHandle_t event_queue_          {nullptr}; ///< All → manager
    TaskHandle_t  manager_task_handle_  {nullptr};
};
