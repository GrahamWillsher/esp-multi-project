#include "ota_manager.h"
#include "../config/logging_config.h"
#include "../test_data/test_data_config.h"
#include <Arduino.h>
#include <Update.h>
#include <firmware_metadata.h>
#include <firmware_version.h>
#include <algorithm>
#include <vector>

OtaManager& OtaManager::instance() {
    static OtaManager instance;
    return instance;
}

esp_err_t OtaManager::ota_upload_handler(httpd_req_t *req) {
    auto& mgr = instance();
    char buf[1024];
    size_t remaining = req->content_len;
    
    LOG_INFO("HTTP_OTA", "Receiving OTA update, size: %d bytes", remaining);
    
    // Stop other tasks to free resources
    mgr.ota_in_progress_ = true;
    
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        LOG_ERROR("HTTP_OTA", "Update.begin failed: %s", Update.errorString());
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
            LOG_ERROR("HTTP_OTA", "Connection error during upload");
            Update.abort();
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Connection error");
            mgr.ota_in_progress_ = false;
            return ESP_FAIL;
        }
        
        // Write firmware chunk
        if (Update.write((uint8_t*)buf, recv_len) != (size_t)recv_len) {
            LOG_ERROR("HTTP_OTA", "Update.write failed: %s", Update.errorString());
            Update.abort();
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
            mgr.ota_in_progress_ = false;
            return ESP_FAIL;
        }
        
        remaining -= recv_len;
        LOG_DEBUG("HTTP_OTA", "Written: %d bytes, remaining: %d", recv_len, remaining);
    }
    
    // Finalize update
    if (Update.end(true)) {
        LOG_INFO("HTTP_OTA", "Update successful! Size: %u bytes", Update.size());
        httpd_resp_sendstr(req, "OTA update successful! Rebooting...");
        delay(1000);
        ESP.restart();
        return ESP_OK;
    } else {
        LOG_ERROR("HTTP_OTA", "Update.end failed: %s", Update.errorString());
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, Update.errorString());
        mgr.ota_in_progress_ = false;
        return ESP_FAIL;
    }
}

esp_err_t OtaManager::root_handler(httpd_req_t *req) {
    httpd_resp_sendstr(req, "ESP-NOW Transmitter - Ready for OTA");
    return ESP_OK;
}

esp_err_t OtaManager::event_logs_handler(httpd_req_t *req) {
    // Query parameters: limit (default 50)
    char buf[128];
    int limit = 50;
    
    // Try to extract limit parameter from query string
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char limit_str[16];
        if (httpd_query_key_value(buf, "limit", limit_str, sizeof(limit_str)) == ESP_OK) {
            limit = atoi(limit_str);
            if (limit <= 0 || limit > 500) limit = 50;  // Clamp to reasonable range
        }
    }
    
    // Build JSON response with event logs
    String json = "{\"success\":true,\"event_count\":0,\"events\":[";
    
    #ifdef CONFIG_BATTERY_EMULATOR_ENABLED
        // Import event system
        extern const EVENTS_STRUCT_TYPE* get_event_pointer(EVENTS_ENUM_TYPE event);
        extern const char* get_event_enum_string(EVENTS_ENUM_TYPE event);
        extern String get_event_message_string(EVENTS_ENUM_TYPE event);
        extern const char* get_event_level_string(EVENTS_ENUM_TYPE event);
        
        // Collect active events
        std::vector<std::pair<EVENTS_ENUM_TYPE, const EVENTS_STRUCT_TYPE*>> active_events;
        
        for (int i = 0; i < EVENT_NOF_EVENTS; i++) {
            const EVENTS_STRUCT_TYPE* event_ptr = get_event_pointer((EVENTS_ENUM_TYPE)i);
            if (event_ptr && event_ptr->occurences > 0) {
                active_events.push_back({(EVENTS_ENUM_TYPE)i, event_ptr});
            }
        }
        
        // Sort by timestamp descending (newest first)
        std::sort(active_events.begin(), active_events.end(),
            [](const auto& a, const auto& b) {
                return a.second->timestamp > b.second->timestamp;
            });
        
        // Limit number of events in response
        int event_count = active_events.size();
        if (event_count > limit) {
            event_count = limit;
        }
        
        // Build JSON with events
        for (int i = 0; i < event_count; i++) {
            if (i > 0) json += ",";
            
            const auto& event_data = active_events[i];
            EVENTS_ENUM_TYPE event_handle = event_data.first;
            const EVENTS_STRUCT_TYPE* event_ptr = event_data.second;
            
            json += "{\"type\":\"";
            json += get_event_enum_string(event_handle);
            json += "\",\"level\":\"";
            json += get_event_level_string(event_handle);
            json += "\",\"timestamp_ms\":";
            json += String(event_ptr->timestamp);
            json += ",\"count\":";
            json += String(event_ptr->occurences);
            json += ",\"message\":\"";
            json += get_event_message_string(event_handle);
            json += "\"}";
        }
        
        // Update event count in JSON
        json = "{\"success\":true,\"event_count\":" + String(event_count) + ",\"events\":[" + 
               json.substring(json.indexOf('[') + 1);
    #else
        json = "{\"success\":false,\"error\":\"Battery emulator not enabled\",\"events\":[";
    #endif
    
    json += "]}";
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json.c_str(), json.length());
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

esp_err_t OtaManager::test_data_config_get_handler(httpd_req_t *req) {
    char json_buffer[1024];
    
    if (TestDataConfig::get_config_json(json_buffer, sizeof(json_buffer))) {
        // Build success response
        String response = "{\"success\":true,\"config\":";
        response += json_buffer;
        response += "}";
        
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, response.c_str());
        return ESP_OK;
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, 
                           "Failed to generate configuration JSON");
        return ESP_FAIL;
    }
}

esp_err_t OtaManager::test_data_config_post_handler(httpd_req_t *req) {
    char content[1024];
    int ret = httpd_req_recv(req, content, std::min((int)req->content_len, (int)sizeof(content) - 1));
    
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read request body");
        return ESP_FAIL;
    }
    
    content[ret] = '\0';  // Null-terminate
    
    // Parse and apply configuration (persist=true)
    if (TestDataConfig::set_config_from_json(content, true)) {
        String response = "{\"success\":true,\"message\":\"Configuration updated and saved\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, response.c_str());
        return ESP_OK;
    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, 
                           "Invalid configuration or parse error");
        return ESP_FAIL;
    }
}

esp_err_t OtaManager::test_data_apply_handler(httpd_req_t *req) {
    if (TestDataConfig::apply_config()) {
        String response = "{\"success\":true,\"message\":\"Configuration applied\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, response.c_str());
        return ESP_OK;
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, 
                           "Failed to apply configuration");
        return ESP_FAIL;
    }
}

esp_err_t OtaManager::test_data_reset_handler(httpd_req_t *req) {
    if (TestDataConfig::reset_to_defaults(true)) {
        String response = "{\"success\":true,\"message\":\"Configuration reset to defaults\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, response.c_str());
        return ESP_OK;
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, 
                           "Failed to reset configuration");
        return ESP_FAIL;
    }
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
        
        // Register event logs handler
        httpd_uri_t event_logs_uri = {
            .uri = "/api/get_event_logs",
            .method = HTTP_GET,
            .handler = event_logs_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_, &event_logs_uri);
        
        // Register root handler
        httpd_uri_t root_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_, &root_uri);
        
        // Register test data configuration GET handler
        httpd_uri_t test_data_config_get_uri = {
            .uri = "/api/test_data_config",
            .method = HTTP_GET,
            .handler = test_data_config_get_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_, &test_data_config_get_uri);
        
        // Register test data configuration POST handler
        httpd_uri_t test_data_config_post_uri = {
            .uri = "/api/test_data_config",
            .method = HTTP_POST,
            .handler = test_data_config_post_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_, &test_data_config_post_uri);
        
        // Register test data apply handler
        httpd_uri_t test_data_apply_uri = {
            .uri = "/api/test_data_apply",
            .method = HTTP_POST,
            .handler = test_data_apply_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_, &test_data_apply_uri);
        
        // Register test data reset handler
        httpd_uri_t test_data_reset_uri = {
            .uri = "/api/test_data_reset",
            .method = HTTP_POST,
            .handler = test_data_reset_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(http_server_, &test_data_reset_uri);
        
        LOG_INFO("HTTP_SERVER", "HTTP server started on port 80");
    } else {
        LOG_ERROR("HTTP_SERVER", "Failed to start HTTP server");
    }
}
