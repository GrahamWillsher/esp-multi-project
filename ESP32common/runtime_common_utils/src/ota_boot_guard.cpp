#include <runtime_common_utils/ota_boot_guard.h>

#include <Arduino.h>
#include <esp_ota_ops.h>
#include <logging_config.h>

namespace {
OtaBootGuard::State g_state = OtaBootGuard::State::Unknown;
bool g_pending_verify = false;
bool g_was_pending_at_boot = false;   // true if this boot started as an OTA pending-verify boot
char g_reason[96] = "init";
char g_log_tag[20] = "BOOT_GUARD";

void set_reason(const char* reason) {
    if (!reason || reason[0] == '\0') {
        return;
    }
    strlcpy(g_reason, reason, sizeof(g_reason));
}

const char* state_to_string(OtaBootGuard::State state) {
    switch (state) {
        case OtaBootGuard::State::Unknown: return "unknown";
        case OtaBootGuard::State::NotPending: return "not_pending";
        case OtaBootGuard::State::PendingVerification: return "pending_verify";
        case OtaBootGuard::State::Confirmed: return "confirmed";
        case OtaBootGuard::State::RollbackTriggered: return "rollback_triggered";
        case OtaBootGuard::State::Error: return "error";
        default: return "unknown";
    }
}
}

namespace OtaBootGuard {

void begin(const char* log_tag) {
    if (log_tag && log_tag[0] != '\0') {
        strlcpy(g_log_tag, log_tag, sizeof(g_log_tag));
    }

    const esp_partition_t* running = esp_ota_get_running_partition();
    if (!running) {
        g_pending_verify = false;
        g_state = State::Error;
        set_reason("running partition unavailable");
        LOG_ERROR(g_log_tag, "running partition unavailable");
        return;
    }

    esp_ota_img_states_t ota_state = ESP_OTA_IMG_UNDEFINED;
    const esp_err_t err = esp_ota_get_state_partition(running, &ota_state);
    if (err != ESP_OK) {
        g_pending_verify = false;
        g_state = State::Error;
        set_reason("esp_ota_get_state_partition failed");
        LOG_ERROR(g_log_tag, "esp_ota_get_state_partition failed (%d)", (int)err);
        return;
    }

    g_pending_verify = (ota_state == ESP_OTA_IMG_PENDING_VERIFY);
    g_was_pending_at_boot = g_pending_verify;  // snapshot: true for OTA reboot, false for normal boot
    if (g_pending_verify) {
        g_state = State::PendingVerification;
        set_reason("running image pending verify");
           LOG_INFO(g_log_tag, "Running image is pending verify");
    } else {
        g_state = State::NotPending;
        set_reason("running image not pending verify");
           LOG_INFO(g_log_tag, "Running image state=%d (no pending verify)", (int)ota_state);
    }
}

bool is_pending_verification() {
    return g_pending_verify;
}

bool confirm_running_app(const char* reason) {
    if (!g_pending_verify) {
        g_state = State::Confirmed;
        set_reason(reason ? reason : "confirm no-op (not pending)");
        return true;
    }

    const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        g_pending_verify = false;
        g_state = State::Confirmed;
        set_reason(reason ? reason : "app marked valid");
        LOG_INFO(g_log_tag, "OTA app marked valid; rollback cancelled");
        return true;
    }

    g_state = State::Error;
    set_reason(reason ? reason : "failed to mark app valid");
    LOG_ERROR(g_log_tag, "Failed to mark app valid (%d)", (int)err);
    return false;
}

bool trigger_rollback_and_reboot(const char* reason) {
    if (!g_pending_verify) {
        g_state = State::Error;
        set_reason(reason ? reason : "rollback requested while not pending verify");
        LOG_WARN(g_log_tag, "Rollback requested but app is not pending verify");
        return false;
    }

    g_state = State::RollbackTriggered;
    set_reason(reason ? reason : "health gate failed; rollback requested");
    LOG_WARN(g_log_tag, "Triggering rollback and reboot");

    const esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
    if (err != ESP_OK) {
        g_state = State::Error;
        set_reason("esp_ota_mark_app_invalid_rollback_and_reboot failed");
        LOG_ERROR(g_log_tag, "Rollback API failed (%d)", (int)err);
        return false;
    }

    return true;
}

State state() {
    return g_state;
}

const char* state_string() {
    return state_to_string(g_state);
}

const char* last_reason() {
    return g_reason;
}

bool was_pending_at_boot() {
    return g_was_pending_at_boot;
}

} // namespace OtaBootGuard
