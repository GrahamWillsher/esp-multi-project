// ota_upload_handler.cpp
// Implements OtaManager::ota_upload_handler – the POST /ota_upload endpoint.
// Extracted from ota_manager.cpp to keep that file focused on server
// lifecycle, session management and the arm/status handlers.

#include "ota_manager.h"
#include "ota_manager_internal.h"
#include "../config/logging_config.h"
#include <webserver_common_utils/ota_auth_utils.h>
#include <firmware_metadata.h>
#include <firmware_compatibility_policy.h>
#include <firmware_version.h>
#include <Arduino.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <cstring>

esp_err_t OtaManager::ota_upload_handler(httpd_req_t *req) {
    auto& mgr = instance();

    char expected_sha256_hex[OTA_IMAGE_SHA256_HEX_LEN + 1] = {0};
    if (!OtaAuthUtils::get_header_value(req,
                                        "X-OTA-Image-SHA256",
                                        expected_sha256_hex,
                                        sizeof(expected_sha256_hex)) ||
        !is_hex_sha256(expected_sha256_hex)) {
        send_json_error(req,
                        HTTPD_400_BAD_REQUEST,
                        "Missing or invalid X-OTA-Image-SHA256 header",
                        "Provide lowercase/uppercase 64-char SHA-256 hex for firmware image");
        return ESP_FAIL;
    }

    char client_ip[24] = "unknown";
    (void)get_request_client_ip(req, client_ip, sizeof(client_ip));

    if (!mgr.validate_ota_auth_headers(req)) {
        LOG_WARN("HTTP_OTA", "OTA upload rejected during auth from %s", client_ip);
        return ESP_FAIL;
    }

    // Reject chunked Transfer-Encoding: OTA upload requires Content-Length for
    // pre-flight partition size validation and bounded receive loop termination.
    {
        char te_buf[32] = {0};
        if (OtaAuthUtils::get_header_value(req, "Transfer-Encoding",
                                           te_buf, sizeof(te_buf))) {
            if (header_has_token_ci(te_buf, "chunked")) {
                send_json_error(req, HTTPD_400_BAD_REQUEST,
                                "Chunked transfer encoding not supported; use Content-Length");
                return ESP_FAIL;
            }
        }
    }

    if (check_request_content_type(req, "application/octet-stream") != ESP_OK) {
        return ESP_FAIL;
    }

    if (req->content_len <= 0) {
        send_json_error(req, HTTPD_400_BAD_REQUEST, "Firmware payload is required");
        return ESP_FAIL;
    }

    const esp_partition_t* update_partition =
        esp_ota_get_next_update_partition(nullptr);
    if (update_partition != nullptr &&
        static_cast<size_t>(req->content_len) >
            static_cast<size_t>(update_partition->size)) {
        LOG_WARN("HTTP_OTA",
                 "Upload rejected: payload=%u exceeds update partition=%u",
                 static_cast<unsigned>(req->content_len),
                 static_cast<unsigned>(update_partition->size));
        send_json_error(req, HTTP_STATUS_PAYLOAD_TOO_LARGE,
                        "Firmware image too large for update partition");
        return ESP_FAIL;
    }

    constexpr size_t kOtaChunkSize = 1024;
    uint8_t* buf = static_cast<uint8_t*>(malloc(kOtaChunkSize));
    if (!buf) {
        send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Failed to allocate OTA buffer");
        return ESP_FAIL;
    }

    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    bool sha_ctx_active = false;

    auto fail_ota = [&](int status_code, const char* message) -> esp_err_t {
        if (sha_ctx_active) {
            mbedtls_sha256_free(&sha_ctx);
            sha_ctx_active = false;
        }
        free(buf);
        send_json_error(req, status_code, message);
        mgr.ota_in_progress_ = false;
        mgr.set_commit_state("prepare_failed", message);
        return ESP_FAIL;
    };

    auto fail_ota_with_update_abort = [&](int status_code,
                                          const char* message) -> esp_err_t {
        if (sha_ctx_active) {
            mbedtls_sha256_free(&sha_ctx);
            sha_ctx_active = false;
        }
        Update.abort();
        free(buf);
        send_json_error(req, status_code, message);
        mgr.ota_in_progress_ = false;
        mgr.set_commit_state("prepare_failed", message);
        return ESP_FAIL;
    };

    size_t remaining     = req->content_len;
    const size_t expected_size = req->content_len;
    size_t total_written = 0;
    size_t last_reported = 0;
    int    timeout_count = 0;
    FirmwareCompatibilityPolicy::MetadataScan metadata_scan;

    if (mbedtls_sha256_starts_ret(&sha_ctx, 0) != 0) {
        send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Failed to initialize image hash");
        mbedtls_sha256_free(&sha_ctx);
        free(buf);
        return ESP_FAIL;
    }
    sha_ctx_active = true;

    LOG_INFO("HTTP_OTA", "=== OTA START ===");
    LOG_INFO("HTTP_OTA", "OTA upload source IP: %s", client_ip);
    LOG_INFO("HTTP_OTA", "Receiving OTA update, size: %u bytes",
             (unsigned)remaining);
    LOG_INFO("HTTP_OTA", "Heap before OTA: free=%u, max_alloc=%u",
             ESP.getFreeHeap(), ESP.getMaxAllocHeap());

    mgr.ota_in_progress_   = true;
    mgr.ota_ready_for_reboot_ = false;
    mgr.ota_last_success_  = false;
    mgr.ota_txn_id_        = static_cast<uint32_t>(esp_random());
    mgr.set_commit_state("prepare_upload", "receiving firmware image");
    strncpy(mgr.ota_last_error_, "", sizeof(mgr.ota_last_error_) - 1);
    mgr.ota_last_error_[sizeof(mgr.ota_last_error_) - 1] = '\0';

    LOG_INFO("HTTP_OTA", "Calling Update.begin(UPDATE_SIZE_UNKNOWN)...");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        LOG_ERROR("HTTP_OTA", "Update.begin failed: %s", Update.errorString());
        LOG_ERROR("HTTP_OTA", "Update.begin error code: %d", Update.getError());
        strncpy(mgr.ota_last_error_, Update.errorString(),
                sizeof(mgr.ota_last_error_) - 1);
        mgr.ota_last_error_[sizeof(mgr.ota_last_error_) - 1] = '\0';
        return fail_ota(HTTPD_500_INTERNAL_SERVER_ERROR, "Update begin failed");
    }
    mgr.set_commit_state("prepare_writing", "streaming firmware to inactive slot");
    LOG_INFO("HTTP_OTA", "Update.begin OK");

    // Receive and write firmware data
    while (remaining > 0) {
        const int recv_len = httpd_req_recv(
            req,
            reinterpret_cast<char*>(buf),
            (remaining < kOtaChunkSize) ? remaining : kOtaChunkSize);

        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
                timeout_count++;
                if (timeout_count >= OTA_UPLOAD_MAX_RECV_TIMEOUTS) {
                    LOG_ERROR("HTTP_OTA",
                              "Upload timed out after %d recv timeouts "
                              "(remaining=%u, written=%u)",
                              timeout_count, (unsigned)remaining,
                              (unsigned)total_written);
                    strncpy(mgr.ota_last_error_, "Upload timeout",
                            sizeof(mgr.ota_last_error_) - 1);
                    mgr.ota_last_error_[sizeof(mgr.ota_last_error_) - 1] = '\0';
                    return fail_ota_with_update_abort(HTTP_STATUS_REQUEST_TIMEOUT,
                                                      "Upload timed out");
                }
                if ((timeout_count % 20) == 0) {
                    LOG_WARN("HTTP_OTA",
                             "recv timeout x%d, remaining=%u, written=%u",
                             timeout_count, (unsigned)remaining,
                             (unsigned)total_written);
                }
                continue;
            }
            LOG_ERROR("HTTP_OTA",
                      "Connection error during upload (recv_len=%d, "
                      "remaining=%u, written=%u)",
                      recv_len, (unsigned)remaining, (unsigned)total_written);
            strncpy(mgr.ota_last_error_, "Connection error during upload",
                    sizeof(mgr.ota_last_error_) - 1);
            mgr.ota_last_error_[sizeof(mgr.ota_last_error_) - 1] = '\0';
            return fail_ota_with_update_abort(HTTPD_500_INTERNAL_SERVER_ERROR,
                                              "Connection error");
        }

        // Write firmware chunk
        if (Update.write((uint8_t*)buf, recv_len) != (size_t)recv_len) {
            LOG_ERROR("HTTP_OTA", "Update.write failed: %s", Update.errorString());
            LOG_ERROR("HTTP_OTA",
                      "Update.write error code: %d (chunk=%d, remaining=%u, written=%u)",
                      Update.getError(), recv_len, (unsigned)remaining,
                      (unsigned)total_written);
            strncpy(mgr.ota_last_error_, Update.errorString(),
                    sizeof(mgr.ota_last_error_) - 1);
            mgr.ota_last_error_[sizeof(mgr.ota_last_error_) - 1] = '\0';
            return fail_ota_with_update_abort(HTTPD_500_INTERNAL_SERVER_ERROR,
                                              "Write failed");
        }

        if (mbedtls_sha256_update_ret(
                &sha_ctx,
                reinterpret_cast<const unsigned char*>(buf),
                static_cast<size_t>(recv_len)) != 0) {
            strncpy(mgr.ota_last_error_, "Image hash update failed",
                    sizeof(mgr.ota_last_error_) - 1);
            mgr.ota_last_error_[sizeof(mgr.ota_last_error_) - 1] = '\0';
            return fail_ota_with_update_abort(HTTPD_500_INTERNAL_SERVER_ERROR,
                                              "Image hash update failed");
        }

        metadata_scan.consume(reinterpret_cast<const uint8_t*>(buf),
                              static_cast<size_t>(recv_len));

        remaining      -= recv_len;
        total_written  += (size_t)recv_len;

        if ((total_written - last_reported) >= 32768 || remaining == 0) {
            last_reported = total_written;
            const unsigned percent =
                (expected_size > 0)
                    ? (unsigned)((total_written * 100U) / expected_size)
                    : 0U;
            LOG_DEBUG("HTTP_OTA",
                      "Progress: %u%%, written=%u/%u, remaining=%u, heap=%u",
                      percent, (unsigned)total_written, (unsigned)expected_size,
                      (unsigned)remaining, ESP.getFreeHeap());
        }
    }

    LOG_INFO("HTTP_OTA",
             "Receive loop complete: written=%u bytes, expected=%u bytes",
             (unsigned)total_written, (unsigned)expected_size);

    unsigned char computed_sha[32] = {0};
    if (mbedtls_sha256_finish_ret(&sha_ctx, computed_sha) != 0) {
        strncpy(mgr.ota_last_error_, "Image hash finalize failed",
                sizeof(mgr.ota_last_error_) - 1);
        mgr.ota_last_error_[sizeof(mgr.ota_last_error_) - 1] = '\0';
        return fail_ota_with_update_abort(HTTPD_500_INTERNAL_SERVER_ERROR,
                                          "Image hash finalize failed");
    }
    mbedtls_sha256_free(&sha_ctx);
    sha_ctx_active = false;

    char computed_sha_hex[OTA_IMAGE_SHA256_HEX_LEN + 1] = {0};
    for (size_t index = 0; index < sizeof(computed_sha); ++index) {
        (void)snprintf(&computed_sha_hex[index * 2], 3, "%02x",
                       computed_sha[index]);
    }

    if (strcasecmp(computed_sha_hex, expected_sha256_hex) != 0) {
        LOG_ERROR("HTTP_OTA",
                  "Image SHA-256 mismatch: expected=%s computed=%s",
                  expected_sha256_hex, computed_sha_hex);
        strncpy(mgr.ota_last_error_, "Image hash verification failed",
                sizeof(mgr.ota_last_error_) - 1);
        mgr.ota_last_error_[sizeof(mgr.ota_last_error_) - 1] = '\0';
        return fail_ota_with_update_abort(HTTPD_400_BAD_REQUEST,
                                          "Image hash verification failed");
    }

    const auto compatibility = FirmwareCompatibilityPolicy::validate_scan(
        metadata_scan,
        "TRANSMITTER",
        static_cast<uint8_t>(FW_VERSION_MAJOR));

    if (!compatibility.allowed) {
        const char* reject_message = "Firmware compatibility policy rejected image";
        const char* user_message   = reject_message;

        switch (compatibility.code) {
            case FirmwareCompatibilityPolicy::ValidationCode::InvalidMetadataStructure:
                reject_message = "Invalid firmware metadata";
                user_message   = "Invalid firmware metadata structure";
                break;
            case FirmwareCompatibilityPolicy::ValidationCode::DeviceTypeMismatch:
                reject_message = "Firmware target mismatch";
                user_message   = "Firmware target mismatch (expected TRANSMITTER)";
                break;
            case FirmwareCompatibilityPolicy::ValidationCode::MajorVersionIncompatible:
                reject_message = "Firmware major version incompatible";
                user_message   = "Firmware major version incompatible with running transmitter";
                break;
            case FirmwareCompatibilityPolicy::ValidationCode::MinimumCompatibleMajorIncompatible:
                reject_message = "Firmware minimum compatible major requirement not met";
                user_message   = "Firmware requires newer transmitter major compatibility";
                break;
            default:
                break;
        }

        LOG_ERROR("HTTP_OTA",
                  "Compatibility reject code=%s device=%s image_major=%u "
                  "min_compat_major=%u running_major=%u",
                  FirmwareCompatibilityPolicy::validation_code_to_string(
                      compatibility.code),
                  compatibility.normalized_device_type,
                  static_cast<unsigned>(compatibility.image_major),
                  static_cast<unsigned>(compatibility.image_min_compatible_major),
                  static_cast<unsigned>(FW_VERSION_MAJOR));

        strncpy(mgr.ota_last_error_, reject_message,
                sizeof(mgr.ota_last_error_) - 1);
        mgr.ota_last_error_[sizeof(mgr.ota_last_error_) - 1] = '\0';
        return fail_ota_with_update_abort(HTTPD_400_BAD_REQUEST, user_message);
    }

    if (compatibility.code ==
        FirmwareCompatibilityPolicy::ValidationCode::LegacyAllowed) {
        LOG_WARN("HTTP_OTA", "%s", compatibility.message);
    }

    LOG_INFO("HTTP_OTA", "Image SHA-256 verified successfully");
    LOG_INFO("HTTP_OTA", "Calling Update.end(true)...");

    // Finalize update
    if (Update.end(true)) {
        free(buf);
        LOG_INFO("HTTP_OTA", "Update successful! Size: %u bytes", Update.size());
        LOG_INFO("HTTP_OTA",
                 "OTA ready for reboot. Heap after OTA: free=%u, max_alloc=%u",
                 ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        mgr.ota_in_progress_      = false;
        mgr.ota_ready_for_reboot_ = true;
        mgr.ota_last_success_     = true;
        mgr.ota_session_.deactivate();
        mgr.set_commit_state("prepared_waiting_reboot",
                             "firmware staged; awaiting reboot command");

        StaticJsonDocument<224> ok_doc;
        ok_doc["success"]          = true;
        ok_doc["code"]             = 200;
        ok_doc["message"]          = "OTA applied. Waiting for reboot command";
        ok_doc["ready_for_reboot"] = true;
        ok_doc["ota_txn_id"]       = mgr.ota_txn_id_;
        char ok_json[224];
        const size_t ok_json_len = serializeJson(ok_doc, ok_json, sizeof(ok_json));
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        httpd_resp_send(req, ok_json,
                        ok_json_len > 0 ? ok_json_len : HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    } else {
        free(buf);
        LOG_ERROR("HTTP_OTA", "Update.end failed: %s", Update.errorString());
        LOG_ERROR("HTTP_OTA",
                  "Update.end error code: %d, written=%u, expected=%u",
                  Update.getError(), (unsigned)total_written,
                  (unsigned)expected_size);
        LOG_ERROR("HTTP_OTA",
                  "Heap on failure: free=%u, max_alloc=%u",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        strncpy(mgr.ota_last_error_, Update.errorString(),
                sizeof(mgr.ota_last_error_) - 1);
        mgr.ota_last_error_[sizeof(mgr.ota_last_error_) - 1] = '\0';
        send_json_error(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        Update.errorString());
        mgr.ota_in_progress_ = false;
        mgr.ota_session_.deactivate();
        mgr.set_commit_state("prepare_failed", Update.errorString());
        return ESP_FAIL;
    }
}
