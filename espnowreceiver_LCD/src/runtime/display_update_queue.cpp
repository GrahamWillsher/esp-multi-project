#include "runtime/display_update_queue.h"

#include <freertos/queue.h>

#include "common_lcd.h"

namespace DisplayUpdateQueue {

bool init(size_t depth) {
    if (RTOS::display_update_queue != nullptr) {
        return true;
    }
    RTOS::display_update_queue = xQueueCreate(static_cast<UBaseType_t>(depth), sizeof(snapshot_t));
    return RTOS::display_update_queue != nullptr;
}

bool enqueue(const snapshot_t& snapshot) {
    if (RTOS::display_update_queue == nullptr) {
        return false;
    }

    if (xQueueSend(RTOS::display_update_queue, &snapshot, 0) == pdPASS) {
        return true;
    }

    snapshot_t dropped{};
    (void)xQueueReceive(RTOS::display_update_queue, &dropped, 0);
    return xQueueSend(RTOS::display_update_queue, &snapshot, 0) == pdPASS;
}

bool try_dequeue(snapshot_t& out_snapshot) {
    if (RTOS::display_update_queue == nullptr) {
        return false;
    }
    return xQueueReceive(RTOS::display_update_queue, &out_snapshot, 0) == pdPASS;
}

}  // namespace DisplayUpdateQueue
