#include "ota_manager.h"
#include "../config/logging_config.h"
#include <Arduino.h>
#include <Update.h>
#include <firmware_metadata.h>
#include <firmware_version.h>

OtaManager& OtaManager::instance() {
    static OtaManager instance;
    return instance;
}

esp_err_t OtaManager::ota_upload_handler(httpd_req_t *req) {
    auto& mgr = instance();
    char buf[1024];
    size_t remaining = req->content_len;
    
    LOG_INFO("[HTTP_OTA] Receiving OTA update, size: %d bytes", remaining);
    
    // Stop other tasks to free resources
    mgr.ota_in_progress_ = true;
    
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        LOG_ERROR("[HTTP_OTA] Update.begin failed: %s", Update.errorString());
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Update begin failed");
        mgr.ota_in_progress_ = false;
        return ESP_FAIL;
    }
    
    // Receive and write firmware data
    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, (remaining < sizeof(buf)) ? remaining : sizeof(buf));
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            LOG_ERROR("[HTTP_OTA] Connection error during upload");
            Update.abort();
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Connection error");
            mgr.ota_in_progress_ = false;
            return ESP_FAIL;
        }
        
        // Write firmware chunk
        if (Update.write((uint8_t*)buf, recv_len) != (size_t)recv_len) {
            LOG_ERROR("[HTTP_OTA] Update.write failed: %s", Update.errorString());
            Update.abort();
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
            mgr.ota_in_progress_ = false;
            return ESP_FAIL;
        }
        
        remaining -= recv_len;
        LOG_DEBUG("[HTTP_OTA] Written: %d bytes, remaining: %d", recv_len, remaining);
    }
    
    // Finalize update
    if (Update.end(true)) {
        LOG_INFO("[HTTP_OTA] Update successful! Size: %u bytes", Update.size());
        httpd_resp_sendstr(req, "OTA update successful! Rebooting...");
        delay(1000);
        ESP.restart();
        return ESP_OK;
    } else {
        LOG_ERROR("[HTTP_OTA] Update.end failed: %s", Update.errorString());
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, Update.errorString());
        mgr.ota_in_progress_ = false;
        return ESP_FAIL;
    }
}

esp_err_t OtaManager::root_handler(httpd_req_t *req) {
    httpd_resp_sendstr(req, "ESP-NOW Transmitter - Ready for OTA");
    return ESP_OK;
}

esp_err_t OtaManager::firmware_info_handler(httpd_req_t *req) {
    char json[512];
    
    if (FirmwareMetadata::isValid(FirmwareMetadata::metadata)) {
        // Metadata is valid - return embedded metadata
        snprintf(json, sizeof(json), 
                 "{\"valid\":true,"
                 "\"env\":\"%s\","
                 "\"device\":\"%s\","
                 "\"version\":\"%d.%d.%d\","
                 "\"build_date\":\"%s\"}",
                 FirmwareMetadata::metadata.env_name,
                 FirmwareMetadata::metadata.device_type,
                 FirmwareMetadata::metadata.version_major,
                 FirmwareMetadata::metadata.version_minor,
                 FirmwareMetadata::metadata.version_patch,
                 FirmwareMetadata::metadata.build_date);
    } else {
        // Metadata not valid - return fallback from build flags
        snprintf(json, sizeof(json), 
                 "{\"valid\":false,"
                 "\"version\":\"%d.%d.%d\","
                 "\"build_date\":\"%s %s\"}",
                 FW_VERSION_MAJOR,
                 FW_VERSION_MINOR,
                 FW_VERSION_PATCH,
                 __DATE__, __TIME__);
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

void OtaManager::init_http_server() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_uri_handlers = 8;
    config.max_resp_headers = 8;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;
    
    // Start HTTP server
    if (httpd_start(&http_server_, &config) == ESP_OK) {
        // Register OTA upload handler
        httpd_uri_t ota_upload_uri = {
            .uri = "/ota_upload",
            .method = HTTP_POST,
            .handler = ota_upload_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_, &ota_upload_uri);
        
        // Register firmware info handler
        httpd_uri_t firmware_info_uri = {
            .uri = "/api/firmware_info",
            .method = HTTP_GET,
            .handler = firmware_info_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_, &firmware_info_uri);
        
        // Register root handler
        httpd_uri_t root_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_, &root_uri);
        
        LOG_INFO("[HTTP_SERVER] HTTP server started on port 80");
    } else {
        LOG_ERROR("[HTTP_SERVER] Failed to start HTTP server");
    }
}
