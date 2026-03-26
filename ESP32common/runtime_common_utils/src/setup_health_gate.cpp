#include <runtime_common_utils/setup_health_gate.h>

#include <Arduino.h>
#include <runtime_common_utils/ota_boot_guard.h>
#include <string.h>
#include <logging_config.h>

namespace {

bool all_checks_ok(const SetupHealthGate::Check* checks, size_t check_count) {
    if (!checks || check_count == 0) {
        return false;
    }
    for (size_t i = 0; i < check_count; ++i) {
        if (!checks[i].ok) {
            return false;
        }
    }
    return true;
}

void format_checks(const SetupHealthGate::Check* checks,
                   size_t check_count,
                   char* out,
                   size_t out_size) {
    if (!out || out_size == 0) {
        return;
    }

    out[0] = '\0';
    if (!checks || check_count == 0) {
        strlcpy(out, "no_checks=0", out_size);
        return;
    }

    for (size_t i = 0; i < check_count; ++i) {
        const char* name = (checks[i].name && checks[i].name[0] != '\0') ? checks[i].name : "check";
        const char* fmt = (i == 0) ? "%s=%d" : ", %s=%d";
        const int written = snprintf(out + strlen(out),
                                     out_size - strlen(out),
                                     fmt,
                                     name,
                                     checks[i].ok ? 1 : 0);
        if (written <= 0 || (size_t)written >= (out_size - strlen(out))) {
            break;
        }
    }
}

} // namespace

namespace SetupHealthGate {

Outcome apply(const char* log_tag,
              const Check* checks,
              size_t check_count,
              const char* rollback_reason,
              const char* confirm_reason) {
    const char* tag = (log_tag && log_tag[0] != '\0') ? log_tag : "BOOT_GUARD";
    const bool health_ok = all_checks_ok(checks, check_count);

    char status_buf[192];
    format_checks(checks, check_count, status_buf, sizeof(status_buf));

    if (OtaBootGuard::is_pending_verification() && !health_ok) {
        LOG_ERROR(tag, "Setup health gate failed (%s); triggering rollback", status_buf);
        const bool rollback_requested =
            OtaBootGuard::trigger_rollback_and_reboot(
                (rollback_reason && rollback_reason[0] != '\0')
                    ? rollback_reason
                    : "setup health gate failed");
        return rollback_requested ? Outcome::RollbackTriggered : Outcome::Error;
    }

    if (!health_ok) {
        LOG_WARN(tag, "Setup health gate suboptimal on normal boot (%s); confirming anyway", status_buf);
        if (!OtaBootGuard::confirm_running_app(
                (confirm_reason && confirm_reason[0] != '\0')
                    ? confirm_reason
                    : "setup health gate suboptimal but confirmed")) {
            return Outcome::Error;
        }
        return Outcome::ConfirmedSuboptimal;
    }

    if (!OtaBootGuard::confirm_running_app(
            (confirm_reason && confirm_reason[0] != '\0')
                ? confirm_reason
                : "setup health gate passed")) {
        return Outcome::Error;
    }

    LOG_INFO(tag, "Setup health gate passed (%s)", status_buf);
    return Outcome::ConfirmedHealthy;
}

} // namespace SetupHealthGate
