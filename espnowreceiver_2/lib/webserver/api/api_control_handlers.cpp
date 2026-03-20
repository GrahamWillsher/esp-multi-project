#include "api_control_handlers.h"

#include "api_response_utils.h"
#include "../utils/transmitter_manager.h"
#include <webserver_common_utils/http_json_utils.h>
#include "../logging.h"

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <esp_now.h>
#include <esp32common/espnow/common.h>
#include <firmware_version.h>
#include <firmware_compatibility_policy.h>
#include <mbedtls/sha256.h>

namespace {
constexpr size_t OTA_IMAGE_SHA256_HEX_LEN = 64;

bool is_hex_sha256(const char* value) {
    if (!value) {
        return false;
    }

    size_t len = 0;
    while (value[len] != '\0') {
        const char ch = value[len];
        const bool is_hex = (ch >= '0' && ch <= '9') ||
                            (ch >= 'a' && ch <= 'f') ||
                            (ch >= 'A' && ch <= 'F');
        if (!is_hex) {
            return false;
        }
        ++len;
    }

    return len == OTA_IMAGE_SHA256_HEX_LEN;
}
}

esp_err_t api_reboot_handler(httpd_req_t *req) {
    const uint8_t* target_mac = TransmitterManager::getMAC();
    const char* mac_source = "TransmitterManager";

    if (target_mac != nullptr) {
        reboot_t reboot_msg = { msg_reboot };
        esp_err_t result = esp_now_send(target_mac, (const uint8_t*)&reboot_msg, sizeof(reboot_msg));
        if (result == ESP_OK) {
            LOG_INFO("REBOOT: Sent command to transmitter via %s", mac_source);
            return ApiResponseUtils::send_jsonf(req,
                                                "{\"success\":true,\"message\":\"Reboot command sent\",\"source\":\"%s\"}",
                                                mac_source);
        } else {
            LOG_ERROR("REBOOT: Failed to send command: %s", esp_err_to_name(result));
            return ApiResponseUtils::send_error_message(req, esp_err_to_name(result));
        }
    } else {
        LOG_WARN("REBOOT: Transmitter MAC unknown, cannot send command");
        return ApiResponseUtils::send_error_message(req, "Transmitter MAC unknown");
    }
}

esp_err_t api_transmitter_ota_status_handler(httpd_req_t *req) {
    if (!TransmitterManager::isIPKnown()) {
        return ApiResponseUtils::send_error_message(req, "Transmitter IP unknown");
    }

    const String status_url = TransmitterManager::getURL() + "/api/ota_status";
    HTTPClient http;
    http.begin(status_url);
    http.setTimeout(3000);

    int code = http.GET();
    if (code == 200) {
        String body = http.getString();
        http.end();

        StaticJsonDocument<1024> doc;
        DeserializationError err = deserializeJson(doc, body);
        if (err) {
            LOG_ERROR("OTA: Failed to parse transmitter OTA status JSON: %s", err.c_str());
            return ApiResponseUtils::send_jsonf(req,
                                                "{\"success\":false,\"message\":\"Invalid OTA status JSON\",\"detail\":\"%s\"}",
                                                err.c_str());
        }

        bool in_progress = doc["in_progress"] | false;
        bool ready_for_reboot = doc["ready_for_reboot"] | false;
        bool last_success = doc["last_success"] | false;
        uint32_t ota_txn_id = doc["ota_txn_id"] | 0U;
        bool rollback_pending = doc["rollback_pending"] | false;
        bool boot_guard_passed = doc["boot_guard_passed"] | false;
        const char* boot_guard_state = doc["boot_guard_state"] | "unknown";
        const char* commit_state = doc["commit_state"] | "unknown";
        const char* commit_detail = doc["commit_detail"] | "";
        uint32_t state_since_ms = doc["state_since_ms"] | 0U;
        uint32_t last_update_ms = doc["last_update_ms"] | 0U;
        const char* raw_rollback_reason = doc["rollback_reason"] | "";
        const char* raw_last_error = doc["last_error"] | "";
        char safe_last_error[128];
        char safe_rollback_reason[128];
        char safe_commit_detail[128];
        size_t copy_index = 0;
        while (raw_last_error[copy_index] != '\0' && copy_index < sizeof(safe_last_error) - 1) {
            safe_last_error[copy_index] = (raw_last_error[copy_index] == '"') ? '\'' : raw_last_error[copy_index];
            copy_index++;
        }
        safe_last_error[copy_index] = '\0';

        copy_index = 0;
        while (raw_rollback_reason[copy_index] != '\0' && copy_index < sizeof(safe_rollback_reason) - 1) {
            safe_rollback_reason[copy_index] = (raw_rollback_reason[copy_index] == '"') ? '\'' : raw_rollback_reason[copy_index];
            copy_index++;
        }
        safe_rollback_reason[copy_index] = '\0';

        copy_index = 0;
        while (commit_detail[copy_index] != '\0' && copy_index < sizeof(safe_commit_detail) - 1) {
            safe_commit_detail[copy_index] = (commit_detail[copy_index] == '"') ? '\'' : commit_detail[copy_index];
            copy_index++;
        }
        safe_commit_detail[copy_index] = '\0';

        StaticJsonDocument<512> out_doc;
        out_doc["success"] = true;
        out_doc["in_progress"] = in_progress;
        out_doc["ready_for_reboot"] = ready_for_reboot;
        out_doc["last_success"] = last_success;
        out_doc["ota_txn_id"] = ota_txn_id;
        out_doc["commit_state"] = commit_state;
        out_doc["commit_detail"] = safe_commit_detail;
        out_doc["state_since_ms"] = state_since_ms;
        out_doc["last_update_ms"] = last_update_ms;
        out_doc["last_error"] = safe_last_error;
        out_doc["rollback_pending"] = rollback_pending;
        out_doc["boot_guard_passed"] = boot_guard_passed;
        out_doc["boot_guard_state"] = boot_guard_state;
        out_doc["rollback_reason"] = safe_rollback_reason;

        String json;
        json.reserve(160);
        serializeJson(out_doc, json);
        return HttpJsonUtils::send_json(req, json.c_str());
    }

    http.end();
    return ApiResponseUtils::send_jsonf(req,
                                        "{\"success\":false,\"message\":\"Status HTTP error: %d\",\"in_progress\":false,\"ready_for_reboot\":false,\"last_success\":false}",
                                        code);
}

esp_err_t api_ota_upload_receiver_handler(httpd_req_t *req) {
    if (req->content_len <= 0) {
        return ApiResponseUtils::send_error_message(req, "Firmware payload required");
    }

    char image_sha256_hex[OTA_IMAGE_SHA256_HEX_LEN + 1] = {0};
    if (httpd_req_get_hdr_value_str(req,
                                    "X-OTA-Image-SHA256",
                                    image_sha256_hex,
                                    sizeof(image_sha256_hex)) != ESP_OK ||
        !is_hex_sha256(image_sha256_hex)) {
        return ApiResponseUtils::send_error_message(req, "Missing or invalid X-OTA-Image-SHA256 header");
    }

    char content_type[96] = {0};
    const bool has_content_type = (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) == ESP_OK);
    const bool is_raw_upload = has_content_type && (strstr(content_type, "application/octet-stream") != nullptr);
    if (!is_raw_upload) {
        return ApiResponseUtils::send_error_message(req, "Unsupported upload type. Use raw application/octet-stream");
    }

    LOG_INFO("OTA_RX", "Starting receiver self-OTA upload, size=%d", req->content_len);

    if (!Update.begin(static_cast<size_t>(req->content_len))) {
        LOG_ERROR("OTA_RX", "Update.begin failed: %s", Update.errorString());
        return ApiResponseUtils::send_error_message(req, Update.errorString());
    }

    char buf[1024];
    size_t remaining = static_cast<size_t>(req->content_len);
    size_t written_total = 0;
    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    FirmwareCompatibilityPolicy::MetadataScan metadata_scan;
    if (mbedtls_sha256_starts_ret(&sha_ctx, 0) != 0) {
        Update.abort();
        mbedtls_sha256_free(&sha_ctx);
        return ApiResponseUtils::send_error_message(req, "Failed to initialize image hash");
    }

    while (remaining > 0) {
        const int recv_len = httpd_req_recv(req,
                                            buf,
                                            (remaining < sizeof(buf)) ? static_cast<int>(remaining) : static_cast<int>(sizeof(buf)));

        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            Update.abort();
            mbedtls_sha256_free(&sha_ctx);
            LOG_ERROR("OTA_RX", "Upload receive error while streaming receiver OTA");
            return ApiResponseUtils::send_error_message(req, "Upload receive failed");
        }

        const size_t written = Update.write(reinterpret_cast<uint8_t*>(buf), static_cast<size_t>(recv_len));
        if (written != static_cast<size_t>(recv_len)) {
            Update.abort();
            mbedtls_sha256_free(&sha_ctx);
            LOG_ERROR("OTA_RX", "Update.write failed: %s", Update.errorString());
            return ApiResponseUtils::send_error_message(req, Update.errorString());
        }

        if (mbedtls_sha256_update_ret(&sha_ctx,
                                      reinterpret_cast<const unsigned char*>(buf),
                                      static_cast<size_t>(recv_len)) != 0) {
            Update.abort();
            mbedtls_sha256_free(&sha_ctx);
            return ApiResponseUtils::send_error_message(req, "Image hash update failed");
        }

        metadata_scan.consume(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(recv_len));

        remaining -= static_cast<size_t>(recv_len);
        written_total += static_cast<size_t>(recv_len);
    }

    unsigned char computed_sha[32] = {0};
    if (mbedtls_sha256_finish_ret(&sha_ctx, computed_sha) != 0) {
        Update.abort();
        mbedtls_sha256_free(&sha_ctx);
        return ApiResponseUtils::send_error_message(req, "Image hash finalize failed");
    }
    mbedtls_sha256_free(&sha_ctx);

    char computed_sha_hex[OTA_IMAGE_SHA256_HEX_LEN + 1] = {0};
    for (size_t index = 0; index < sizeof(computed_sha); ++index) {
        (void)snprintf(&computed_sha_hex[index * 2], 3, "%02x", computed_sha[index]);
    }

    if (strcasecmp(computed_sha_hex, image_sha256_hex) != 0) {
        Update.abort();
        LOG_ERROR("OTA_RX", "Image SHA-256 mismatch: expected=%s computed=%s", image_sha256_hex, computed_sha_hex);
        return ApiResponseUtils::send_error_message(req, "Image hash verification failed");
    }

    const auto compatibility = FirmwareCompatibilityPolicy::validate_scan(
        metadata_scan,
        "RECEIVER",
        static_cast<uint8_t>(FW_VERSION_MAJOR));

    if (!compatibility.allowed) {
        const char* user_message = "Firmware compatibility policy rejected image";

        switch (compatibility.code) {
            case FirmwareCompatibilityPolicy::ValidationCode::InvalidMetadataStructure:
                user_message = "Invalid firmware metadata structure";
                break;
            case FirmwareCompatibilityPolicy::ValidationCode::DeviceTypeMismatch:
                user_message = "Firmware target mismatch (expected RECEIVER)";
                break;
            case FirmwareCompatibilityPolicy::ValidationCode::MajorVersionIncompatible:
                user_message = "Firmware major version incompatible with running receiver";
                break;
            case FirmwareCompatibilityPolicy::ValidationCode::MinimumCompatibleMajorIncompatible:
                user_message = "Firmware requires newer receiver major compatibility";
                break;
            default:
                break;
        }

        Update.abort();
        LOG_ERROR("OTA_RX",
                  "Compatibility reject code=%s device=%s image_major=%u min_compat_major=%u running_major=%u",
                  FirmwareCompatibilityPolicy::validation_code_to_string(compatibility.code),
                  compatibility.normalized_device_type,
                  static_cast<unsigned>(compatibility.image_major),
                  static_cast<unsigned>(compatibility.image_min_compatible_major),
                  static_cast<unsigned>(FW_VERSION_MAJOR));
        return ApiResponseUtils::send_error_message(req, user_message);
    }

    if (compatibility.code == FirmwareCompatibilityPolicy::ValidationCode::LegacyAllowed) {
        LOG_WARN("OTA_RX", "%s", compatibility.message);
    }

    if (!Update.end(true)) {
        LOG_ERROR("OTA_RX", "Update.end failed: %s", Update.errorString());
        return ApiResponseUtils::send_error_message(req, Update.errorString());
    }

    LOG_INFO("OTA_RX", "Receiver OTA successful, written=%u bytes", static_cast<unsigned>(written_total));
    HttpJsonUtils::send_json(req, "{\"success\":true,\"message\":\"Receiver firmware uploaded. Rebooting...\"}");

    vTaskDelay(pdMS_TO_TICKS(250));
    ESP.restart();
    return ESP_OK;
}

esp_err_t api_ota_upload_handler(httpd_req_t *req) {
    size_t remaining = req->content_len;
    LOG_INFO("OTA: Receiving firmware upload, total size: %d bytes", remaining);

    char image_sha256_hex[OTA_IMAGE_SHA256_HEX_LEN + 1] = {0};
    if (httpd_req_get_hdr_value_str(req,
                                    "X-OTA-Image-SHA256",
                                    image_sha256_hex,
                                    sizeof(image_sha256_hex)) != ESP_OK ||
        !is_hex_sha256(image_sha256_hex)) {
        return ApiResponseUtils::send_error_message(req, "Missing or invalid X-OTA-Image-SHA256 header");
    }

    char content_type[96] = {0};
    bool has_content_type = (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) == ESP_OK);
    bool is_raw_upload = has_content_type && (strstr(content_type, "application/octet-stream") != nullptr);
    LOG_INFO("OTA: Content-Type: %s (%s mode)", has_content_type ? content_type : "<none>", is_raw_upload ? "raw/stream" : "unsupported");

    if (!is_raw_upload) {
        return ApiResponseUtils::send_error_message(req, "Unsupported upload type. Use raw application/octet-stream");
    }

    if (!TransmitterManager::isIPKnown()) {
        return ApiResponseUtils::send_error_message(req, "Transmitter IP unknown");
    }

    const size_t firmware_size = remaining;

    if (TransmitterManager::isMACKnown()) {
        ota_start_t ota_msg = { msg_ota_start, (uint32_t)firmware_size };
        esp_now_send(TransmitterManager::getMAC(), (const uint8_t*)&ota_msg, sizeof(ota_msg));
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    // Fetch OTA session challenge from transmitter so auth headers can be sent with the upload.
    // Preferred: use pre-armed challenge from /api/ota_status (after OTA_START control msg).
    // Fallback: explicitly call /api/ota_arm.
    char ota_session_id[40] = {0};
    char ota_nonce[40] = {0};
    char ota_expires_str[24] = {0};
    char ota_signature[80] = {0};

    bool challenge_ready = false;
    {
        const String status_url = TransmitterManager::getURL() + "/api/ota_status";
        HTTPClient status_client;
        status_client.begin(status_url);
        status_client.setTimeout(5000);
        int status_code = status_client.GET();
        if (status_code == 200) {
            String status_body = status_client.getString();
            StaticJsonDocument<1024> status_doc;
            const bool status_ok = !deserializeJson(status_doc, status_body) && status_doc["success"].as<bool>();
            if (status_ok &&
                (status_doc["session_active"] | false) &&
                (status_doc["signature_available"] | false)) {
                strlcpy(ota_session_id, status_doc["session_id"] | "", sizeof(ota_session_id));
                strlcpy(ota_nonce,      status_doc["nonce"]      | "", sizeof(ota_nonce));
                strlcpy(ota_signature,  status_doc["signature"]  | "", sizeof(ota_signature));
                uint32_t expires_at_ms = status_doc["expires_at_ms"] | 0U;
                snprintf(ota_expires_str, sizeof(ota_expires_str), "%lu", (unsigned long)expires_at_ms);

                challenge_ready = (ota_session_id[0] != '\0' && ota_nonce[0] != '\0' && ota_signature[0] != '\0' && expires_at_ms > 0);
                if (challenge_ready) {
                    LOG_INFO("OTA: Reusing pre-armed OTA challenge from /api/ota_status, id=%.8s...", ota_session_id);
                }
            }
        }
        status_client.end();

        if (!challenge_ready) {
            const String arm_url = TransmitterManager::getURL() + "/api/ota_arm";
            HTTPClient arm_client;
            arm_client.begin(arm_url);
            arm_client.setTimeout(5000);
            int arm_code = arm_client.sendRequest("POST", static_cast<uint8_t*>(nullptr), 0);
            String arm_body = arm_client.getString();
            arm_client.end();

            if (arm_code != 200) {
                String detail = "HTTP " + String(arm_code);
                StaticJsonDocument<256> err_doc;
                if (!deserializeJson(err_doc, arm_body)) {
                    const char* msg = err_doc["message"] | "";
                    const char* details = err_doc["details"] | "";
                    if (msg[0] != '\0') {
                        detail = String(msg);
                        if (details[0] != '\0') {
                            detail += ": ";
                            detail += details;
                        }
                    }
                }

                LOG_ERROR("OTA: Failed to arm OTA session on transmitter (%s)", detail.c_str());
                if (detail.indexOf("OTA secret not provisioned") >= 0) {
                    return ApiResponseUtils::send_error_message(req, "Transmitter OTA secret not provisioned (set security/ota_psk in NVS)");
                }
                return ApiResponseUtils::send_error_message(req, detail.c_str());
            }

            StaticJsonDocument<400> arm_doc;
            if (deserializeJson(arm_doc, arm_body) || !arm_doc["success"].as<bool>()) {
                LOG_ERROR("OTA: Invalid OTA arm response from transmitter");
                return ApiResponseUtils::send_error_message(req, "Invalid OTA arm response from transmitter");
            }
            strlcpy(ota_session_id, arm_doc["session_id"] | "", sizeof(ota_session_id));
            strlcpy(ota_nonce,      arm_doc["nonce"]       | "", sizeof(ota_nonce));
            strlcpy(ota_signature,  arm_doc["signature"]   | "", sizeof(ota_signature));
            uint32_t expires_at_ms = arm_doc["expires_at_ms"] | 0U;
            snprintf(ota_expires_str, sizeof(ota_expires_str), "%lu", (unsigned long)expires_at_ms);
            challenge_ready = (ota_session_id[0] != '\0' && ota_nonce[0] != '\0' && ota_signature[0] != '\0' && expires_at_ms > 0);
            LOG_INFO("OTA: Session armed, id=%.8s... expires_at=%s", ota_session_id, ota_expires_str);
        }
    }

    if (!challenge_ready) {
        LOG_ERROR("OTA: Unable to obtain OTA challenge from transmitter status/arm endpoints");
        return ApiResponseUtils::send_error_message(req, "Unable to obtain OTA challenge from transmitter");
    }

    const uint8_t* ip = TransmitterManager::getIP();
    IPAddress tx_ip(ip[0], ip[1], ip[2], ip[3]);
    WiFiClient tx_client;
    tx_client.setTimeout(60000);

    if (!tx_client.connect(tx_ip, 80)) {
        return ApiResponseUtils::send_error_message(req, "Failed to connect to transmitter OTA server");
    }
    tx_client.setNoDelay(true);

    tx_client.print("POST /ota_upload HTTP/1.1\r\n");
    tx_client.print("Host: ");
    tx_client.print(tx_ip.toString());
    tx_client.print("\r\n");
    tx_client.print("Content-Type: application/octet-stream\r\n");
    tx_client.print("Content-Length: ");
    tx_client.print((unsigned)firmware_size);
    tx_client.print("\r\n");
    tx_client.print("X-OTA-Session: ");
    tx_client.print(ota_session_id);
    tx_client.print("\r\n");
    tx_client.print("X-OTA-Nonce: ");
    tx_client.print(ota_nonce);
    tx_client.print("\r\n");
    tx_client.print("X-OTA-Expires: ");
    tx_client.print(ota_expires_str);
    tx_client.print("\r\n");
    tx_client.print("X-OTA-Signature: ");
    tx_client.print(ota_signature);
    tx_client.print("\r\n");
    tx_client.print("X-OTA-Image-SHA256: ");
    tx_client.print(image_sha256_hex);
    tx_client.print("\r\n");
    tx_client.print("Connection: close\r\n\r\n");

    auto read_transmitter_response = [&](int* out_status, String* out_body) -> bool {
        if (!out_status || !out_body || !tx_client.available()) {
            return false;
        }

        String status_line = tx_client.readStringUntil('\n');
        status_line.trim();

        int status_code = -1;
        int sp1 = status_line.indexOf(' ');
        if (sp1 > 0 && status_line.length() >= sp1 + 4) {
            status_code = status_line.substring(sp1 + 1, sp1 + 4).toInt();
        }

        while (tx_client.available()) {
            String h = tx_client.readStringUntil('\n');
            if (h == "\r" || h.length() == 0) break;
        }

        String body = "";
        unsigned long body_start = millis();
        while ((tx_client.connected() || tx_client.available()) && (millis() - body_start < 1200)) {
            while (tx_client.available()) {
                body += static_cast<char>(tx_client.read());
                if (body.length() > 512) break;
            }
            if (body.length() > 512) break;
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        *out_status = status_code;
        *out_body = body;
        return true;
    };

    char buf[1024];
    size_t total_forwarded = 0;
    while (remaining > 0) {
        int read_len = httpd_req_recv(req, buf, (remaining < sizeof(buf)) ? remaining : sizeof(buf));
        if (read_len <= 0) {
            if (read_len == HTTPD_SOCK_ERR_TIMEOUT) continue;

            tx_client.stop();
            return ApiResponseUtils::send_error_message(req, "Upload receive failed during streaming");
        }

        size_t offset = 0;
        const uint32_t write_start_ms = millis();
        while (offset < static_cast<size_t>(read_len)) {
            const size_t to_write = static_cast<size_t>(read_len) - offset;
            size_t sent = tx_client.write(reinterpret_cast<const uint8_t*>(buf) + offset, to_write);
            if (sent > 0) {
                offset += sent;
                continue;
            }

            int early_status = -1;
            String early_body;
            if (read_transmitter_response(&early_status, &early_body)) {
                tx_client.stop();
                early_body.replace("\"", "'");
                StaticJsonDocument<768> out_doc;
                char message[96];
                snprintf(message, sizeof(message), "Transmitter OTA rejected upload: HTTP %d", early_status);
                out_doc["success"] = false;
                out_doc["message"] = message;
                out_doc["detail"] = early_body;
                String err_json;
                err_json.reserve(256 + early_body.length());
                serializeJson(out_doc, err_json);
                LOG_ERROR("OTA: Transmitter replied early with HTTP %d while forwarding at byte %u", early_status, (unsigned)(total_forwarded + offset));
                return HttpJsonUtils::send_json(req, err_json.c_str());
            }

            // No progress: allow generous retry window for transient socket backpressure
            // on WiFi/LWIP path during long OTA streams.
            if (!tx_client.connected() || (millis() - write_start_ms) > 60000) {
                LOG_ERROR("OTA: Stream stall while forwarding at byte=%u chunk_offset=%u connected=%d",
                          static_cast<unsigned>(total_forwarded),
                          static_cast<unsigned>(offset),
                          tx_client.connected() ? 1 : 0);
                tx_client.stop();
                return ApiResponseUtils::send_error_message(req, "Failed to forward OTA chunk to transmitter");
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        remaining -= read_len;
        total_forwarded += static_cast<size_t>(read_len);
    }

    unsigned long wait_start = millis();
    while (!tx_client.available() && tx_client.connected() && (millis() - wait_start < 70000)) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (!tx_client.available()) {
        tx_client.stop();
        return ApiResponseUtils::send_error_message(req, "No response from transmitter after streaming OTA");
    }

    String status_line = tx_client.readStringUntil('\n');
    status_line.trim();

    int status_code = -1;
    int sp1 = status_line.indexOf(' ');
    if (sp1 > 0 && status_line.length() >= sp1 + 4) {
        status_code = status_line.substring(sp1 + 1, sp1 + 4).toInt();
    }

    while (tx_client.available()) {
        String h = tx_client.readStringUntil('\n');
        if (h == "\r" || h.length() == 0) break;
    }

    String body = "";
    unsigned long body_start = millis();
    while ((tx_client.connected() || tx_client.available()) && (millis() - body_start < 3000)) {
        while (tx_client.available()) {
            body += (char)tx_client.read();
            if (body.length() > 512) break;
        }
        if (body.length() > 512) break;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    tx_client.stop();

    LOG_INFO("OTA: Streamed %u bytes to transmitter, HTTP status=%d", (unsigned)total_forwarded, status_code);

    if (status_code == 200) {
        return HttpJsonUtils::send_json(req, "{\"success\":true,\"message\":\"Firmware streamed to transmitter\",\"status\":\"forwarded\"}");
    } else {
        body.replace("\"", "'");
        StaticJsonDocument<768> out_doc;
        char message[80];
        snprintf(message, sizeof(message), "Transmitter OTA HTTP error: %d", status_code);
        out_doc["success"] = false;
        out_doc["message"] = message;
        out_doc["detail"] = body;
        String err_json;
        err_json.reserve(256 + body.length());
        serializeJson(out_doc, err_json);
        return HttpJsonUtils::send_json(req, err_json.c_str());
    }
}
