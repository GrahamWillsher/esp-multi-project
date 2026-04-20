#include "espnow_tx_scheduler.h"

#include <esp_now.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <cstring>

#include <log_routed.h>

namespace EspnowTxScheduler {
namespace {

struct TxItem {
    uint8_t mac[6];
    uint8_t len;
    uint8_t type;
    uint8_t payload[ESP_NOW_MAX_DATA_LEN];
};

QueueHandle_t g_queue = nullptr;
TaskHandle_t g_task = nullptr;
InitOptions g_options{};
Stats g_stats{};
portMUX_TYPE g_stats_mux = portMUX_INITIALIZER_UNLOCKED;

esp_err_t send_immediate_with_retry(const uint8_t* mac, const uint8_t* data, size_t len) {
    esp_err_t result = ESP_FAIL;
    for (uint8_t attempt = 0; attempt < g_options.no_mem_retry_attempts; ++attempt) {
        result = esp_now_send(mac, data, len);
        if (result == ESP_OK) {
            return ESP_OK;
        }

        if (result != ESP_ERR_ESPNOW_NO_MEM || (attempt + 1U) >= g_options.no_mem_retry_attempts) {
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

    for (;;) {
        if (xQueueReceive(g_queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        const esp_err_t result = send_immediate_with_retry(item.mac, item.payload, item.len);

        portENTER_CRITICAL(&g_stats_mux);
        if (result == ESP_OK) {
            g_stats.sent_ok++;
        } else {
            g_stats.send_fail++;
        }
        portEXIT_CRITICAL(&g_stats_mux);

        if (result != ESP_OK) {
            LOG_WARN("ESPNOW_TX", "Send failed (type=%u len=%u): %s",
                     static_cast<unsigned>(item.type),
                     static_cast<unsigned>(item.len),
                     esp_err_to_name(result));
        }

        if (g_options.inter_frame_delay_ms > 0U) {
            vTaskDelay(pdMS_TO_TICKS(g_options.inter_frame_delay_ms));
        }
    }
}

}  // namespace

bool init(const InitOptions& options) {
    if (g_task != nullptr && g_queue != nullptr) {
        return true;
    }

    g_options = options;
    if (g_options.queue_depth == 0) {
        g_options.queue_depth = 1;
    }
    if (g_options.no_mem_retry_attempts == 0) {
        g_options.no_mem_retry_attempts = 1;
    }

    if (g_queue == nullptr) {
        g_queue = xQueueCreate(g_options.queue_depth, sizeof(TxItem));
        if (g_queue == nullptr) {
            LOG_ERROR("ESPNOW_TX", "Failed to create queue (depth=%u)",
                      static_cast<unsigned>(g_options.queue_depth));
            return false;
        }
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
    return g_queue != nullptr && g_task != nullptr;
}

esp_err_t send(const uint8_t* mac, const void* data, size_t len, const char* /*context*/) {
    if (!mac || !data || len == 0 || len > ESP_NOW_MAX_DATA_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!is_ready()) {
        const esp_err_t result = send_immediate_with_retry(mac, reinterpret_cast<const uint8_t*>(data), len);
        portENTER_CRITICAL(&g_stats_mux);
        if (result == ESP_OK) {
            g_stats.sent_ok++;
        } else {
            g_stats.send_fail++;
        }
        portEXIT_CRITICAL(&g_stats_mux);
        return result;
    }

    TxItem item{};
    memcpy(item.mac, mac, sizeof(item.mac));
    item.len = static_cast<uint8_t>(len);
    item.type = reinterpret_cast<const uint8_t*>(data)[0];
    memcpy(item.payload, data, len);

    const BaseType_t queued = xQueueSend(g_queue, &item, pdMS_TO_TICKS(g_options.queue_send_timeout_ms));
    portENTER_CRITICAL(&g_stats_mux);
    if (queued == pdTRUE) {
        g_stats.enqueued++;
    } else {
        g_stats.enqueue_drop++;
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

void reset_stats() {
    portENTER_CRITICAL(&g_stats_mux);
    g_stats = {};
    portEXIT_CRITICAL(&g_stats_mux);
}

}  // namespace EspnowTxScheduler
