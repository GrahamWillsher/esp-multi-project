#include "api_handlers.h"
#include "../utils/transmitter_manager.h"
#include "../utils/receiver_config_manager.h"
#include "../../receiver_config/receiver_config_manager.h"
#include "../utils/sse_notifier.h"
#include "../page_definitions.h"
#include "../logging.h"
#include "../../src/espnow/espnow_send.h"
#include <Arduino.h>
#include <esp_now.h>
#include <espnow_common.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <firmware_version.h>
#include <firmware_metadata.h>
#include <ArduinoJson.h>

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

// Receiver info API - returns receiver's own cached static data
static esp_err_t api_get_receiver_info_handler(httpd_req_t *req) {
    String json = ReceiverConfigManager::getReceiverInfoJson();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json.c_str(), json.length());
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

// Dashboard data API - returns transmitter and receiver status
static esp_err_t api_dashboard_data_handler(httpd_req_t *req) {
    char json[1024];
    
    // Get transmitter data from cache
    bool tx_connected = TransmitterManager::isTransmitterConnected();
    String tx_ip = TransmitterManager::getIPString();
    bool tx_is_static = TransmitterManager::isStaticIP();
    String tx_mac = TransmitterManager::getMACString();
    String tx_firmware = "Unknown";
    
    if (TransmitterManager::hasMetadata()) {
        uint8_t major, minor, patch;
        TransmitterManager::getMetadataVersion(major, minor, patch);
        char version_str[12];
        snprintf(version_str, sizeof(version_str), "%d.%d.%d", major, minor, patch);
        tx_firmware = String(version_str);
    }
    
    // Build JSON response
    snprintf(json, sizeof(json),
        "{"
        "\"transmitter\":{"
            "\"connected\":%s,"
            "\"ip\":\"%s\","
            "\"is_static\":%s,"
            "\"mac\":\"%s\","
            "\"firmware\":\"%s\""
        "},"
        "\"receiver\":{"
            "\"is_static\":true"
        "}"
        "}",
        tx_connected ? "true" : "false",
        tx_ip.c_str(),
        tx_is_static ? "true" : "false",
        tx_mac.c_str(),
        tx_firmware.c_str()
    );
    
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

// Firmware version API
static esp_err_t api_version_handler(httpd_req_t *req) {
    char json[768];
    
    // Format receiver version
    String receiver_version = formatVersion(FW_VERSION_NUMBER);
    
    // V2: Only use metadata (legacy version info removed)
    String transmitter_version = "Unknown";
    uint32_t transmitter_version_number = 0;
    bool version_compatible = false;
    String transmitter_build_date = "";
    String transmitter_build_time = "";
    bool has_metadata = TransmitterManager::hasMetadata();
    bool metadata_valid = TransmitterManager::isMetadataValid();
    
    if (has_metadata) {
        uint8_t major, minor, patch;
        TransmitterManager::getMetadataVersion(major, minor, patch);
        transmitter_version_number = major * 10000 + minor * 100 + patch;
        transmitter_version = formatVersion(transmitter_version_number);
        version_compatible = isVersionCompatible(transmitter_version_number);
        transmitter_build_date = String(TransmitterManager::getMetadataBuildDate());
        transmitter_build_time = "";  // Metadata has combined date/time
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

// Phase 2: Get battery settings from TransmitterManager cache
static esp_err_t api_get_battery_settings_handler(httpd_req_t *req) {
    char json[512];
    
    // Get battery settings from TransmitterManager
    // Note: These are cached from the last PACKET/SETTINGS message received
    auto settings = TransmitterManager::getBatterySettings();
    
    snprintf(json, sizeof(json),
        "{"
        "\"success\":true,"
        "\"capacity_wh\":%u,"
        "\"max_voltage_mv\":%u,"
        "\"min_voltage_mv\":%u,"
        "\"max_charge_current_a\":%.1f,"
        "\"max_discharge_current_a\":%.1f,"
        "\"soc_high_limit\":%u,"
        "\"soc_low_limit\":%u,"
        "\"cell_count\":%u,"
        "\"chemistry\":%u"
        "}",
        settings.capacity_wh,
        settings.max_voltage_mv,
        settings.min_voltage_mv,
        settings.max_charge_current_a,
        settings.max_discharge_current_a,
        settings.soc_high_limit,
        settings.soc_low_limit,
        settings.cell_count,
        settings.chemistry
    );
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

// Phase 2: Save setting to transmitter via ESP-NOW
static esp_err_t api_save_setting_handler(httpd_req_t *req) {
    Serial.println("\n\n===== API SAVE SETTING CALLED =====");
    Serial.flush();
    
    char json[256];
    char buf[512];
    int ret, remaining = req->content_len;
    
    Serial.printf("Content length: %d\n", remaining);
    LOG_INFO("API: save_setting called, content_len=%d", remaining);
    
    // Read POST body
    if (remaining == 0 || remaining > sizeof(buf) - 1) {
        LOG_ERROR("API: Invalid request size: %d", remaining);
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"Invalid request size\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }
    
    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        LOG_ERROR("API: Failed to read request body");
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"Failed to read request\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }
    buf[ret] = '\0';
    
    LOG_INFO("API: Received JSON: %s", buf);
    
    // Parse JSON
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, buf);
    if (error) {
        LOG_ERROR("API: JSON parse error: %s", error.c_str());
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"JSON parse error: %s\"}", error.c_str());
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }
    
    // Extract parameters
    if (!doc.containsKey("category") || !doc.containsKey("field") || !doc.containsKey("value")) {
        LOG_ERROR("API: Missing required fields in JSON");
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"Missing required fields (category, field, value)\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }
    
    uint8_t category = doc["category"];
    uint8_t field = doc["field"];
    
    LOG_INFO("API: Parsed - category=%d, field=%d", category, field);
    
    // Create settings update message
    settings_update_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = msg_battery_settings_update;
    msg.category = category;
    msg.field_id = field;
    
    // Set value based on type (determine from JSON value type)
    JsonVariant value = doc["value"];
    if (value.is<int>() || value.is<uint32_t>()) {
        msg.value_uint32 = value.as<uint32_t>();
        LOG_INFO("API: Value type=uint32, value=%u", msg.value_uint32);
    } else if (value.is<float>() || value.is<double>()) {
        msg.value_float = value.as<float>();
        LOG_INFO("API: Value type=float, value=%.2f", msg.value_float);
    } else if (value.is<const char*>()) {
        strncpy(msg.value_string, value.as<const char*>(), sizeof(msg.value_string) - 1);
        LOG_INFO("API: Value type=string, value=%s", msg.value_string);
    }
    
    // Calculate checksum (simple XOR of all bytes except checksum field)
    msg.checksum = 0;
    uint8_t* bytes = (uint8_t*)&msg;
    for (size_t i = 0; i < sizeof(msg) - sizeof(msg.checksum); i++) {
        msg.checksum ^= bytes[i];
    }
    
    LOG_INFO("API: Message prepared - type=%d, category=%d, field=%d, checksum=%u, size=%d bytes",
             msg.type, msg.category, msg.field_id, msg.checksum, sizeof(msg));
    
    // Send to transmitter
    if (!TransmitterManager::isMACKnown()) {
        LOG_ERROR("API: Transmitter not connected");
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"Transmitter not connected\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }
    
    LOG_INFO("API: Sending to transmitter MAC: %s", TransmitterManager::getMACString().c_str());
    
    esp_err_t result = esp_now_send(TransmitterManager::getMAC(), (const uint8_t*)&msg, sizeof(msg));
    if (result == ESP_OK) {
        LOG_INFO("API: ✓ ESP-NOW send SUCCESS (category=%d, field=%d)", category, field);
        // Update local cache optimistically so UI reflects saved values
        if (category == SETTINGS_BATTERY) {
            auto emu = TransmitterManager::getBatteryEmulatorSettings();
            switch (field) {
                case BATTERY_DOUBLE_ENABLED: emu.double_battery = msg.value_uint32 ? true : false; break;
                case BATTERY_PACK_MAX_VOLTAGE_DV: emu.pack_max_voltage_dV = msg.value_uint32; break;
                case BATTERY_PACK_MIN_VOLTAGE_DV: emu.pack_min_voltage_dV = msg.value_uint32; break;
                case BATTERY_CELL_MAX_VOLTAGE_MV: emu.cell_max_voltage_mV = msg.value_uint32; break;
                case BATTERY_CELL_MIN_VOLTAGE_MV: emu.cell_min_voltage_mV = msg.value_uint32; break;
                case BATTERY_SOC_ESTIMATED: emu.soc_estimated = msg.value_uint32 ? true : false; break;
                default: break;
            }
            TransmitterManager::storeBatteryEmulatorSettings(emu);
        } else if (category == SETTINGS_POWER) {
            auto power = TransmitterManager::getPowerSettings();
            switch (field) {
                case POWER_CHARGE_W: power.charge_w = msg.value_uint32; break;
                case POWER_DISCHARGE_W: power.discharge_w = msg.value_uint32; break;
                case POWER_MAX_PRECHARGE_MS: power.max_precharge_ms = msg.value_uint32; break;
                case POWER_PRECHARGE_DURATION_MS: power.precharge_duration_ms = msg.value_uint32; break;
                default: break;
            }
            TransmitterManager::storePowerSettings(power);
        } else if (category == SETTINGS_INVERTER) {
            auto inverter = TransmitterManager::getInverterSettings();
            switch (field) {
                case INVERTER_CELLS: inverter.cells = msg.value_uint32; break;
                case INVERTER_MODULES: inverter.modules = msg.value_uint32; break;
                case INVERTER_CELLS_PER_MODULE: inverter.cells_per_module = msg.value_uint32; break;
                case INVERTER_VOLTAGE_LEVEL: inverter.voltage_level = msg.value_uint32; break;
                case INVERTER_CAPACITY_AH: inverter.capacity_ah = msg.value_uint32; break;
                case INVERTER_BATTERY_TYPE: inverter.battery_type = msg.value_uint32; break;
                default: break;
            }
            TransmitterManager::storeInverterSettings(inverter);
        } else if (category == SETTINGS_CAN) {
            auto can = TransmitterManager::getCanSettings();
            switch (field) {
                case CAN_FREQUENCY_KHZ: can.frequency_khz = msg.value_uint32; break;
                case CAN_FD_FREQUENCY_MHZ: can.fd_frequency_mhz = msg.value_uint32; break;
                case CAN_SOFAR_ID: can.sofar_id = msg.value_uint32; break;
                case CAN_PYLON_SEND_INTERVAL_MS: can.pylon_send_interval_ms = msg.value_uint32; break;
                default: break;
            }
            TransmitterManager::storeCanSettings(can);
        } else if (category == SETTINGS_CONTACTOR) {
            auto contactor = TransmitterManager::getContactorSettings();
            switch (field) {
                case CONTACTOR_CONTROL_ENABLED: contactor.control_enabled = msg.value_uint32 ? true : false; break;
                case CONTACTOR_NC_MODE: contactor.nc_contactor = msg.value_uint32 ? true : false; break;
                case CONTACTOR_PWM_FREQUENCY_HZ: contactor.pwm_frequency_hz = msg.value_uint32; break;
                default: break;
            }
            TransmitterManager::storeContactorSettings(contactor);
        }
        snprintf(json, sizeof(json), "{\"success\":true,\"message\":\"Setting sent to transmitter\"}");
    } else {
        LOG_ERROR("API: ✗ ESP-NOW send FAILED: %s (0x%x)", esp_err_to_name(result), result);
        LOG_ERROR("API: Failed details - category=%d, field=%d, msg_size=%d", category, field, sizeof(msg));
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"ESP-NOW send failed: %s\"}", esp_err_to_name(result));
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

// ═══════════════════════════════════════════════════════════════════════
// NETWORK CONFIGURATION API HANDLERS
// ═══════════════════════════════════════════════════════════════════════

static bool parse_ip_string(const char* ip_str, uint8_t out[4]) {
    if (!ip_str || ip_str[0] == '\0') {
        return false;
    }

    int a = 0, b = 0, c = 0, d = 0;
    if (sscanf(ip_str, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
        return false;
    }

    if (a < 0 || a > 255 || b < 0 || b > 255 || c < 0 || c > 255 || d < 0 || d > 255) {
        return false;
    }

    out[0] = static_cast<uint8_t>(a);
    out[1] = static_cast<uint8_t>(b);
    out[2] = static_cast<uint8_t>(c);
    out[3] = static_cast<uint8_t>(d);
    return true;
}

// Get receiver's own WiFi configuration (for network config page)
static esp_err_t api_get_receiver_network_handler(httpd_req_t *req) {
    char json[1024];
    
    // Get WiFi MAC address (different from eFuse MAC)
    String wifi_mac = WiFi.macAddress();
    
    // Get current WiFi connection info
    String ssid = WiFi.SSID();
    int channel = WiFi.channel();
    bool is_ap_mode = (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA);
    
    // Get chip information
    String chip_model = ESP.getChipModel();
    uint8_t chip_revision = ESP.getChipRevision();
    
    // Get receiver network config from NVS
    const char* hostname = ReceiverNetworkConfig::getHostname();
    const char* configured_ssid = ReceiverNetworkConfig::getSSID();
    const char* configured_password = ReceiverNetworkConfig::getPassword();
    bool use_static_ip = ReceiverNetworkConfig::useStaticIP();
    const uint8_t* static_ip = ReceiverNetworkConfig::getStaticIP();
    const uint8_t* gateway = ReceiverNetworkConfig::getGateway();
    const uint8_t* subnet = ReceiverNetworkConfig::getSubnet();
    const uint8_t* dns_primary = ReceiverNetworkConfig::getDNSPrimary();
    const uint8_t* dns_secondary = ReceiverNetworkConfig::getDNSSecondary();
    
    // Build JSON response
    snprintf(json, sizeof(json),
        "{"
        "\"success\":true,"
        "\"is_ap_mode\":%s,"
        "\"wifi_mac\":\"%s\","
        "\"chip_model\":\"%s\","
        "\"chip_revision\":%d,"
        "\"hostname\":\"%s\","
        "\"ssid\":\"%s\","
        "\"password\":\"%s\","
        "\"channel\":%d,"
        "\"use_static_ip\":%s,"
        "\"static_ip\":\"%d.%d.%d.%d\","
        "\"gateway\":\"%d.%d.%d.%d\","
        "\"subnet\":\"%d.%d.%d.%d\","
        "\"dns_primary\":\"%d.%d.%d.%d\","
        "\"dns_secondary\":\"%d.%d.%d.%d\""
        "}",
        is_ap_mode ? "true" : "false",
        wifi_mac.c_str(),
        chip_model.c_str(),
        chip_revision,
        hostname,
        configured_ssid[0] ? configured_ssid : ssid.c_str(),  // Fall back to current if not configured
        configured_password,
        channel,
        use_static_ip ? "true" : "false",
        static_ip[0], static_ip[1], static_ip[2], static_ip[3],
        gateway[0], gateway[1], gateway[2], gateway[3],
        subnet[0], subnet[1], subnet[2], subnet[3],
        dns_primary[0], dns_primary[1], dns_primary[2], dns_primary[3],
        dns_secondary[0], dns_secondary[1], dns_secondary[2], dns_secondary[3]
    );
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

// Save receiver's own WiFi configuration
static esp_err_t api_save_receiver_network_handler(httpd_req_t *req) {
    char json[256];
    char buf[512];
    int ret, remaining = req->content_len;

    LOG_INFO("API: save_receiver_network called, content_len=%d", remaining);

    if (remaining == 0 || remaining > static_cast<int>(sizeof(buf) - 1)) {
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"Invalid request size\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }

    ret = httpd_req_recv(req, buf, (remaining < static_cast<int>(sizeof(buf) - 1)) ? remaining : (sizeof(buf) - 1));
    if (ret <= 0) {
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"Failed to read request body\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }
    buf[ret] = '\0';

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, buf);
    if (error) {
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"JSON parse error\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }

    const char* hostname = doc["hostname"] | "";
    const char* ssid = doc["ssid"] | "";
    const char* password = doc["password"] | "";
    bool use_static_ip = doc["use_static_ip"].as<bool>();

    if (!ssid || ssid[0] == '\0') {
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"SSID is required\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }

    const char* password_to_save = password;
    if (!password || password[0] == '\0') {
        password_to_save = ReceiverNetworkConfig::getPassword();
    }

    uint8_t ip[4] = {0};
    uint8_t gateway[4] = {0};
    uint8_t subnet[4] = {0};
    uint8_t dns_primary[4] = {8, 8, 8, 8};
    uint8_t dns_secondary[4] = {8, 8, 4, 4};

    if (use_static_ip) {
        const char* ip_str = doc["ip"] | "";
        const char* gateway_str = doc["gateway"] | "";
        const char* subnet_str = doc["subnet"] | "";
        const char* dns1_str = doc["dns_primary"] | "";
        const char* dns2_str = doc["dns_secondary"] | "";

        if (!parse_ip_string(ip_str, ip) || !parse_ip_string(gateway_str, gateway) || !parse_ip_string(subnet_str, subnet)) {
            snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"Invalid static IP configuration\"}");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, json, strlen(json));
            return ESP_OK;
        }

        if (!parse_ip_string(dns1_str, dns_primary)) {
            dns_primary[0] = 8; dns_primary[1] = 8; dns_primary[2] = 8; dns_primary[3] = 8;
        }

        if (!parse_ip_string(dns2_str, dns_secondary)) {
            dns_secondary[0] = 8; dns_secondary[1] = 8; dns_secondary[2] = 4; dns_secondary[3] = 4;
        }
    }

    bool saved = ReceiverNetworkConfig::saveConfig(
        hostname,
        ssid,
        password_to_save,
        use_static_ip,
        ip,
        gateway,
        subnet,
        dns_primary,
        dns_secondary
    );

    if (saved) {
        snprintf(json, sizeof(json), "{\"success\":true,\"message\":\"Receiver network config saved\"}");
    } else {
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"Failed to save receiver config\"}");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

// Get current network configuration from TransmitterManager
static esp_err_t api_get_network_config_handler(httpd_req_t *req) {
    char json[1024];  // Increased size for both configs

    if (!TransmitterManager::isIPKnown()) {
        snprintf(json, sizeof(json),
            "{"
            "\"success\":false,"
            "\"message\":\"No network config cached yet\""
            "}"
        );
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }
    
    bool is_static = TransmitterManager::isStaticIP();
    uint32_t version = TransmitterManager::getNetworkConfigVersion();
    
    // Current network configuration
    const uint8_t* current_ip = TransmitterManager::getIP();
    const uint8_t* current_gateway = TransmitterManager::getGateway();
    const uint8_t* current_subnet = TransmitterManager::getSubnet();
    
    // Static configuration from NVS
    const uint8_t* static_ip = TransmitterManager::getStaticIP();
    const uint8_t* static_gateway = TransmitterManager::getStaticGateway();
    const uint8_t* static_subnet = TransmitterManager::getStaticSubnet();
    const uint8_t* static_dns1 = TransmitterManager::getStaticDNSPrimary();
    const uint8_t* static_dns2 = TransmitterManager::getStaticDNSSecondary();
    
    if (current_ip && current_gateway && current_subnet && 
        static_ip && static_gateway && static_subnet && static_dns1 && static_dns2) {
        snprintf(json, sizeof(json),
            "{"
            "\"success\":true,"
            "\"use_static_ip\":%s,"
            "\"current\":{"
                "\"ip\":\"%d.%d.%d.%d\","
                "\"gateway\":\"%d.%d.%d.%d\","
                "\"subnet\":\"%d.%d.%d.%d\""
            "},"
            "\"static_config\":{"
                "\"ip\":\"%d.%d.%d.%d\","
                "\"gateway\":\"%d.%d.%d.%d\","
                "\"subnet\":\"%d.%d.%d.%d\","
                "\"dns_primary\":\"%d.%d.%d.%d\","
                "\"dns_secondary\":\"%d.%d.%d.%d\""
            "},"
            "\"config_version\":%u"
            "}",
            is_static ? "true" : "false",
            current_ip[0], current_ip[1], current_ip[2], current_ip[3],
            current_gateway[0], current_gateway[1], current_gateway[2], current_gateway[3],
            current_subnet[0], current_subnet[1], current_subnet[2], current_subnet[3],
            static_ip[0], static_ip[1], static_ip[2], static_ip[3],
            static_gateway[0], static_gateway[1], static_gateway[2], static_gateway[3],
            static_subnet[0], static_subnet[1], static_subnet[2], static_subnet[3],
            static_dns1[0], static_dns1[1], static_dns1[2], static_dns1[3],
            static_dns2[0], static_dns2[1], static_dns2[2], static_dns2[3],
            version
        );
    } else {
        // No IP data available yet
        snprintf(json, sizeof(json),
            "{"
            "\"success\":false,"
            "\"message\":\"No network data available\""
            "}"
        );
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

// Save network configuration to transmitter via ESP-NOW
static esp_err_t api_save_network_config_handler(httpd_req_t *req) {
    Serial.println("\n===== API SAVE NETWORK CONFIG CALLED =====");
    
    char json[256];
    char buf[512];
    int ret, remaining = req->content_len;
    
    LOG_INFO("API: save_network_config called, content_len=%d", remaining);
    
    // Read POST body
    if (remaining == 0 || remaining > sizeof(buf) - 1) {
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"Invalid request size\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }
    
    ret = httpd_req_recv(req, buf, (remaining < (int)(sizeof(buf) - 1)) ? remaining : (sizeof(buf) - 1));
    if (ret <= 0) {
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"Failed to read request body\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }
    buf[ret] = '\0';
    
    Serial.printf("Received JSON: %s\n", buf);
    LOG_INFO("API: Received network config JSON: %s", buf);
    
    // Parse JSON
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, buf);
    if (error) {
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"JSON parse error\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }
    
    // Check if transmitter MAC is known
    if (!TransmitterManager::isMACKnown()) {
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"Transmitter MAC unknown\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }
    
    // Build ESP-NOW message
    network_config_update_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = msg_network_config_update;
    
    msg.use_static_ip = doc["use_static_ip"].as<bool>() ? 1 : 0;
    
    if (msg.use_static_ip) {
        // Parse IP addresses from strings
        String ip_str = doc["ip"].as<String>();
        String gateway_str = doc["gateway"].as<String>();
        String subnet_str = doc["subnet"].as<String>();
        String dns1_str = doc.containsKey("dns_primary") ? doc["dns_primary"].as<String>() : "8.8.8.8";
        String dns2_str = doc.containsKey("dns_secondary") ? doc["dns_secondary"].as<String>() : "8.8.4.4";
        
        // Parse IP
        sscanf(ip_str.c_str(), "%hhu.%hhu.%hhu.%hhu", 
               &msg.ip[0], &msg.ip[1], &msg.ip[2], &msg.ip[3]);
        
        // Parse gateway
        sscanf(gateway_str.c_str(), "%hhu.%hhu.%hhu.%hhu",
               &msg.gateway[0], &msg.gateway[1], &msg.gateway[2], &msg.gateway[3]);
        
        // Parse subnet
        sscanf(subnet_str.c_str(), "%hhu.%hhu.%hhu.%hhu",
               &msg.subnet[0], &msg.subnet[1], &msg.subnet[2], &msg.subnet[3]);
        
        // Parse DNS
        sscanf(dns1_str.c_str(), "%hhu.%hhu.%hhu.%hhu",
               &msg.dns_primary[0], &msg.dns_primary[1], &msg.dns_primary[2], &msg.dns_primary[3]);
        sscanf(dns2_str.c_str(), "%hhu.%hhu.%hhu.%hhu",
               &msg.dns_secondary[0], &msg.dns_secondary[1], &msg.dns_secondary[2], &msg.dns_secondary[3]);
        
        LOG_INFO("API: Sending static IP config: %d.%d.%d.%d", 
                 msg.ip[0], msg.ip[1], msg.ip[2], msg.ip[3]);
    } else {
        LOG_INFO("API: Sending DHCP mode config");
    }
    
    msg.config_version = 0;  // Transmitter will increment
    msg.checksum = 0;  // TODO: Implement checksum if needed
    
    // Send via ESP-NOW
    esp_err_t result = esp_now_send(TransmitterManager::getMAC(), (const uint8_t*)&msg, sizeof(msg));
    if (result == ESP_OK) {
        LOG_INFO("API: ✓ Network config sent to transmitter");
        snprintf(json, sizeof(json), 
                 "{\"success\":true,\"message\":\"Network config sent - awaiting transmitter response\"}");
    } else {
        LOG_ERROR("API: ✗ ESP-NOW send FAILED: %s", esp_err_to_name(result));
        snprintf(json, sizeof(json), 
                 "{\"success\":false,\"message\":\"ESP-NOW send failed: %s\"}", 
                 esp_err_to_name(result));
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

// ═══════════════════════════════════════════════════════════════════════
// MQTT CONFIGURATION API HANDLERS
// ═══════════════════════════════════════════════════════════════════════

// Get MQTT configuration from transmitter cache
static esp_err_t api_get_mqtt_config_handler(httpd_req_t *req) {
    Serial.println("\n===== API GET MQTT CONFIG CALLED =====");
    char json[512];
    
    if (!TransmitterManager::isMqttConfigKnown()) {
        LOG_INFO("API: MQTT config not cached");
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"MQTT config not cached\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }
    
    const uint8_t* server = TransmitterManager::getMqttServer();
    snprintf(json, sizeof(json),
        "{\"success\":true,"
        "\"enabled\":%s,"
        "\"server\":\"%d.%d.%d.%d\","
        "\"port\":%d,"
        "\"username\":\"%s\","
        "\"password\":\"********\","  // Always mask password
        "\"client_id\":\"%s\","
        "\"connected\":%s}",
        TransmitterManager::isMqttEnabled() ? "true" : "false",
        server[0], server[1], server[2], server[3],
        TransmitterManager::getMqttPort(),
        TransmitterManager::getMqttUsername(),
        TransmitterManager::getMqttClientId(),
        TransmitterManager::isMqttConnected() ? "true" : "false"
    );
    
    LOG_INFO("API: ✓ Returning cached MQTT config (enabled=%d, connected=%d)",
             TransmitterManager::isMqttEnabled(), TransmitterManager::isMqttConnected());
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

// Save MQTT configuration to transmitter via ESP-NOW
static esp_err_t api_save_mqtt_config_handler(httpd_req_t *req) {
    Serial.println("\n===== API SAVE MQTT CONFIG CALLED =====");
    
    char json[256];
    char buf[512];
    int ret, remaining = req->content_len;
    
    LOG_INFO("API: save_mqtt_config called, content_len=%d", remaining);
    
    // Read POST body
    if (remaining > (int)sizeof(buf) - 1) {
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"Request too large\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }
    
    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    Serial.printf("Received JSON: %s\n", buf);
    LOG_INFO("API: Received MQTT config JSON: %s", buf);
    
    // Parse JSON
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, buf);
    if (error) {
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"JSON parse error\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }
    
    // Check if transmitter MAC is known
    if (!TransmitterManager::isMACKnown()) {
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"Transmitter MAC unknown\"}");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }
    
    // Build ESP-NOW message
    mqtt_config_update_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = msg_mqtt_config_update;
    
    msg.enabled = doc["enabled"].as<bool>() ? 1 : 0;
    
    // Parse server IP from string
    String server_str = doc["server"].as<String>();
    sscanf(server_str.c_str(), "%hhu.%hhu.%hhu.%hhu",
           &msg.server[0], &msg.server[1], &msg.server[2], &msg.server[3]);
    
    msg.port = doc["port"].as<uint16_t>();
    
    String username = doc.containsKey("username") ? doc["username"].as<String>() : "";
    String password = doc.containsKey("password") ? doc["password"].as<String>() : "";
    String client_id = doc.containsKey("client_id") ? doc["client_id"].as<String>() : "espnow_transmitter";
    
    strncpy(msg.username, username.c_str(), sizeof(msg.username) - 1);
    strncpy(msg.password, password.c_str(), sizeof(msg.password) - 1);
    strncpy(msg.client_id, client_id.c_str(), sizeof(msg.client_id) - 1);
    
    msg.config_version = 0;  // Transmitter will increment
    msg.checksum = 0;  // TODO: Implement checksum if needed
    
    LOG_INFO("API: Sending MQTT config: %s, %d.%d.%d.%d:%d",
             msg.enabled ? "ENABLED" : "DISABLED",
             msg.server[0], msg.server[1], msg.server[2], msg.server[3],
             msg.port);
    
    // Send via ESP-NOW
    esp_err_t result = esp_now_send(TransmitterManager::getMAC(), (const uint8_t*)&msg, sizeof(msg));
    if (result == ESP_OK) {
        LOG_INFO("API: ✓ MQTT config sent to transmitter");
        snprintf(json, sizeof(json), 
                 "{\"success\":true,\"message\":\"MQTT config sent - awaiting transmitter response\"}");
    } else {
        LOG_ERROR("API: ✗ ESP-NOW send FAILED: %s", esp_err_to_name(result));
        snprintf(json, sizeof(json), 
                 "{\"success\":false,\"message\":\"ESP-NOW send failed: %s\"}", 
                 esp_err_to_name(result));
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

// Get transmitter health data (uptime, time, connection status)
// Returns cached heartbeat data for dashboard display
static esp_err_t api_transmitter_health_handler(httpd_req_t *req) {
    Serial.println("\n===== API GET TRANSMITTER HEALTH CALLED =====");
    char json[512];
    
    // Get cached data from TransmitterManager (updated by heartbeat)
    uint64_t uptime_ms = TransmitterManager::getUptimeMs();
    uint64_t unix_time = TransmitterManager::getUnixTime();
    uint8_t time_source = TransmitterManager::getTimeSource();
    
    // Build JSON response with transmitter status
    snprintf(json, sizeof(json),
        "{\"success\":true,"
        "\"uptime_ms\":%llu,"
        "\"unix_time\":%llu,"
        "\"time_source\":%u,"
        "\"mqtt_connected\":%s,"
        "\"ethernet_connected\":%s}",
        uptime_ms,
        unix_time,
        time_source,
        TransmitterManager::isMqttConnected() ? "true" : "false",
        TransmitterManager::isEthernetConnected() ? "true" : "false"
    );
    
    LOG_INFO("API: ✓ Returning transmitter health (uptime=%llu ms, time=%llu, mqtt=%d)",
             uptime_ms, unix_time, TransmitterManager::isMqttConnected());
    
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
        {.uri = "/api/get_receiver_info", .method = HTTP_GET, .handler = api_get_receiver_info_handler, .user_ctx = NULL},
        {.uri = "/api/dashboard_data", .method = HTTP_GET, .handler = api_dashboard_data_handler, .user_ctx = NULL},
        {.uri = "/api/monitor", .method = HTTP_GET, .handler = api_monitor_handler, .user_ctx = NULL},
        {.uri = "/api/transmitter_ip", .method = HTTP_GET, .handler = api_transmitter_ip_handler, .user_ctx = NULL},
        {.uri = "/api/transmitter_health", .method = HTTP_GET, .handler = api_transmitter_health_handler, .user_ctx = NULL},
        {.uri = "/api/version", .method = HTTP_GET, .handler = api_version_handler, .user_ctx = NULL},
        {.uri = "/api/firmware_info", .method = HTTP_GET, .handler = api_firmware_info_handler, .user_ctx = NULL},
        {.uri = "/api/transmitter_metadata", .method = HTTP_GET, .handler = api_transmitter_metadata_handler, .user_ctx = NULL},
        {.uri = "/api/monitor_sse", .method = HTTP_GET, .handler = api_monitor_sse_handler, .user_ctx = NULL},
        {.uri = "/api/reboot", .method = HTTP_GET, .handler = api_reboot_handler, .user_ctx = NULL},
        {.uri = "/api/setDebugLevel", .method = HTTP_GET, .handler = api_set_debug_level_handler, .user_ctx = NULL},
        {.uri = "/api/get_battery_settings", .method = HTTP_GET, .handler = api_get_battery_settings_handler, .user_ctx = NULL},
        {.uri = "/api/save_setting", .method = HTTP_POST, .handler = api_save_setting_handler, .user_ctx = NULL},
        {.uri = "/api/get_receiver_network", .method = HTTP_GET, .handler = api_get_receiver_network_handler, .user_ctx = NULL},
        {.uri = "/api/save_receiver_network", .method = HTTP_POST, .handler = api_save_receiver_network_handler, .user_ctx = NULL},
        {.uri = "/api/get_network_config", .method = HTTP_GET, .handler = api_get_network_config_handler, .user_ctx = NULL},
        {.uri = "/api/save_network_config", .method = HTTP_POST, .handler = api_save_network_config_handler, .user_ctx = NULL},
        {.uri = "/api/get_mqtt_config", .method = HTTP_GET, .handler = api_get_mqtt_config_handler, .user_ctx = NULL},
        {.uri = "/api/save_mqtt_config", .method = HTTP_POST, .handler = api_save_mqtt_config_handler, .user_ctx = NULL},
        {.uri = "/api/ota_upload", .method = HTTP_POST, .handler = api_ota_upload_handler, .user_ctx = NULL},
        {.uri = "/firmware.bin", .method = HTTP_GET, .handler = firmware_bin_handler, .user_ctx = NULL}
    };
    
    // Register all specific handlers first
    for (int i = 0; i < sizeof(handlers) / sizeof(httpd_uri_t); i++) {
        if (httpd_register_uri_handler(server, &handlers[i]) == ESP_OK) {
            count++;
        }
    }
    
    // Register catch-all handler LAST to avoid catching specific routes
    httpd_uri_t notfound = {.uri = "/*", .method = HTTP_GET, .handler = notfound_handler, .user_ctx = NULL};
    if (httpd_register_uri_handler(server, &notfound) == ESP_OK) {
        count++;
    }
    
    return count;
}
