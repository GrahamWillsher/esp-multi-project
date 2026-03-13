#include "test_data.h"
#include "../common.h"
#include "../display/display_core.h"
#include "../display/display_led.h"

extern void notify_sse_data_updated();

// DEPRECATED: All test mode functionality has been moved to transmitter
// These functions are deprecated stubs kept for build compatibility

// Global test mode variables (used by API handlers for web UI)
// Test mode is now disabled on receiver - it's a pure display device
bool& test_mode_enabled = *(new bool(false));  // Always false - test data comes from transmitter
volatile int& g_test_soc = *(new volatile int(0));
volatile int32_t& g_test_power = *(new volatile int32_t(0));
volatile uint32_t& g_test_voltage_mv = *(new volatile uint32_t(0));

void generate_test_data() {
    // Test mode now handled by transmitter via transmission selector
    return;
}

void task_generate_test_data(void *parameter) {
    // Test mode task no longer runs on receiver
    vTaskDelete(NULL);
}

// Status indicator task
void taskStatusIndicator(void *parameter) {
    // Status indicator deprecated - receiver is pure display device
    vTaskDelete(NULL);
}
