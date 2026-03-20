#ifndef WEBSERVER_COMMON_UTILS_OTA_SESSION_UTILS_H
#define WEBSERVER_COMMON_UTILS_OTA_SESSION_UTILS_H

#include <stddef.h>
#include <stdint.h>

namespace OtaSessionUtils {

enum class ValidationResult {
    Valid,
    NotArmed,
    Consumed,
    Expired,
    Locked,
    InvalidSession,
    InvalidSignature,
    InternalError,
};

class Session {
public:
    void arm(uint32_t now_ms,
             uint32_t ttl_ms,
             uint8_t max_attempts,
             const uint8_t* requester_mac = nullptr);

    ValidationResult validate_and_consume(const char* session_id,
                                          const char* nonce,
                                          uint32_t expires_at_ms,
                                          const char* signature,
                                          const char* psk,
                                          uint32_t device_mac_lower32,
                                          uint32_t now_ms);

    bool compute_signature(const char* psk,
                           uint32_t device_mac_lower32,
                           char* out_hex,
                           size_t out_len) const;

    void deactivate();

    bool is_active() const { return active_; }
    bool is_consumed() const { return consumed_; }
    uint8_t attempts_remaining() const { return attempts_remaining_; }
    uint32_t expires_at_ms() const { return expires_at_ms_; }
    const char* session_id() const { return session_id_; }
    const char* nonce() const { return nonce_; }

private:
    bool active_{false};
    bool consumed_{false};
    uint8_t attempts_remaining_{0};
    uint32_t expires_at_ms_{0};
    char session_id_[33] = {0};
    char nonce_[33] = {0};
    uint8_t requester_mac_[6] = {0};
};

} // namespace OtaSessionUtils

#endif // WEBSERVER_COMMON_UTILS_OTA_SESSION_UTILS_H
