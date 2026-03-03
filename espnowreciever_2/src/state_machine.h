#pragma once

#include "common.h"

class SystemStateManager {
public:
	static SystemStateManager& instance();

	void init();
	void update();

	uint32_t get_state_duration_ms() const;

private:
	SystemStateManager() = default;

	uint32_t state_entry_time_ms_ = 0;

	static constexpr uint32_t BOOT_TIMEOUT_MS = 30000;
	static constexpr uint32_t TX_WAIT_TIMEOUT_MS = 60000;
	static constexpr uint32_t DATA_STALE_TIMEOUT_MS = 10000;
	static constexpr uint32_t ERROR_RECOVERY_RETRY_MS = 30000;
};

// Note: SystemState and ErrorSeverity enums are defined in common.h
// This header just declares the functions that operate on them

void transition_to_state(SystemState new_state);
void handle_error(ErrorSeverity severity, const char* component, const char* message);
