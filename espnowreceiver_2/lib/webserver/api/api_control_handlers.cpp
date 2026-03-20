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
#include <esp_now.h>
#include <esp32common/espnow/common.h>

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
        const char* raw_last_error = doc["last_error"] | "";
        char safe_last_error[128];
        size_t copy_index = 0;
        while (raw_last_error[copy_index] != '\0' && copy_index < sizeof(safe_last_error) - 1) {
            safe_last_error[copy_index] = (raw_last_error[copy_index] == '"') ? '\'' : raw_last_error[copy_index];
            copy_index++;
        }
        safe_last_error[copy_index] = '\0';

        StaticJsonDocument<256> out_doc;
        out_doc["success"] = true;
        out_doc["in_progress"] = in_progress;
        out_doc["ready_for_reboot"] = ready_for_reboot;
        out_doc["last_success"] = last_success;
        out_doc["last_error"] = safe_last_error;

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

esp_err_t api_ota_upload_handler(httpd_req_t *req) {
    size_t remaining = req->content_len;
    LOG_INFO("OTA: Receiving firmware upload, total size: %d bytes", remaining);

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
    // The transmitter requires X-OTA-Session/Nonce/Expires/Signature before accepting the body.
    char ota_session_id[40] = {0};
    char ota_nonce[40] = {0};
    char ota_expires_str[24] = {0};
    char ota_signature[80] = {0};
    {
        const String arm_url = TransmitterManager::getURL() + "/api/ota_arm";
        HTTPClient arm_client;
        arm_client.begin(arm_url);
        arm_client.setTimeout(5000);
        int arm_code = arm_client.sendRequest("POST", static_cast<uint8_t*>(nullptr), 0);
        if (arm_code != 200) {
            LOG_ERROR("OTA: Failed to arm OTA session on transmitter, HTTP %d", arm_code);
            arm_client.end();
            return ApiResponseUtils::send_error_message(req, "Failed to arm OTA session on transmitter");
        }
        String arm_body = arm_client.getString();
        arm_client.end();

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
        LOG_INFO("OTA: Session armed, id=%.8s... expires_at=%s", ota_session_id, ota_expires_str);
    }

    const uint8_t* ip = TransmitterManager::getIP();
    IPAddress tx_ip(ip[0], ip[1], ip[2], ip[3]);
    WiFiClient tx_client;
    tx_client.setTimeout(60000);

    if (!tx_client.connect(tx_ip, 80)) {
        return ApiResponseUtils::send_error_message(req, "Failed to connect to transmitter OTA server");
    }

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

            // No progress: allow short retry window for transient socket backpressure.
            if (!tx_client.connected() || (millis() - write_start_ms) > 15000) {
                tx_client.stop();
                return ApiResponseUtils::send_error_message(req, "Failed to forward OTA chunk to transmitter");
            }
            vTaskDelay(pdMS_TO_TICKS(2));
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
