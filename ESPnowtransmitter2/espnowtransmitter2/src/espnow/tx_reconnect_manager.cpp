/**
 * @file tx_reconnect_manager.cpp
 * @brief Single-task ESP-NOW reconnect manager implementation.
 *
 * DESIGN PRINCIPLES
 * ─────────────────
 * 1. ALL reconnect state lives inside run_manager() — one task, one core.
 * 2. The hop worker on Core 1 has NO shared variables with the manager.
 *    It receives a heap-allocated WorkerContext, runs a scan, posts one
 *    result message, then self-deletes.  FreeRTOS queue operations provide
 *    the only inter-core synchronisation — guaranteed correct by design.
 * 3. Backoff counts COMPLETED scan cycles (WORKER_MISS), never state-
 *    machine timeout re-entries.  Repeated CONNECTING timeouts while a scan
 *    is already running are silently ignored.
 * 4. WiFi channel is set inside the worker before it reports WORKER_FOUND,
 *    so by the time the manager adds the peer the channel is already stable.
 */

#include "tx_reconnect_manager.h"
#include "discovery_task.h"
#include "tx_connection_handler.h"
#include "version_beacon_manager.h"
#include "data_cache.h"
#include "tx_state_machine.h"
#include "../config/task_config.h"
#include "../config/logging_config.h"
#include <esp32common/espnow/connection_manager.h>
#include <esp32common/config/timing_config.h>
#include <espnow_peer_manager.h>
#include <espnow_transmitter.h>   // g_lock_channel
#include <channel_manager.h>
#include <Arduino.h>
#include <esp_now.h>
#include <esp32common/espnow/common.h>

// ============================================================================
// Internal types (translation-unit scope)
// ============================================================================
namespace {

/** Context block passed to each worker task invocation.
 *  Heap-allocated by the manager; freed by the worker before self-delete.
 */
struct WorkerContext {
    QueueHandle_t result_queue;      ///< Worker posts its single result here
    uint8_t       start_channel_hint; ///< Where to start scanning
};

// ── Backoff schedule (indexed by scan_attempt_) ──────────────────────────
// attempt 0: first scan starts immediately (no wait)
// attempt 1: 3 s   (first failure — brief pause before retry)
// attempt 2+: 5 s  (settled wait matching DISCOVERY_RETRY_INTERVAL_MS)
constexpr uint32_t kBackoffTable[] = { 0, 3000, 5000 };
constexpr size_t   kBackoffCount   = sizeof(kBackoffTable) / sizeof(kBackoffTable[0]);

inline uint32_t backoff_for_attempt(uint32_t attempt) {
    const size_t idx = (attempt < kBackoffCount) ? attempt : (kBackoffCount - 1);
    return kBackoffTable[idx];
}

}  // namespace

// ============================================================================
// Singleton accessor
// ============================================================================
TxReconnectManager& TxReconnectManager::instance() {
    static TxReconnectManager inst;
    return inst;
}

// ============================================================================
// Public init
// ============================================================================
void TxReconnectManager::init() {
    // Queue depth 8: enough to buffer CONNECT + STOP storms during rapid cycling
    event_queue_ = xQueueCreate(8, sizeof(ReconnectMsg));
    configASSERT(event_queue_ != nullptr);

    xTaskCreatePinnedToCore(
        manager_task_entry,
        "reconnect_mgr",
        4096,
        this,
        task_config::PRIORITY_NORMAL,
        &manager_task_handle_,
        0  // Core 0 — shares core with connection_event_processor
    );
    configASSERT(manager_task_handle_ != nullptr);

    LOG_INFO("RECONNECT", "TxReconnectManager initialised (Core 0, queue depth 8)");
}

// ============================================================================
// Public notify — safe from any task or ISR
// ============================================================================
void TxReconnectManager::notify(ReconnectEvent event,
                                uint8_t        channel,
                                const uint8_t* mac) {
    if (!event_queue_) return;

    ReconnectMsg msg{};
    msg.event   = event;
    msg.channel = channel;
    if (mac) memcpy(msg.mac, mac, 6);

    if (xPortInIsrContext()) {
        BaseType_t woken = pdFALSE;
        xQueueSendFromISR(event_queue_, &msg, &woken);
        if (woken) portYIELD_FROM_ISR();
    } else {
        // Drop silently if full rather than block — callers are event-driven
        xQueueSend(event_queue_, &msg, pdMS_TO_TICKS(50));
    }
}

// ============================================================================
// Manager task
// ============================================================================
void TxReconnectManager::manager_task_entry(void* param) {
    static_cast<TxReconnectManager*>(param)->run_manager();
}

void TxReconnectManager::run_manager() {
    LOG_INFO("RECONNECT", "Manager task running on Core %d", xPortGetCoreID());

    for (;;) {
        // In BACKOFF we poll every 50 ms to check the timer; otherwise block.
        const TickType_t wait =
            (state_ == ManagerState::BACKOFF || state_ == ManagerState::CONFIRMING)
                ? pdMS_TO_TICKS(50) : portMAX_DELAY;

        ReconnectMsg msg{};
        if (xQueueReceive(event_queue_, &msg, wait) == pdTRUE) {
            switch (msg.event) {
                case ReconnectEvent::CONNECT:
                    handle_connect();
                    break;
                case ReconnectEvent::STOP:
                    handle_stop();
                    break;
                case ReconnectEvent::WORKER_FOUND:
                    handle_worker_found(msg.channel, msg.mac);
                    break;
                case ReconnectEvent::WORKER_MISS:
                    handle_worker_miss();
                    break;
                case ReconnectEvent::CONFIRM_ACK:
                    handle_confirm_ack(msg.session_id);
                    break;
            }
        }

        // Backoff expiry check (polled every 50 ms while in BACKOFF state)
        if (state_ == ManagerState::BACKOFF) {
            if ((int32_t)(millis() - backoff_until_ms_) >= 0) {
                LOG_INFO("RECONNECT", "Backoff expired — starting scan (attempt %lu)",
                         static_cast<unsigned long>(scan_attempt_ + 1));
                start_worker_scan();
            }
        }

        // Confirm timeout check (polled every 50 ms while in CONFIRMING state)
        if (state_ == ManagerState::CONFIRMING) {
            if ((int32_t)(millis() - confirm_timeout_until_ms_) >= 0) {
                constexpr uint8_t kMaxConfirmRetries = 2;
                if (confirm_retry_count_ >= kMaxConfirmRetries) {
                    LOG_WARN("RECONNECT",
                             "[STATE_CHANGE] CONFIRMING -> SCANNING  "
                             "reason=confirm_timeout  retries=%u  session=%u",
                             static_cast<unsigned>(confirm_retry_count_),
                             static_cast<unsigned>(session_id_));
                    state_ = ManagerState::SCANNING;
                    start_worker_scan();
                } else {
                    ++confirm_retry_count_;
                    LOG_INFO("RECONNECT",
                             "confirm_confirm retry %u/%u (session=%u)",
                             static_cast<unsigned>(confirm_retry_count_),
                             static_cast<unsigned>(kMaxConfirmRetries),
                             static_cast<unsigned>(session_id_));
                    send_connect_confirm(confirm_peer_mac_);
                    confirm_timeout_until_ms_ = millis() + 2000U;
                }
            }
        }
    }
}

// ============================================================================
// Event handlers (all called from manager task — single-threaded)
// ============================================================================
void TxReconnectManager::handle_connect() {
    switch (state_) {
        case ManagerState::SCANNING:
            // Worker is already running — the CONNECTING re-entry was caused
            // by the 35 s timeout cycling CONNECTING→IDLE→CONNECTING.
            // The scan is doing useful work; ignore and let it complete.
            LOG_DEBUG("RECONNECT",
                      "CONNECT received while already scanning — ignoring (backoff not counted)");
            break;

        case ManagerState::BACKOFF:
            // A new explicit CONNECT means we should try immediately.
            LOG_INFO("RECONNECT", "CONNECT received during backoff — scanning now");
            backoff_until_ms_ = millis();  // expire timer immediately
            // Backoff poll loop will pick this up within 50 ms
            break;

        case ManagerState::IDLE:
        default:
            scan_attempt_ = 0;
            start_worker_scan();
            break;
    }
}

void TxReconnectManager::handle_stop() {
    if (state_ == ManagerState::IDLE) return;

    LOG_INFO("RECONNECT", "STOP received — returning to IDLE (was state %d)",
             static_cast<int>(state_));

    // Any in-flight worker will self-delete and post WORKER_FOUND/MISS.
    // Those will arrive while we are in IDLE and be silently ignored
    // (handle_worker_* checks state_ == SCANNING before acting).
    state_          = ManagerState::IDLE;
    scan_attempt_   = 0;
    backoff_until_ms_ = 0;
}

void TxReconnectManager::handle_worker_found(uint8_t channel, const uint8_t* mac) {
    if (state_ != ManagerState::SCANNING) {
        // Arrived after a STOP — discard.
        LOG_DEBUG("RECONNECT",
                  "WORKER_FOUND ignored (state is not SCANNING, likely post-STOP stale result)");
        return;
    }

    LOG_INFO("RECONNECT",
             "✓ Receiver found on ch=%d after %lu scan attempt(s)",
             channel, static_cast<unsigned long>(scan_attempt_ + 1));

    // ── 1. Lock global channel (read by many components) ────────────────
    g_lock_channel = channel;
    ChannelManager::instance().lock_channel(channel, "RECONNECT_MGR");

    // ── 2. Register peer with the discovered channel so we can send confirm ─
    if (!EspnowPeerManager::is_peer_registered(mac)) {
        EspnowPeerManager::add_peer(const_cast<uint8_t*>(mac), channel);
        LOG_INFO("RECONNECT", "  ✓ Peer registered");
    } else {
        LOG_DEBUG("RECONNECT", "  - Peer already registered");
    }

    // ── 3. Update TX connection handler caches (no PEER_REGISTERED yet) ─
    //  on_ack_received stores receiver_mac_ + receiver_channel_.
    //  PEER_REGISTERED is posted only after connect_confirm_ack is received.
    TransmitterConnectionHandler::instance().on_ack_received(mac, channel);

    // ── 4. Send connect_confirm and enter CONFIRMING ─────────────────────
    //  Store confirmation context for timeout retries and session matching.
    ++session_id_;
    memcpy(confirm_peer_mac_, mac, 6);
    confirm_channel_           = channel;
    confirm_retry_count_       = 0;
    confirm_timeout_until_ms_  = millis() + 2000U;

    send_connect_confirm(confirm_peer_mac_);
    state_        = ManagerState::CONFIRMING;
    scan_attempt_ = 0;  // reset for next scan cycle if needed

    LOG_INFO("RECONNECT",
             "[STATE_CHANGE] SCANNING -> CONFIRMING  "
             "reason=worker_found  ch=%d  session=%u",
             channel, static_cast<unsigned>(session_id_));
}

// ============================================================================
// handle_confirm_ack — called from run_manager() on CONFIRM_ACK event
// ============================================================================
void TxReconnectManager::handle_confirm_ack(uint16_t session_id) {
    if (state_ != ManagerState::CONFIRMING) {
        LOG_DEBUG("RECONNECT",
                  "CONFIRM_ACK ignored (not in CONFIRMING state, likely stale)");
        return;
    }

    if (session_id != session_id_) {
        LOG_WARN("RECONNECT",
                 "CONFIRM_ACK session mismatch (got %u, expected %u) — discarding",
                 static_cast<unsigned>(session_id),
                 static_cast<unsigned>(session_id_));
        return;
    }

    LOG_INFO("RECONNECT",
             "[STATE_CHANGE] CONFIRMING -> CONNECTED  "
             "reason=confirm_ack_received  session=%u  ch=%d",
             static_cast<unsigned>(session_id_),
             static_cast<int>(confirm_channel_));

    // Re-register peer with channel=0 so all future sends use the current
    // WiFi channel rather than a hard-coded value (correct now channel lock
    // is in effect on both sides).
    if (EspnowPeerManager::is_peer_registered(confirm_peer_mac_)) {
        EspnowPeerManager::remove_peer(confirm_peer_mac_);
    }
    EspnowPeerManager::add_peer(confirm_peer_mac_, /*channel=*/0);
    LOG_INFO("RECONNECT", "  ✓ Peer re-registered with channel=0 (follows WiFi AP channel)");

    // Post PEER_REGISTERED — drives CONNECTING -> CONNECTED in EspNowConnectionManager
    TransmitterConnectionHandler::instance().on_peer_registered(confirm_peer_mac_);

    // Flush any cached data accumulated during disconnection
    if (!DataCache::instance().is_empty()) {
        const size_t flushed = DataCache::instance().flush();
        LOG_INFO("RECONNECT", "  ✓ %d cached messages flushed", static_cast<int>(flushed));
    }

    // Send initial version beacon
    if (!VersionBeaconManager::instance().send_version_beacon(true)) {
        LOG_WARN("RECONNECT", "  Version beacon failed — will retry via periodic beacons");
    }

    state_ = ManagerState::IDLE;
}

// ============================================================================
// on_confirm_ack_received — called from route handler (ESP-NOW task, thread-safe)
// ============================================================================
void TxReconnectManager::on_confirm_ack_received(uint16_t session_id) {
    if (!event_queue_) return;
    ReconnectMsg msg{};
    msg.event      = ReconnectEvent::CONFIRM_ACK;
    msg.session_id = session_id;
    // Drop silently if queue is full — a retry will follow within 2 s
    xQueueSend(event_queue_, &msg, pdMS_TO_TICKS(50));
}

// ============================================================================
// send_connect_confirm — internal helper, called from manager task
// ============================================================================
void TxReconnectManager::send_connect_confirm(const uint8_t* mac) {
    espnow_connect_confirm_t pkt{};
    pkt.type             = msg_connect_confirm;
    pkt.protocol_version = ESPNOW_PROTOCOL_VERSION;
    pkt.session_id       = session_id_;
    pkt.channel          = confirm_channel_;

    const esp_err_t err = esp_now_send(
        mac,
        reinterpret_cast<const uint8_t*>(&pkt),
        sizeof(pkt));

    if (err == ESP_OK) {
        LOG_INFO("RECONNECT",
                 "connect_confirm sent (session=%u, ch=%d)",
                 static_cast<unsigned>(session_id_),
                 static_cast<int>(confirm_channel_));
    } else {
        LOG_WARN("RECONNECT",
                 "connect_confirm send failed: %s (session=%u)",
                 esp_err_to_name(err),
                 static_cast<unsigned>(session_id_));
    }
}

void TxReconnectManager::handle_worker_miss() {
    if (state_ != ManagerState::SCANNING) {
        LOG_DEBUG("RECONNECT", "WORKER_MISS ignored (not in SCANNING state)");
        return;
    }

    scan_attempt_++;
    const uint32_t bkoff = backoff_for_attempt(scan_attempt_);

    LOG_WARN("RECONNECT",
             "✗ Scan attempt %lu complete — receiver not found (backoff %lu ms)",
             static_cast<unsigned long>(scan_attempt_),
             static_cast<unsigned long>(bkoff));

    if (bkoff == 0) {
        // No wait — start next scan immediately
        start_worker_scan();
    } else {
        state_           = ManagerState::BACKOFF;
        backoff_until_ms_ = millis() + bkoff;
    }
}

// ============================================================================
// Worker lifecycle
// ============================================================================
void TxReconnectManager::start_worker_scan() {
    state_ = ManagerState::SCANNING;

    // Heap-allocate context — worker frees it before self-deleting.
    auto* ctx = new WorkerContext{};
    ctx->result_queue       = event_queue_;
    ctx->start_channel_hint = ChannelManager::instance().get_channel();

    LOG_INFO("RECONNECT",
             "Starting hop worker: hint ch=%d, scan #%lu",
             ctx->start_channel_hint,
             static_cast<unsigned long>(scan_attempt_ + 1));

    BaseType_t created = xTaskCreatePinnedToCore(
        worker_task_entry,
        "hop_worker",
        4096,
        ctx,
        task_config::PRIORITY_LOW,  // Does not block control/CAN traffic
        nullptr,                    // No handle needed — worker self-deletes
        1                           // Core 1 — isolated from CAN/battery (Core 0)
    );

    if (created != pdPASS) {
        LOG_ERROR("RECONNECT", "Failed to create hop worker task!");
        delete ctx;
        // Fall back to immediate MISS so backoff/retry continues
        handle_worker_miss();
    }
}

// ============================================================================
// Worker task — runs on Core 1, communicates ONLY via queue
// ============================================================================
void TxReconnectManager::worker_task_entry(void* param) {
    auto* ctx       = static_cast<WorkerContext*>(param);
    QueueHandle_t q = ctx->result_queue;
    uint8_t       hint = ctx->start_channel_hint;
    delete ctx;  // Free immediately — no further use

    LOG_INFO("HOP_WORKER",
             "Channel scan starting (hint ch=%d) on Core %d",
             hint, xPortGetCoreID());

    uint8_t found_channel = 0;
    uint8_t found_mac[6]  = {};

    const bool found =
        DiscoveryTask::instance().run_hop_scan(hint, &found_channel, found_mac);

    ReconnectMsg result{};
    if (found) {
        result.event   = ReconnectEvent::WORKER_FOUND;
        result.channel = found_channel;
        memcpy(result.mac, found_mac, 6);
        LOG_INFO("HOP_WORKER",
                 "Scan complete: FOUND on ch=%d MAC=%02X:%02X:%02X:%02X:%02X:%02X",
                 found_channel,
                 found_mac[0], found_mac[1], found_mac[2],
                 found_mac[3], found_mac[4], found_mac[5]);
    } else {
        result.event = ReconnectEvent::WORKER_MISS;
        LOG_INFO("HOP_WORKER", "Scan complete: receiver NOT FOUND");
    }

    // Post result (100 ms timeout; queue should never be full)
    xQueueSend(q, &result, pdMS_TO_TICKS(100));

    vTaskDelete(nullptr);  // Clean self-delete — no shared state to worry about
}
