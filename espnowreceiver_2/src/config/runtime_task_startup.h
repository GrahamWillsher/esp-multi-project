#pragma once

#include <freertos/FreeRTOS.h>

namespace RuntimeTaskStartup {

void create_runtime_primitives();
void start_runtime_tasks(TaskFunction_t led_renderer_task_fn);

} // namespace RuntimeTaskStartup
