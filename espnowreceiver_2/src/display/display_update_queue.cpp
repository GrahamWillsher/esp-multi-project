#include "display_update_queue.h"

#include "display.h"
#include "../common.h"
#include "../config/task_config.h"

namespace DisplayUpdateQueue {

namespace {
QueueHandle_t s_queue = nullptr;
constexpr size_t kQueueDepth = 8;
}

void init() {
    if (s_queue != nullptr) {
        return;
    }

    s_queue = xQueueCreate(kQueueDepth, sizeof(Snapshot));
    if (s_queue == nullptr) {
        LOG_ERROR("DISPLAY", "Failed to create display snapshot queue");
        return;
    }

    LOG_INFO("DISPLAY", "Display snapshot queue initialized (depth=%u)", static_cast<unsigned>(kQueueDepth));
}

bool enqueue(uint8_t soc, int32_t power_w, bool soc_changed, bool power_changed) {
    if (!soc_changed && !power_changed) {
        return true;
    }

    if (s_queue == nullptr) {
        return false;
    }

    Snapshot snapshot = {
        .soc = soc,
        .power_w = power_w,
        .soc_changed = soc_changed,
        .power_changed = power_changed,
    };

    if (xQueueSend(s_queue, &snapshot, 0) == pdTRUE) {
        return true;
    }

    Snapshot dropped = {};
    (void)xQueueReceive(s_queue, &dropped, 0);
    return xQueueSend(s_queue, &snapshot, 0) == pdTRUE;
}

void task_renderer(void* parameter) {
    (void)parameter;
    LOG_INFO("DISPLAY", "Display renderer task started");

    Snapshot snapshot = {};
    for (;;) {
        if (xQueueReceive(s_queue, &snapshot, pdMS_TO_TICKS(1000)) != pdTRUE) {
            continue;
        }

        if (xSemaphoreTake(RTOS::tft_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            LOG_WARN("DISPLAY", "TFT mutex timeout - display snapshot dropped");
            continue;
        }

        if (snapshot.soc_changed) {
            display_soc(static_cast<float>(snapshot.soc));
        }
        if (snapshot.power_changed) {
            display_power(snapshot.power_w);
        }

        xSemaphoreGive(RTOS::tft_mutex);
    }
}

}  // namespace DisplayUpdateQueue
