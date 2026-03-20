#include <webserver_common_utils/ota_session_utils.h>

#include <webserver_common_utils/ota_auth_utils.h>

#include <cstdio>
#include <cstring>

namespace {

constexpr size_t OTA_SESSION_BYTES = 16;

bool compute_signature_for_fields(const char* session_id,
                                  const char* nonce,
                                  uint32_t expires_at_ms,
                                  const char* psk,
                                  uint32_t device_mac_lower32,
                                  char* out_hex,
                                  size_t out_len) {
    if (!session_id || !nonce || !psk || !out_hex || out_len < 65) {
        return false;
    }

    char message[192];
    const int msg_len = snprintf(message,
                                 sizeof(message),
                                 "%s|%s|%lu|%08lX",
                                 session_id,
                                 nonce,
                                 static_cast<unsigned long>(expires_at_ms),
                                 static_cast<unsigned long>(device_mac_lower32 & 0xFFFFFFFFUL));
    if (msg_len <= 0 || msg_len >= static_cast<int>(sizeof(message))) {
        return false;
    }

    return OtaAuthUtils::compute_hmac_sha256_hex(psk, message, out_hex, out_len);
}

} // namespace

namespace OtaSessionUtils {

void Session::arm(uint32_t now_ms,
                  uint32_t ttl_ms,
                  uint8_t max_attempts,
                  const uint8_t* requester_mac) {
    OtaAuthUtils::random_hex(session_id_, OTA_SESSION_BYTES);
    OtaAuthUtils::random_hex(nonce_, OTA_SESSION_BYTES);

    expires_at_ms_ = now_ms + ttl_ms;
    attempts_remaining_ = max_attempts;
    active_ = true;
    consumed_ = false;

    if (requester_mac) {
        memcpy(requester_mac_, requester_mac, sizeof(requester_mac_));
    }
}

ValidationResult Session::validate_and_consume(const char* session_id,
                                               const char* nonce,
                                               uint32_t expires_at_ms,
                                               const char* signature,
                                               const char* psk,
                                               uint32_t device_mac_lower32,
                                               uint32_t now_ms) {
    if (!active_) {
        return ValidationResult::NotArmed;
    }
    if (consumed_) {
        return ValidationResult::Consumed;
    }

    if (now_ms > expires_at_ms_ || now_ms > expires_at_ms || expires_at_ms != expires_at_ms_) {
        active_ = false;
        return ValidationResult::Expired;
    }

    if (attempts_remaining_ == 0) {
        active_ = false;
        return ValidationResult::Locked;
    }

    if (!session_id || !nonce || strcmp(session_id, session_id_) != 0 || strcmp(nonce, nonce_) != 0) {
        attempts_remaining_--;
        return ValidationResult::InvalidSession;
    }

    char expected_sig[65] = {0};
    if (!compute_signature_for_fields(session_id,
                                      nonce,
                                      expires_at_ms,
                                      psk,
                                      device_mac_lower32,
                                      expected_sig,
                                      sizeof(expected_sig))) {
        return ValidationResult::InternalError;
    }

    if (!OtaAuthUtils::constant_time_equals(signature, expected_sig)) {
        attempts_remaining_--;
        if (attempts_remaining_ == 0) {
            active_ = false;
        }
        return ValidationResult::InvalidSignature;
    }

    consumed_ = true;
    return ValidationResult::Valid;
}

bool Session::compute_signature(const char* psk,
                                uint32_t device_mac_lower32,
                                char* out_hex,
                                size_t out_len) const {
    return compute_signature_for_fields(session_id_,
                                        nonce_,
                                        expires_at_ms_,
                                        psk,
                                        device_mac_lower32,
                                        out_hex,
                                        out_len);
}

void Session::deactivate() {
    active_ = false;
}

} // namespace OtaSessionUtils
