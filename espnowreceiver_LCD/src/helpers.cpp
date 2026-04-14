#include "helpers.h"

void smart_delay(uint32_t ms) {
    if (xTaskGetCurrentTaskHandle() != nullptr &&
        xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        TickType_t ticks = pdMS_TO_TICKS(ms);
        if (ticks == 0 && ms > 0) {
            ticks = 1;  // guarantee at least one yield for sub-tick durations
        }
        vTaskDelay(ticks);
    } else {
        delay(ms);
    }
}
