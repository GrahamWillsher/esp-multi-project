#include "espnow_tx_scheduler.h"

#include <esp32common/espnow/common.h>
#include <esp_now.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <cstring>

#include <log_routed.h>

namespace EspnowTxScheduler {
namespace {

enum class MessagePriority : uint8_t {
    CONTROL = 0,
    DISCOVERY = 1,
    DATA = 2,
    MONITORING = 3,
    COUNT = 4,
};

struct SendPolicy {
    uint32_t min_gap_ms;
    uint8_t retry_attempts;
};

struct TxItem {
    uint8_t mac[6];
    uint8_t len;
    uint8_t type;
    uint8_t payload[ESP_NOW_MAX_DATA_LEN];
};

struct AckTokenSlot {
    bool active = false;
    uint8_t mac[6] = {0};
    uint32_t acquired_ms = 0;
};

static constexpr uint32_t ACK_TOKEN_WATCHDOG_MS = 500;
static constexpr size_t ACK_TOKEN_SLOT_COUNT = 8;

QueueHandle_t g_queues[static_cast<size_t>(MessagePriority::COUNT)] = {
    nullptr, nullptr, nullptr, nullptr
};
TaskHandle_t g_task = nullptr;
InitOptions g_options{};
Stats g_stats{};
portMUX_TYPE g_stats_mux = portMUX_INITIALIZER_UNLOCKED;
uint32_t g_last_send_ms[256] = {0};
bool g_control_only_mode = false;
AckTokenSlot g_ack_tokens[ACK_TOKEN_SLOT_COUNT] = {};
uint32_t g_consecutive_no_mem_count = 0;   ///< Count of ACK sends that exhausted all retries with NO_MEM

uint32_t now_ms() {
    return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

bool valid_unicast_mac(const uint8_t* mac) {
    if (mac == nullptr) {
        return false;
    }

    bool all_zero = true;
    bool all_ff = true;
    for (size_t i = 0; i < 6; ++i) {
        if (mac[i] != 0x00) {
            all_zero = false;
        }
        if (mac[i] != 0xFF) {
            all_ff = false;
        }
    }

    return !all_zero && !all_ff;
}

int find_token_slot_locked(const uint8_t* mac) {
    for (size_t i = 0; i < ACK_TOKEN_SLOT_COUNT; ++i) {
        if (!g_ack_tokens[i].active) {
            continue;
        }
        if (std::memcmp(g_ack_tokens[i].mac, mac, 6) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int find_free_token_slot_locked(uint32_t now) {
    for (size_t i = 0; i < ACK_TOKEN_SLOT_COUNT; ++i) {
        if (!g_ack_tokens[i].active) {
            return static_cast<int>(i);
        }
    }

    for (size_t i = 0; i < ACK_TOKEN_SLOT_COUNT; ++i) {
        if (g_ack_tokens[i].active && (now - g_ack_tokens[i].acquired_ms) >= ACK_TOKEN_WATCHDOG_MS) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

MessagePriority classify_priority(uint8_t msg_type) {
    switch (msg_type) {
        case msg_ack:
        case msg_heartbeat:
        case msg_heartbeat_ack:
            return MessagePriority::CONTROL;

        case msg_probe:
            return MessagePriority::DISCOVERY;

        case msg_version_beacon:
        case msg_temperature_report:
            return MessagePriority::MONITORING;

        default:
            return MessagePriority::DATA;
    }
}

SendPolicy policy_for(uint8_t msg_type) {
    switch (msg_type) {
        case msg_heartbeat:            return {800, 4};
        case msg_heartbeat_ack:        return {80, 8};
        // Discovery ACK is reconnect-critical. Under transient WiFi TX buffer
        // starvation (ESP_ERR_ESPNOW_NO_MEM), a longer bounded retry window is
        // required to survive the pressure interval within the same dwell.
        case msg_ack:                  return {150, 12};
        case msg_probe:                return {250, 3};
        case msg_request_data:         return {1000, 3};
        case msg_battery_status:       return {1000, 1};
        case msg_charger_status:
        case msg_inverter_status:
        case msg_system_status:        return {500, 1};
        case msg_config_section_request:return {3000, 3};
        case msg_version_beacon:       return {60000, 1};
        case msg_temperature_report:   return {1000, 1};
        default:                       return {100, 0};
    }
}

void bump_enqueue_drop_by_priority(MessagePriority prio) {
    switch (prio) {
        case MessagePriority::CONTROL:
            g_stats.enqueue_drop_control++;
            break;
        case MessagePriority::DISCOVERY:
            g_stats.enqueue_drop_discovery++;
            break;
        case MessagePriority::DATA:
            g_stats.enqueue_drop_data++;
            break;
        case MessagePriority::MONITORING:
            g_stats.enqueue_drop_monitoring++;
            break;
        default:
            break;
    }
}

void bump_send_fail_by_priority(MessagePriority prio, esp_err_t result) {
    if (result == ESP_ERR_ESPNOW_NO_MEM) {
        g_stats.send_fail_no_mem++;
    }

    switch (prio) {
        case MessagePriority::CONTROL:
            g_stats.send_fail_control++;
            break;
        case MessagePriority::DISCOVERY:
            g_stats.send_fail_discovery++;
            break;
        case MessagePriority::DATA:
            g_stats.send_fail_data++;
            break;
        case MessagePriority::MONITORING:
            g_stats.send_fail_monitoring++;
            break;
        default:
            break;
    }
}

bool dequeue_next_priority_item(TxItem& out_item, MessagePriority& out_prio) {
    for (size_t p = 0; p < static_cast<size_t>(MessagePriority::COUNT); ++p) {
        if (g_queues[p] == nullptr) {
            continue;
        }
        if (xQueueReceive(g_queues[p], &out_item, 0) == pdTRUE) {
            out_prio = static_cast<MessagePriority>(p);
            return true;
        }
    }
    return false;
}

bool requeue_deferred(MessagePriority prio, const TxItem& item) {
    QueueHandle_t q = g_queues[static_cast<size_t>(prio)];
    if (q == nullptr) {
        return false;
    }

    // Keep control-plane traffic (heartbeat/ACK) at the head when a send is
    // deferred by cadence, so data bursts cannot push it to the tail.
    if (prio == MessagePriority::CONTROL) {
        return xQueueSendToFront(q, &item, 0) == pdTRUE;
    }

    return xQueueSend(q, &item, 0) == pdTRUE;
}

void create_priority_queues(uint8_t total_depth) {
    const uint8_t depth = (total_depth == 0) ? 1 : total_depth;
    const uint8_t d0 = (depth >= 4) ? static_cast<uint8_t>((depth * 30U) / 100U) : 1; // control
    const uint8_t d1 = (depth >= 4) ? static_cast<uint8_t>((depth * 20U) / 100U) : 1; // discovery
    const uint8_t d2 = (depth >= 4) ? static_cast<uint8_t>((depth * 40U) / 100U) : 1; // data
    uint8_t d3 = (depth >= 4) ? static_cast<uint8_t>(depth - d0 - d1 - d2) : 1;       // monitoring

    uint8_t q0 = (d0 == 0) ? 1 : d0;
    uint8_t q1 = (d1 == 0) ? 1 : d1;
    uint8_t q2 = (d2 == 0) ? 1 : d2;
    uint8_t q3 = (d3 == 0) ? 1 : d3;

    g_queues[0] = xQueueCreate(q0, sizeof(TxItem));
    g_queues[1] = xQueueCreate(q1, sizeof(TxItem));
    g_queues[2] = xQueueCreate(q2, sizeof(TxItem));
    g_queues[3] = xQueueCreate(q3, sizeof(TxItem));

    LOG_INFO("ESPNOW_TX", "Priority queues: P0=%u P1=%u P2=%u P3=%u (total=%u)",
             static_cast<unsigned>(q0),
             static_cast<unsigned>(q1),
             static_cast<unsigned>(q2),
             static_cast<unsigned>(q3),
             static_cast<unsigned>(depth));
}

void destroy_priority_queues() {
    for (size_t i = 0; i < static_cast<size_t>(MessagePriority::COUNT); ++i) {
        if (g_queues[i] != nullptr) {
            vQueueDelete(g_queues[i]);
            g_queues[i] = nullptr;
        }
    }
}

uint32_t purge_queue(QueueHandle_t queue) {
    if (queue == nullptr) {
        return 0;
    }

    uint32_t purged = 0;
    TxItem dropped{};
    while (xQueueReceive(queue, &dropped, 0) == pdTRUE) {
        ++purged;
    }
    return purged;
}

esp_err_t send_immediate_with_retry(const uint8_t* mac, const uint8_t* data, size_t len, uint8_t retry_attempts) {
    esp_err_t result = ESP_FAIL;
    const uint8_t attempts = (retry_attempts == 0) ? g_options.no_mem_retry_attempts : retry_attempts;
    for (uint8_t attempt = 0; attempt < attempts; ++attempt) {
        result = esp_now_send(mac, data, len);
        if (result == ESP_OK) {
            return ESP_OK;
        }

        if (result != ESP_ERR_ESPNOW_NO_MEM || (attempt + 1U) >= attempts) {
            break;
        }

        portENTER_CRITICAL(&g_stats_mux);
        g_stats.no_mem_retry++;
        portEXIT_CRITICAL(&g_stats_mux);

        vTaskDelay(pdMS_TO_TICKS(g_options.retry_base_delay_ms * (attempt + 1U)));
    }

    return result;
}

void task_tx_worker(void* /*parameter*/) {
    TxItem item{};
    MessagePriority prio = MessagePriority::DATA;

    for (;;) {
        if (!dequeue_next_priority_item(item, prio)) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        const uint32_t now = now_ms();
        const SendPolicy policy = policy_for(item.type);
        const uint32_t last = g_last_send_ms[item.type];
        const uint32_t elapsed = now - last;

        if (policy.min_gap_ms > 0U && elapsed < policy.min_gap_ms) {
            const bool requeued = requeue_deferred(prio, item);
            portENTER_CRITICAL(&g_stats_mux);
            g_stats.cadence_defer++;
            if (!requeued) {
                g_stats.enqueue_drop++;
                bump_enqueue_drop_by_priority(prio);
            }
            portEXIT_CRITICAL(&g_stats_mux);

            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        const esp_err_t result = send_immediate_with_retry(item.mac, item.payload, item.len, policy.retry_attempts);

        portENTER_CRITICAL(&g_stats_mux);
        if (result == ESP_OK) {
            g_stats.sent_ok++;
            g_last_send_ms[item.type] = now;
            if (item.type == msg_ack) {
                g_consecutive_no_mem_count = 0;  // successful ACK send: clear the stuck-buffer counter
            }
        } else {
            g_stats.send_fail++;
            bump_send_fail_by_priority(prio, result);
            // Back off this message type after NO_MEM so we don't hammer the
            // WiFi driver allocation path with immediate retries from backlog.
            if (result == ESP_ERR_ESPNOW_NO_MEM) {
                g_last_send_ms[item.type] = now;
                if (item.type == msg_ack) {
                    g_consecutive_no_mem_count++;  // track persistent buffer exhaustion
                }
            }
        }
        portEXIT_CRITICAL(&g_stats_mux);

        if (result != ESP_OK) {
            LOG_WARN("ESPNOW_TX", "Send failed (type=%u len=%u): %s",
                     static_cast<unsigned>(item.type),
                     static_cast<unsigned>(item.len),
                     esp_err_to_name(result));

            // Discovery ACK sends can fail synchronously (e.g. NO_MEM) and then
            // never receive a send callback. Release the per-peer ACK token
            // immediately on send failure so reconnect can retry on the next
            // probe instead of waiting for watchdog expiry.
            if (item.type == msg_ack) {
                release_ack_token(item.mac, "send_failed");
            }
        }

        if (g_options.inter_frame_delay_ms > 0U) {
            vTaskDelay(pdMS_TO_TICKS(g_options.inter_frame_delay_ms));
        }
    }
}

}  // namespace

bool try_acquire_ack_token(const uint8_t* peer_mac, uint32_t* held_ms) {
    if (!valid_unicast_mac(peer_mac)) {
        return false;
    }

    const uint32_t now = now_ms();
    if (held_ms != nullptr) {
        *held_ms = 0;
    }

    bool acquired = false;
    bool reused_stale_slot = false;

    portENTER_CRITICAL(&g_stats_mux);
    const int existing_slot = find_token_slot_locked(peer_mac);
    if (existing_slot >= 0) {
        if (held_ms != nullptr) {
            *held_ms = now - g_ack_tokens[existing_slot].acquired_ms;
        }
    } else {
        const int slot = find_free_token_slot_locked(now);
        if (slot >= 0) {
            reused_stale_slot = g_ack_tokens[slot].active;
            g_ack_tokens[slot].active = true;
            std::memcpy(g_ack_tokens[slot].mac, peer_mac, sizeof(g_ack_tokens[slot].mac));
            g_ack_tokens[slot].acquired_ms = now;
            acquired = true;
        }
    }
    portEXIT_CRITICAL(&g_stats_mux);

    if (reused_stale_slot) {
        LOG_WARN("ESPNOW_TX", "ACK token watchdog reclaimed stale slot before acquire");
    }

    return acquired;
}

void release_ack_token(const uint8_t* peer_mac, const char* reason) {
    if (!valid_unicast_mac(peer_mac)) {
        return;
    }

    const uint32_t now = now_ms();
    bool released = false;
    uint32_t held_ms = 0;

    portENTER_CRITICAL(&g_stats_mux);
    const int slot = find_token_slot_locked(peer_mac);
    if (slot >= 0) {
        held_ms = now - g_ack_tokens[slot].acquired_ms;
        g_ack_tokens[slot] = {};
        released = true;
    }
    portEXIT_CRITICAL(&g_stats_mux);

    if (released) {
        LOG_DEBUG("ESPNOW_TX", "ACK token released (%s, held=%lu ms)",
                  reason ? reason : "unspecified",
                  static_cast<unsigned long>(held_ms));
    }
}

void on_send_complete(const uint8_t* peer_mac, bool success) {
    release_ack_token(peer_mac, success ? "send_cb_success" : "send_cb_fail");
}

uint32_t check_ack_token_watchdogs() {
    struct ExpiredToken {
        uint8_t mac[6];
        uint32_t held_ms = 0;
    } expired[ACK_TOKEN_SLOT_COUNT] = {};
    size_t expired_count = 0;

    const uint32_t now = now_ms();
    portENTER_CRITICAL(&g_stats_mux);
    for (size_t i = 0; i < ACK_TOKEN_SLOT_COUNT; ++i) {
        if (!g_ack_tokens[i].active) {
            continue;
        }

        const uint32_t held = now - g_ack_tokens[i].acquired_ms;
        if (held < ACK_TOKEN_WATCHDOG_MS) {
            continue;
        }

        if (expired_count < ACK_TOKEN_SLOT_COUNT) {
            std::memcpy(expired[expired_count].mac, g_ack_tokens[i].mac, 6);
            expired[expired_count].held_ms = held;
            ++expired_count;
        }
        g_ack_tokens[i] = {};
    }
    portEXIT_CRITICAL(&g_stats_mux);

    for (size_t i = 0; i < expired_count; ++i) {
        LOG_WARN("ESPNOW_TX", "ACK token watchdog release for %02X:%02X:%02X:%02X:%02X:%02X (held=%lu ms)",
                 expired[i].mac[0], expired[i].mac[1], expired[i].mac[2],
                 expired[i].mac[3], expired[i].mac[4], expired[i].mac[5],
                 static_cast<unsigned long>(expired[i].held_ms));
    }

    return static_cast<uint32_t>(expired_count);
}

void clear_ack_tokens() {
    portENTER_CRITICAL(&g_stats_mux);
    for (size_t i = 0; i < ACK_TOKEN_SLOT_COUNT; ++i) {
        g_ack_tokens[i] = {};
    }
    portEXIT_CRITICAL(&g_stats_mux);
}

bool init(const InitOptions& options) {
    if (g_task != nullptr && g_queues[0] != nullptr) {
        return true;
    }

    g_options = options;
    if (g_options.queue_depth == 0) {
        g_options.queue_depth = 1;
    }
    if (g_options.no_mem_retry_attempts == 0) {
        g_options.no_mem_retry_attempts = 1;
    }

    if (g_queues[0] == nullptr) {
        create_priority_queues(g_options.queue_depth);
    }

    if (g_queues[0] == nullptr || g_queues[1] == nullptr || g_queues[2] == nullptr || g_queues[3] == nullptr) {
        LOG_ERROR("ESPNOW_TX", "Failed to create priority queues");
        destroy_priority_queues();
        return false;
    }

    if (g_task == nullptr) {
        const BaseType_t rc = xTaskCreatePinnedToCore(
            task_tx_worker,
            g_options.task_name,
            g_options.task_stack,
            nullptr,
            g_options.task_priority,
            &g_task,
            g_options.task_core);

        if (rc != pdPASS) {
            LOG_ERROR("ESPNOW_TX", "Failed to create task '%s'", g_options.task_name);
            return false;
        }
    }

    LOG_INFO("ESPNOW_TX", "Initialized (depth=%u retries=%u baseDelay=%lu interFrame=%lu)",
             static_cast<unsigned>(g_options.queue_depth),
             static_cast<unsigned>(g_options.no_mem_retry_attempts),
             static_cast<unsigned long>(g_options.retry_base_delay_ms),
             static_cast<unsigned long>(g_options.inter_frame_delay_ms));
    return true;
}

bool is_ready() {
    return g_queues[0] != nullptr && g_task != nullptr;
}

void set_control_only_mode(bool enabled, bool purge_non_control) {
    bool changed = false;
    portENTER_CRITICAL(&g_stats_mux);
    changed = (g_control_only_mode != enabled);
    g_control_only_mode = enabled;
    portEXIT_CRITICAL(&g_stats_mux);

    if (enabled && purge_non_control) {
        const uint32_t purged = purge_non_control_queues();
        LOG_INFO("ESPNOW_TX", "Control-only mode enabled%s (purged=%lu)",
                 changed ? "" : " (already active)",
                 static_cast<unsigned long>(purged));
    } else if (changed) {
        LOG_INFO("ESPNOW_TX", "Control-only mode %s",
                 enabled ? "enabled" : "disabled");
    }
}

bool is_control_only_mode() {
    bool enabled = false;
    portENTER_CRITICAL(&g_stats_mux);
    enabled = g_control_only_mode;
    portEXIT_CRITICAL(&g_stats_mux);
    return enabled;
}

uint32_t purge_non_control_queues() {
    uint32_t purged = 0;
    purged += purge_queue(g_queues[static_cast<size_t>(MessagePriority::DISCOVERY)]);
    purged += purge_queue(g_queues[static_cast<size_t>(MessagePriority::DATA)]);
    purged += purge_queue(g_queues[static_cast<size_t>(MessagePriority::MONITORING)]);
    return purged;
}

uint32_t purge_all_queues() {
    uint32_t purged = 0;
    for (size_t i = 0; i < static_cast<size_t>(MessagePriority::COUNT); ++i) {
        purged += purge_queue(g_queues[i]);
    }
    if (purged > 0) {
        LOG_INFO("ESPNOW_TX", "purge_all_queues: flushed %lu items", static_cast<unsigned long>(purged));
    }
    return purged;
}

esp_err_t send(const uint8_t* mac, const void* data, size_t len, const char* /*context*/) {
    if (!mac || !data || len == 0 || len > ESP_NOW_MAX_DATA_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t type = reinterpret_cast<const uint8_t*>(data)[0];
    const MessagePriority priority = classify_priority(type);
    if (is_control_only_mode() && priority != MessagePriority::CONTROL) {
        portENTER_CRITICAL(&g_stats_mux);
        g_stats.enqueue_drop++;
        bump_enqueue_drop_by_priority(priority);
        portEXIT_CRITICAL(&g_stats_mux);
        return ESP_ERR_INVALID_STATE;
    }

    if (!is_ready()) {
        // Allow safe direct send even if scheduler task/queue is not initialized.
        // Some early-boot or recovery paths call send() before init().
        if (g_options.no_mem_retry_attempts == 0) {
            g_options.no_mem_retry_attempts = 1;
        }
        if (g_options.retry_base_delay_ms == 0) {
            g_options.retry_base_delay_ms = 2;
        }

        const SendPolicy policy = policy_for(type);
        const esp_err_t result = send_immediate_with_retry(mac, reinterpret_cast<const uint8_t*>(data), len, policy.retry_attempts);
        portENTER_CRITICAL(&g_stats_mux);
        if (result == ESP_OK) {
            g_stats.sent_ok++;
            g_last_send_ms[type] = now_ms();
        } else {
            g_stats.send_fail++;
            bump_send_fail_by_priority(priority, result);
        }
        portEXIT_CRITICAL(&g_stats_mux);
        return result;
    }

    TxItem item{};
    memcpy(item.mac, mac, sizeof(item.mac));
    item.len = static_cast<uint8_t>(len);
    item.type = type;
    memcpy(item.payload, data, len);

    QueueHandle_t q = g_queues[static_cast<size_t>(priority)];
    if (q == nullptr) {
        return ESP_FAIL;
    }

    BaseType_t queued = (priority == MessagePriority::CONTROL)
                            ? xQueueSendToFront(q, &item, pdMS_TO_TICKS(g_options.queue_send_timeout_ms))
                            : xQueueSend(q, &item, pdMS_TO_TICKS(g_options.queue_send_timeout_ms));

    // Lossy behavior for lower priorities: drop oldest and retry enqueue.
    if (queued != pdTRUE && priority != MessagePriority::CONTROL) {
        TxItem dropped{};
        if (xQueueReceive(q, &dropped, 0) == pdTRUE) {
            queued = xQueueSend(q, &item, 0);
        }
    }

    portENTER_CRITICAL(&g_stats_mux);
    if (queued == pdTRUE) {
        g_stats.enqueued++;
    } else {
        g_stats.enqueue_drop++;
        bump_enqueue_drop_by_priority(priority);
    }
    portEXIT_CRITICAL(&g_stats_mux);

    return (queued == pdTRUE) ? ESP_OK : ESP_ERR_ESPNOW_NO_MEM;
}

bool read_stats(Stats& out_stats) {
    portENTER_CRITICAL(&g_stats_mux);
    out_stats = g_stats;
    portEXIT_CRITICAL(&g_stats_mux);
    return true;
}

bool read_queue_depths(QueueDepths& out_depths) {
    out_depths = {};
    out_depths.control = (g_queues[static_cast<size_t>(MessagePriority::CONTROL)] != nullptr)
        ? static_cast<uint32_t>(uxQueueMessagesWaiting(g_queues[static_cast<size_t>(MessagePriority::CONTROL)]))
        : 0U;
    out_depths.discovery = (g_queues[static_cast<size_t>(MessagePriority::DISCOVERY)] != nullptr)
        ? static_cast<uint32_t>(uxQueueMessagesWaiting(g_queues[static_cast<size_t>(MessagePriority::DISCOVERY)]))
        : 0U;
    out_depths.data = (g_queues[static_cast<size_t>(MessagePriority::DATA)] != nullptr)
        ? static_cast<uint32_t>(uxQueueMessagesWaiting(g_queues[static_cast<size_t>(MessagePriority::DATA)]))
        : 0U;
    out_depths.monitoring = (g_queues[static_cast<size_t>(MessagePriority::MONITORING)] != nullptr)
        ? static_cast<uint32_t>(uxQueueMessagesWaiting(g_queues[static_cast<size_t>(MessagePriority::MONITORING)]))
        : 0U;
    return true;
}

void reset_stats() {
    portENTER_CRITICAL(&g_stats_mux);
    g_stats = {};
    portEXIT_CRITICAL(&g_stats_mux);
}

uint32_t get_consecutive_no_mem_count() {
    portENTER_CRITICAL(&g_stats_mux);
    const uint32_t count = g_consecutive_no_mem_count;
    portEXIT_CRITICAL(&g_stats_mux);
    return count;
}

void reset_consecutive_no_mem_count() {
    portENTER_CRITICAL(&g_stats_mux);
    g_consecutive_no_mem_count = 0;
    portEXIT_CRITICAL(&g_stats_mux);
}

}  // namespace EspnowTxScheduler
