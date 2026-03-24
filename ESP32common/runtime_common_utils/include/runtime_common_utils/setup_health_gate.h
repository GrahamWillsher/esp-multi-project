#pragma once

#include <stddef.h>

namespace SetupHealthGate {

struct Check {
    const char* name;
    bool ok;
};

enum class Outcome {
    RollbackTriggered = 0,
    ConfirmedHealthy,
    ConfirmedSuboptimal,
    Error,
};

/**
 * @brief Apply OTA setup health-gate policy.
 *
 * Policy:
 *  - If OTA pending-verify and any check fails: trigger rollback.
 *  - Otherwise: confirm running app.
 *
 * @param log_tag           Serial log tag.
 * @param checks            Health checks array.
 * @param check_count       Number of checks.
 * @param rollback_reason   Reason string used when rollback is triggered.
 * @param confirm_reason    Reason string used when app is confirmed.
 * @return Outcome          Decision path taken.
 */
Outcome apply(const char* log_tag,
              const Check* checks,
              size_t check_count,
              const char* rollback_reason,
              const char* confirm_reason);

} // namespace SetupHealthGate
