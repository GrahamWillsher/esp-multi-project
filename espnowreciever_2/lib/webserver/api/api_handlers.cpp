#include "api_handlers.h"
#include "../utils/transmitter_manager.h"
#include "../utils/sse_notifier.h"
#include "../page_definitions.h"
#include "../logging.h"
#include "../../src/espnow/espnow_send.h"
#include "../../src/config/config_receiver.h"
#include <Arduino.h>
#include <esp_now.h>
#include <espnow_common.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <firmware_version.h>
#include <firmware_metadata.h>

// External references to global variables (backward compatibility aliases from src/globals.cpp)
extern bool& test_mode_enabled;
extern volatile int& g_test_soc;
extern volatile int32_t& g_test_power;
extern volatile uint8_t& g_received_soc;
extern volatile int32_t& g_received_power;

// OTA firmware storage
static const char* OTA_FIRMWARE_FILE = "/firmware.bin";
extern size_t ota_firmware_size;

// ═══════════════════════════════════════════════════════════════════════
// API ENDPOINT HANDLERS
// ═══════════════════════════════════════════════════════════════════════

// System information API
static esp_err_t api_data_handler(httpd_req_t *req) {
    char json[768];
    
    String ssid = WiFi.SSID();
    String ip = WiFi.localIP().toString();
    String mac = WiFi.macAddress();
    int channel = WiFi.channel();
    
    String chipModel = ESP.getChipModel();
    uint8_t chipRevision = ESP.getChipRevision();
    uint64_t efuseMac = ESP.getEfuseMac();
    
    char efuseMacStr[18];
    snprintf(efuseMacStr, sizeof(efuseMacStr), 
             "%02X:%02X:%02X:%02X:%02X:%02X",
             (uint8_t)(efuseMac >> 40), (uint8_t)(efuseMac >> 32),
             (uint8_t)(efuseMac >> 24), (uint8_t)(efuseMac >> 16),
             (uint8_t)(efuseMac >> 8), (uint8_t)(efuseMac));
    
    snprintf(json, sizeof(json), 
             "{\"chipModel\":\"%s\",\"chipRevision\":%d,\"efuseMac\":\"%s\","
             "\"ssid\":\"%s\",\"ip\":\"%s\",\"mac\":\"%s\",\"channel\":%d}",
             chipModel.c_str(), chipRevision, efuseMacStr,
             ssid.c_str(), ip.c_str(), mac.c_str(), channel);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

// Battery monitor data API
static esp_err_t api_monitor_handler(httpd_req_t *req) {
    char json[256];
    const char* mode = test_mode_enabled ? "test" : "real";
    uint8_t soc = test_mode_enabled ? g_test_soc : g_received_soc;
    int32_t power = test_mode_enabled ? g_test_power : g_received_power;
    
    snprintf(json, sizeof(json), 
             "{\"mode\":\"%s\",\"soc\":%d,\"power\":%ld}",
             mode, soc, power);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

// Transmitter IP data API
static esp_err_t api_transmitter_ip_handler(httpd_req_t *req) {
    char json[512];
    
    if (TransmitterManager::isIPKnown()) {
        const uint8_t* ip = TransmitterManager::getIP();
        const uint8_t* gateway = TransmitterManager::getGateway();
        const uint8_t* subnet = TransmitterManager::getSubnet();
        
        snprintf(json, sizeof(json), 
                 "{\"success\":true,\"ip\":\"%d.%d.%d.%d\","
                 "\"gateway\":\"%d.%d.%d.%d\",\"subnet\":\"%d.%d.%d.%d\"}",
                 ip[0], ip[1], ip[2], ip[3],
                 gateway[0], gateway[1], gateway[2], gateway[3],
                 subnet[0], subnet[1], subnet[2], subnet[3]);
    } else {
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"No IP data received yet\"}");
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

// Request transmitter IP data API
static esp_err_t api_request_transmitter_ip_handler(httpd_req_t *req) {
    char json[256];
    
    if (TransmitterManager::isMACKnown()) {
        request_data_t req_msg = { msg_request_data, subtype_settings };
        esp_err_t result = esp_now_send(TransmitterManager::getMAC(), (const uint8_t*)&req_msg, sizeof(req_msg));
        
        if (result == ESP_OK) {
            LOG_INFO("API: Sent REQUEST_DATA (subtype_settings) for IP configuration");
            snprintf(json, sizeof(json), "{\"success\":true,\"message\":\"IP data requested\"}");
        } else {
            LOG_ERROR("API: Failed to send REQUEST_DATA: %s", esp_err_to_name(result));
            snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"%s\"}", esp_err_to_name(result));
        }
    } else {
        LOG_WARN("API: Transmitter MAC unknown, cannot request IP data");
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"Transmitter not connected\"}");
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

// Server-Sent Events for real-time battery monitor
static esp_err_t api_monitor_sse_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");
    
    msg_subtype data_subtype = get_subtype_for_uri("/monitor2");
    
    if (TransmitterManager::isMACKnown()) {
        request_data_t req_msg = { msg_request_data, data_subtype };
        esp_err_t result = esp_now_send(TransmitterManager::getMAC(), (const uint8_t*)&req_msg, sizeof(req_msg));
        if (result == ESP_OK) {
            LOG_DEBUG("SSE: Sent REQUEST_DATA (subtype=%d) to transmitter", data_subtype);
        }
    }
    
    uint8_t last_soc = 255;
    int32_t last_power = INT32_MAX;
    bool last_mode = false;
    
    // Send initial data
    char event_data[512];
    const char* mode = test_mode_enabled ? "test" : "real";
    uint8_t current_soc = test_mode_enabled ? g_test_soc : g_received_soc;
    int32_t current_power = test_mode_enabled ? g_test_power : g_received_power;
    
    snprintf(event_data, sizeof(event_data),
             "data: {\"mode\":\"%s\",\"soc\":%d,\"power\":%ld}\n\n",
             mode, current_soc, current_power);
    
    if (httpd_resp_send_chunk(req, event_data, strlen(event_data)) != ESP_OK) {
        return ESP_FAIL;
    }
    
    last_soc = current_soc;
    last_power = current_power;
    last_mode = test_mode_enabled;
    
    // Event-driven loop (max 5 minutes)
    TickType_t start_time = xTaskGetTickCount();
    const TickType_t max_duration = pdMS_TO_TICKS(300000);
    
    while ((xTaskGetTickCount() - start_time) < max_duration) {
        EventBits_t bits = xEventGroupWaitBits(
            SSENotifier::getEventGroup(),
            (1 << 0),
            pdTRUE,
            pdFALSE,
            pdMS_TO_TICKS(500)
        );
        
        if (bits & (1 << 0)) {
            current_soc = test_mode_enabled ? g_test_soc : g_received_soc;
            current_power = test_mode_enabled ? g_test_power : g_received_power;
            
            if (current_soc != last_soc || current_power != last_power || test_mode_enabled != last_mode) {
                mode = test_mode_enabled ? "test" : "real";
                
                snprintf(event_data, sizeof(event_data),
                         "data: {\"mode\":\"%s\",\"soc\":%d,\"power\":%ld}\n\n",
                         mode, current_soc, current_power);
                
                if (httpd_resp_send_chunk(req, event_data, strlen(event_data)) != ESP_OK) {
                    break;
                }
                
                last_soc = current_soc;
                last_power = current_power;
                last_mode = test_mode_enabled;
            }
        } else {
            const char* ping = ": ping\n\n";
            if (httpd_resp_send_chunk(req, ping, strlen(ping)) != ESP_OK) {
                break;
            }
        }
    }
    
    // Send ABORT_DATA message
    if (TransmitterManager::isMACKnown()) {
        abort_data_t abort_msg = { msg_abort_data, data_subtype };
        esp_now_send(TransmitterManager::getMAC(), (const uint8_t*)&abort_msg, sizeof(abort_msg));
        LOG_DEBUG("SSE: Sent ABORT_DATA (subtype=%d) to transmitter", data_subtype);
    }
    
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// Reboot command API
static esp_err_t api_reboot_handler(httpd_req_t *req) {
    String json = "{";
    
    if (TransmitterManager::isMACKnown()) {
        reboot_t reboot_msg = { msg_reboot };
        esp_err_t result = esp_now_send(TransmitterManager::getMAC(), (const uint8_t*)&reboot_msg, sizeof(reboot_msg));
        if (result == ESP_OK) {
            LOG_INFO("REBOOT: Sent command to transmitter");
            json += "\"success\":true,\"message\":\"Reboot command sent\"";
        } else {
            LOG_ERROR("REBOOT: Failed to send command: %s", esp_err_to_name(result));
            json += "\"success\":false,\"message\":\"" + String(esp_err_to_name(result)) + "\"";
        }
    } else {
        LOG_WARN("REBOOT: Transmitter MAC unknown, cannot send command");
        json += "\"success\":false,\"message\":\"Transmitter MAC unknown\"";
    }
    
    json += "}";
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json.c_str(), json.length());
    return ESP_OK;
}

// OTA firmware upload API (complex handler - streams to LittleFS then pushes to transmitter)
static esp_err_t api_ota_upload_handler(httpd_req_t *req) {
    if (LittleFS.exists(OTA_FIRMWARE_FILE)) {
        LittleFS.remove(OTA_FIRMWARE_FILE);
        LOG_DEBUG("OTA: Removed previous firmware file");
    }
    ota_firmware_size = 0;
    
    size_t remaining = req->content_len;
    LOG_INFO("OTA: Receiving firmware upload, total size: %d bytes", remaining);
    
    File fw_file = LittleFS.open(OTA_FIRMWARE_FILE, "w");
    if (!fw_file) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"success\":false,\"message\":\"Failed to create file\"}");
        return ESP_OK;
    }
    
    size_t total_read = 0;
    char buf[1024];
    bool header_found = false;
    size_t header_offset = 0;
    
    while (remaining > 0) {
        int read_len = httpd_req_recv(req, buf, (remaining < sizeof(buf)) ? remaining : sizeof(buf));
        if (read_len <= 0) {
            if (read_len == HTTPD_SOCK_ERR_TIMEOUT) continue;
            
            fw_file.close();
            LittleFS.remove(OTA_FIRMWARE_FILE);
            httpd_resp_set_type(req, "application/json");
            httpd_resp_sendstr(req, "{\"success\":false,\"message\":\"Upload failed\"}");
            return ESP_FAIL;
        }
        
        if (!header_found) {
            for (int i = 0; i < read_len - 3; i++) {
                if (buf[i] == '\r' && buf[i+1] == '\n' && buf[i+2] == '\r' && buf[i+3] == '\n') {
                    header_offset = i + 4;
                    header_found = true;
                    fw_file.write((uint8_t*)(buf + header_offset), read_len - header_offset);
                    total_read += (read_len - header_offset);
                    break;
                }
            }
        } else {
            fw_file.write((uint8_t*)buf, read_len);
            total_read += read_len;
        }
        
        remaining -= read_len;
        
        if (total_read % 102400 < 1024) {
            LOG_DEBUG("OTA: Progress: %d KB", total_read / 1024);
        }
    }
    
    fw_file.close();
    
    // Clean up multipart boundary trailer
    fw_file = LittleFS.open(OTA_FIRMWARE_FILE, "r");
    size_t file_size = fw_file.size();
    size_t search_start = (file_size > 300) ? (file_size - 300) : 0;
    fw_file.seek(search_start);
    
    char search_buf[300];
    size_t search_len = file_size - search_start;
    if (search_len > sizeof(search_buf)) search_len = sizeof(search_buf);
    
    fw_file.read((uint8_t*)search_buf, search_len);
    fw_file.close();
    
    size_t actual_end = file_size;
    for (int i = search_len - 1; i > 10; i--) {
        if (search_buf[i] == '-' && search_buf[i-1] == '-') {
            actual_end = search_start + i - 2;
            break;
        }
    }
    
    ota_firmware_size = actual_end;
    LOG_INFO("OTA: Final firmware size: %d bytes", ota_firmware_size);
    
    if (!TransmitterManager::isIPKnown()) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"success\":false,\"message\":\"Transmitter IP unknown\"}");
        return ESP_OK;
    }
    
    if (TransmitterManager::isMACKnown()) {
        ota_start_t ota_msg = { msg_ota_start, (uint32_t)ota_firmware_size };
        esp_now_send(TransmitterManager::getMAC(), (const uint8_t*)&ota_msg, sizeof(ota_msg));
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    String transmitter_url = TransmitterManager::getURL() + "/ota_upload";
    fw_file = LittleFS.open(OTA_FIRMWARE_FILE, "r");
    
    HTTPClient http;
    http.begin(transmitter_url);
    http.addHeader("Content-Type", "application/octet-stream");
    http.setTimeout(60000);
    
    int httpCode = http.sendRequest("POST", &fw_file, ota_firmware_size);
    fw_file.close();
    
    if (httpCode == 200) {
        http.end();
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"success\":true,\"message\":\"Firmware pushed to transmitter\"}");
    } else {
        http.end();
        char error_json[128];
        snprintf(error_json, sizeof(error_json), 
                 "{\"success\":false,\"message\":\"HTTP error: %d\"}", httpCode);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, error_json);
    }
    
    return ESP_OK;
}

// Serve firmware binary file
static esp_err_t firmware_bin_handler(httpd_req_t *req) {
    if (!LittleFS.exists(OTA_FIRMWARE_FILE) || ota_firmware_size == 0) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_sendstr(req, "No firmware uploaded");
        return ESP_OK;
    }
    
    File fw_file = LittleFS.open(OTA_FIRMWARE_FILE, "r");
    if (!fw_file) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Failed to read firmware");
        return ESP_OK;
    }
    
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=firmware.bin");
    
    char buf[1024];
    size_t remaining = ota_firmware_size;
    
    while (remaining > 0 && fw_file.available()) {
        size_t to_read = (remaining < sizeof(buf)) ? remaining : sizeof(buf);
        size_t read_len = fw_file.read((uint8_t*)buf, to_read);
        
        if (read_len > 0) {
            if (httpd_resp_send_chunk(req, buf, read_len) != ESP_OK) {
                fw_file.close();
                return ESP_FAIL;
            }
            remaining -= read_len;
        } else {
            break;
        }
    }
    
    fw_file.close();
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// 404 handler
static esp_err_t notfound_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "404 Not Found");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "Endpoint not found");
    return ESP_OK;
}

// Set debug level handler - sends ESP-NOW message to transmitter
static esp_err_t api_set_debug_level_handler(httpd_req_t *req) {
    char buf[64];
    int ret = httpd_req_get_url_query_str(req, buf, sizeof(buf));
    
    if (ret == ESP_OK) {
        char param[32];
        if (httpd_query_key_value(buf, "level", param, sizeof(param)) == ESP_OK) {
            uint8_t level = atoi(param);
            
            if (level > 7) {
                char error_json[128];
                snprintf(error_json, sizeof(error_json), 
                         "{\"success\":false,\"message\":\"Invalid debug level (must be 0-7)\"}");
                httpd_resp_set_type(req, "application/json");
                httpd_resp_sendstr(req, error_json);
                return ESP_OK;
            }
            
            if (send_debug_level_control(level)) {
                char success_json[256];
                const char* level_names[] = {"EMERG", "ALERT", "CRIT", "ERROR", "WARNING", "NOTICE", "INFO", "DEBUG"};
                snprintf(success_json, sizeof(success_json), 
                         "{\"success\":true,\"message\":\"Debug level set to %d (%s)\",\"level\":%d}",
                         level, level_names[level], level);
                httpd_resp_set_type(req, "application/json");
                httpd_resp_sendstr(req, success_json);
            } else {
                char error_json[256];
                snprintf(error_json, sizeof(error_json), 
                         "{\"success\":false,\"message\":\"Failed to send debug control (transmitter not connected?)\"}");
                httpd_resp_set_type(req, "application/json");
                httpd_resp_sendstr(req, error_json);
            }
            return ESP_OK;
        }
    }
    
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":false,\"message\":\"Missing level parameter\"}");
    return ESP_OK;
}

// Configuration version API
static esp_err_t api_config_version_handler(httpd_req_t *req) {
    char json[256];
    auto& configMgr = ReceiverConfigManager::instance();
    
    if (configMgr.isConfigAvailable()) {
        uint32_t global_version = configMgr.getGlobalVersion();
        uint32_t timestamp = configMgr.getTimestamp();
        
        snprintf(json, sizeof(json), 
                 "{\"available\":true,\"global_version\":%u,\"timestamp\":%u}",
                 global_version, timestamp);
    } else {
        snprintf(json, sizeof(json), "{\"available\":false}");
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

// Firmware version API
static esp_err_t api_version_handler(httpd_req_t *req) {
    char json[768];
    
    // Format receiver version
    String receiver_version = formatVersion(FW_VERSION_NUMBER);
    
    // Get transmitter version/metadata if available
    String transmitter_version = "Unknown";
    uint32_t transmitter_version_number = 0;
    bool version_compatible = false;
    String transmitter_build_date = "";
    String transmitter_build_time = "";
    bool has_metadata = TransmitterManager::hasMetadata();
    bool metadata_valid = TransmitterManager::isMetadataValid();
    
    if (has_metadata) {
        // Use metadata if available (more accurate)
        uint8_t major, minor, patch;
        TransmitterManager::getMetadataVersion(major, minor, patch);
        transmitter_version_number = major * 10000 + minor * 100 + patch;
        transmitter_version = formatVersion(transmitter_version_number);
        version_compatible = isVersionCompatible(transmitter_version_number);
        transmitter_build_date = String(TransmitterManager::getMetadataBuildDate());
        transmitter_build_time = "";  // Metadata has combined date/time
    } else if (TransmitterManager::hasVersionInfo()) {
        // Fallback to old version info
        transmitter_version_number = TransmitterManager::getFirmwareVersion();
        transmitter_version = formatVersion(transmitter_version_number);
        version_compatible = isVersionCompatible(transmitter_version_number);
        transmitter_build_date = String(TransmitterManager::getBuildDate());
        transmitter_build_time = String(TransmitterManager::getBuildTime());
    }
    
    snprintf(json, sizeof(json),
             "{"
             "\"device\":\"" DEVICE_NAME "\","
             "\"version\":\"%s\","
             "\"version_number\":%u,"
             "\"build_date\":\"" __DATE__ "\","
             "\"build_time\":\"" __TIME__ "\","
             "\"transmitter_version\":\"%s\","
             "\"transmitter_version_number\":%u,"
             "\"transmitter_build_date\":\"%s\","
             "\"transmitter_build_time\":\"%s\","
             "\"transmitter_compatible\":%s,"
             "\"transmitter_metadata_valid\":%s,"
             "\"uptime\":%lu,"
             "\"heap_free\":%u,"
             "\"wifi_channel\":%d"
             "}",
             receiver_version.c_str(),
             FW_VERSION_NUMBER,
             transmitter_version.c_str(),
             transmitter_version_number,
             transmitter_build_date.c_str(),
             transmitter_build_time.c_str(),
             version_compatible ? "true" : "false",
             metadata_valid ? "true" : "false",
             millis() / 1000,
             ESP.getFreeHeap(),
             WiFi.channel());
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

// Firmware metadata API - returns current running firmware info
static esp_err_t api_firmware_info_handler(httpd_req_t *req) {
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

// Transmitter firmware metadata API - returns transmitter firmware info from ESP-NOW
static esp_err_t api_transmitter_metadata_handler(httpd_req_t *req) {
    char json[512];
    
    if (TransmitterManager::hasMetadata()) {
        uint8_t major, minor, patch;
        TransmitterManager::getMetadataVersion(major, minor, patch);
        bool valid = TransmitterManager::isMetadataValid();
        
        snprintf(json, sizeof(json), 
                 "{\"status\":\"received\","
                 "\"valid\":%s,"
                 "\"env\":\"%s\","
                 "\"device\":\"%s\","
                 "\"version\":\"%d.%d.%d\","
                 "\"build_date\":\"%s\"}",
                 valid ? "true" : "false",
                 TransmitterManager::getMetadataEnv(),
                 TransmitterManager::getMetadataDevice(),
                 major, minor, patch,
                 TransmitterManager::getMetadataBuildDate());
    } else {
        // No metadata received yet
        snprintf(json, sizeof(json), 
                 "{\"status\":\"waiting\","
                 "\"valid\":false,"
                 "\"message\":\"No metadata received from transmitter yet\"}");
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

// ═══════════════════════════════════════════════════════════════════════
// REGISTRATION FUNCTION
// ═══════════════════════════════════════════════════════════════════════

int register_all_api_handlers(httpd_handle_t server) {
    int count = 0;
    
    httpd_uri_t handlers[] = {
        {.uri = "/api/data", .method = HTTP_GET, .handler = api_data_handler, .user_ctx = NULL},
        {.uri = "/api/monitor", .method = HTTP_GET, .handler = api_monitor_handler, .user_ctx = NULL},
        {.uri = "/api/transmitter_ip", .method = HTTP_GET, .handler = api_transmitter_ip_handler, .user_ctx = NULL},
        {.uri = "/api/request_transmitter_ip", .method = HTTP_GET, .handler = api_request_transmitter_ip_handler, .user_ctx = NULL},
        {.uri = "/api/config_version", .method = HTTP_GET, .handler = api_config_version_handler, .user_ctx = NULL},
        {.uri = "/api/version", .method = HTTP_GET, .handler = api_version_handler, .user_ctx = NULL},
        {.uri = "/api/firmware_info", .method = HTTP_GET, .handler = api_firmware_info_handler, .user_ctx = NULL},
        {.uri = "/api/transmitter_metadata", .method = HTTP_GET, .handler = api_transmitter_metadata_handler, .user_ctx = NULL},
        {.uri = "/api/monitor_sse", .method = HTTP_GET, .handler = api_monitor_sse_handler, .user_ctx = NULL},
        {.uri = "/api/reboot", .method = HTTP_GET, .handler = api_reboot_handler, .user_ctx = NULL},
        {.uri = "/api/setDebugLevel", .method = HTTP_GET, .handler = api_set_debug_level_handler, .user_ctx = NULL},
        {.uri = "/api/ota_upload", .method = HTTP_POST, .handler = api_ota_upload_handler, .user_ctx = NULL},
        {.uri = "/firmware.bin", .method = HTTP_GET, .handler = firmware_bin_handler, .user_ctx = NULL},
        {.uri = "/*", .method = HTTP_GET, .handler = notfound_handler, .user_ctx = NULL}
    };
    
    for (int i = 0; i < sizeof(handlers) / sizeof(httpd_uri_t); i++) {
        if (httpd_register_uri_handler(server, &handlers[i]) == ESP_OK) {
            count++;
        }
    }
    
    return count;
}
