#include "api_control_handlers.h"

#include "api_response_utils.h"
#include "../utils/transmitter_manager.h"
#include "../utils/http_json_utils.h"
#include "../logging.h"

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <esp_now.h>
#include <esp32common/espnow/common.h>

namespace ESPNow {
    extern uint8_t transmitter_mac[6];
}

esp_err_t api_reboot_handler(httpd_req_t *req) {
    const uint8_t* target_mac = nullptr;
    const char* mac_source = "none";

    if (TransmitterManager::isMACKnown()) {
        target_mac = TransmitterManager::getMAC();
        mac_source = "TransmitterManager";
    } else {
        bool has_espnow_mac = false;
        for (int i = 0; i < 6; ++i) {
            if (ESPNow::transmitter_mac[i] != 0) {
                has_espnow_mac = true;
                break;
            }
        }
        if (has_espnow_mac) {
            target_mac = ESPNow::transmitter_mac;
            mac_source = "ESPNow::transmitter_mac";
        }
    }

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
        LOG_WARN("REBOOT: Transmitter MAC unknown in both caches, cannot send command");
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

        StaticJsonDocument<256> doc;
        DeserializationError err = deserializeJson(doc, body);
        if (err) {
            return ApiResponseUtils::send_error_message(req, "Invalid OTA status JSON");
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
    return ApiResponseUtils::send_jsonf(req, "{\"success\":false,\"message\":\"Status HTTP error: %d\"}", code);
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
    tx_client.print("Connection: close\r\n\r\n");

    char buf[1024];
    size_t total_forwarded = 0;
    while (remaining > 0) {
        int read_len = httpd_req_recv(req, buf, (remaining < sizeof(buf)) ? remaining : sizeof(buf));
        if (read_len <= 0) {
            if (read_len == HTTPD_SOCK_ERR_TIMEOUT) continue;

            tx_client.stop();
            return ApiResponseUtils::send_error_message(req, "Upload receive failed during streaming");
        }

        size_t sent = tx_client.write((const uint8_t*)buf, read_len);
        if (sent != (size_t)read_len) {
            tx_client.stop();
            return ApiResponseUtils::send_error_message(req, "Failed to forward OTA chunk to transmitter");
        }

        remaining -= read_len;
        total_forwarded += sent;
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
