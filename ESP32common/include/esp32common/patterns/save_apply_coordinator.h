#pragma once

#include <cstdint>
#include <cstring>

namespace Esp32Common {
namespace Patterns {

/**
 * @brief Reusable coordinator for remote save/apply operations.
 *
 * This pattern models the common lifecycle used by paired-device operations:
 * request dispatched -> persisted -> ready_for_reboot | failed | timed_out.
 *
 * Transport-specific code remains outside this class. Callers are responsible
 * for dispatching the request and feeding the resulting ACK/status back into
 * the coordinator.
 */
class SaveApplyCoordinator {
public:
    enum class State : uint8_t {
        idle = 0,
        pending,
        persisted,
        ready_for_reboot,
        failed,
        timed_out
    };

    struct Snapshot {
        uint32_t request_id = 0;
        State state = State::idle;
        uint32_t started_ms = 0;
        uint32_t updated_ms = 0;
        uint8_t requested_mask = 0;
        uint8_t persisted_mask = 0;
        bool success = false;
        bool reboot_required = false;
        bool ready_for_reboot = false;
        uint32_t settings_version = 0;
        char message[48] = {0};
    };

    explicit SaveApplyCoordinator(uint32_t timeout_ms = 15000)
        : timeout_ms_(timeout_ms) {
        reset();
    }

    void reset() {
        snapshot_ = Snapshot{};
        set_message(default_message_for_state(snapshot_.state));
    }

    bool start_transaction(uint32_t request_id,
                           uint8_t requested_mask,
                           uint32_t now_ms,
                           const char* message = nullptr) {
        if (request_id == 0) {
            return false;
        }

        snapshot_ = Snapshot{};
        snapshot_.request_id = request_id;
        snapshot_.state = State::pending;
        snapshot_.started_ms = now_ms;
        snapshot_.updated_ms = now_ms;
        snapshot_.requested_mask = requested_mask;
        set_message(message);
        return true;
    }

    bool bind_recovery_request(uint32_t request_id,
                               uint32_t now_ms,
                               const char* message = nullptr) {
        if (request_id == 0 || snapshot_.request_id != 0) {
            return false;
        }

        snapshot_.request_id = request_id;
        snapshot_.state = State::pending;
        snapshot_.started_ms = now_ms;
        snapshot_.updated_ms = now_ms;
        set_message(message);
        return true;
    }

    bool mark_failed(uint32_t request_id, uint32_t now_ms, const char* message) {
        if (snapshot_.request_id != request_id) {
            return false;
        }

        snapshot_.state = State::failed;
        snapshot_.success = false;
        snapshot_.reboot_required = false;
        snapshot_.ready_for_reboot = false;
        snapshot_.updated_ms = now_ms;
        set_message(message);
        return true;
    }

    bool apply_result(uint32_t request_id,
                      uint8_t requested_mask,
                      uint8_t persisted_mask,
                      bool success,
                      bool reboot_required,
                      bool ready_for_reboot,
                      uint32_t settings_version,
                      uint32_t now_ms,
                      const char* message) {
        if (snapshot_.request_id != request_id) {
            return false;
        }

        snapshot_.updated_ms = now_ms;
        snapshot_.requested_mask = requested_mask;
        snapshot_.persisted_mask = persisted_mask;
        snapshot_.success = success;
        snapshot_.reboot_required = reboot_required;
        snapshot_.ready_for_reboot = ready_for_reboot;
        snapshot_.settings_version = settings_version;

        if (success && ready_for_reboot) {
            snapshot_.state = State::ready_for_reboot;
        } else if (success) {
            snapshot_.state = State::persisted;
        } else {
            snapshot_.state = State::failed;
        }

        set_message(message);
        return true;
    }

    Snapshot snapshot(uint32_t now_ms) {
        if (snapshot_.state == State::pending && snapshot_.started_ms > 0) {
            if ((now_ms - snapshot_.started_ms) > timeout_ms_) {
                snapshot_.state = State::timed_out;
                snapshot_.updated_ms = now_ms;
                set_message(nullptr);
            }
        }

        return snapshot_;
    }

    static const char* state_to_string(State state) {
        switch (state) {
            case State::idle: return "idle";
            case State::pending: return "pending";
            case State::persisted: return "persisted";
            case State::ready_for_reboot: return "ready_for_reboot";
            case State::failed: return "failed";
            case State::timed_out: return "timed_out";
            default: return "unknown";
        }
    }

private:
    static const char* default_message_for_state(State state) {
        switch (state) {
            case State::idle: return "Idle";
            case State::pending: return "Pending transmitter confirmation";
            case State::persisted: return "Persisted";
            case State::ready_for_reboot: return "Persisted - ready for reboot";
            case State::failed: return "Failed";
            case State::timed_out: return "Timed out waiting for ACK";
            default: return "Unknown";
        }
    }

    void set_message(const char* message) {
        const char* src = (message && message[0] != '\0')
            ? message
            : default_message_for_state(snapshot_.state);
        std::strncpy(snapshot_.message, src, sizeof(snapshot_.message) - 1);
        snapshot_.message[sizeof(snapshot_.message) - 1] = '\0';
    }

    Snapshot snapshot_{};
    uint32_t timeout_ms_;
};

} // namespace Patterns
} // namespace Esp32Common
