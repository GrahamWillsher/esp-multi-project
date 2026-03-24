#pragma once

#include <stdbool.h>

namespace OtaBootGuard {

enum class State {
    Unknown = 0,
    NotPending,
    PendingVerification,
    Confirmed,
    RollbackTriggered,
    Error,
};

/**
 * @brief Initialize boot-guard state from current running partition.
 * @param log_tag Optional tag for logs.
 */
void begin(const char* log_tag = "BOOT_GUARD");

/**
 * @brief True if current app is running in pending-verify state.
 */
bool is_pending_verification();

/**
 * @brief True if this boot started with a pending-verify image (i.e. an OTA reboot).
 *        Remains true for the lifetime of this boot session even after confirmation.
 */
bool was_pending_at_boot();

/**
 * @brief Confirm running app as valid and cancel rollback if pending.
 * @param reason Optional diagnostic message.
 * @return true on success (or no-op when not pending), false on error.
 */
bool confirm_running_app(const char* reason = nullptr);

/**
 * @brief Mark app invalid and trigger rollback reboot when pending verify.
 * @param reason Optional diagnostic message.
 * @return true if rollback API call succeeded.
 */
bool trigger_rollback_and_reboot(const char* reason = nullptr);

/**
 * @brief Current guard state.
 */
State state();

/**
 * @brief Human-readable state string.
 */
const char* state_string();

/**
 * @brief Last state transition reason.
 */
const char* last_reason();

} // namespace OtaBootGuard
