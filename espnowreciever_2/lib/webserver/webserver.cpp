#include "webserver.h"
#include "page_definitions.h"
#include "utils/transmitter_manager.h"
#include "utils/sse_notifier.h"
#include "pages/pages.h"
#include "api/api_handlers.h"
#include <esp_netif.h>
#include <ESP.h>
#include <esp_now.h>
#include <espnow_common.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <LittleFS.h>

// External references to global variables (backward-compatible aliases)
// These reference the namespaced variables defined in globals.cpp
extern bool& test_mode_enabled;
extern volatile int& g_test_soc;
extern volatile int32_t& g_test_power;
extern volatile uint8_t& g_received_soc;
extern volatile int32_t& g_received_power;

// ═══════════════════════════════════════════════════════════════════════
// NOTE: PAGE_DEFINITIONS moved to page_definitions.h/cpp
// Navigation buttons moved to common/nav_buttons.h/cpp
// Page generator moved to common/page_generator.h/cpp
// Utilities moved to utils/ directory
// ═══════════════════════════════════════════════════════════════════════

// ESP-IDF HTTP Server handle
httpd_handle_t server = NULL;

// OTA firmware storage - using LittleFS file instead of RAM
size_t ota_firmware_size = 0;

// ═══════════════════════════════════════════════════════════════════════
// NOTE: All page handlers moved to pages/ directory
// NOTE: All API handlers moved to api/api_handlers.cpp
// ═══════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════
// INITIALIZATION
// ═══════════════════════════════════════════════════════════════════════

void init_webserver() {
    Serial.println("[WEBSERVER] Initializing ESP-IDF http_server...");
    
    // Check if server already running
    if (server != NULL) {
        Serial.println("[WEBSERVER] Server already running, skipping");
        return;
    }
    
    // Count expected handlers (update this when adding/removing handlers)
    const int EXPECTED_HANDLER_COUNT = 15;  // Current: 10 pages/APIs + 1 reboot + 1 OTA page + 1 OTA API + 1 firmware.bin + 1 wildcard 404
    
    // Initialize SSE notification system
    SSENotifier::init();
    Serial.println("[WEBSERVER] SSE notification system initialized");
    
    // Ensure network stack initialized
    static bool netif_initialized = false;
    if (!netif_initialized) {
        esp_err_t ret = esp_netif_init();
        if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
            Serial.println("[WEBSERVER] Network interface initialized");
            netif_initialized = true;
        } else {
            Serial.printf("[WEBSERVER] ERROR: esp_netif_init failed: %s\n", esp_err_to_name(ret));
            return;
        }
    }
    
    // Verify WiFi is connected
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WEBSERVER] ERROR: WiFi not connected");
        return;
    }
    
    // Configure HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.task_priority = tskIDLE_PRIORITY + 2;
    config.stack_size = 6144;
    config.max_open_sockets = 4;
    config.max_uri_handlers = 15;  // Increased from 10 to accommodate all handlers (currently 12)
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.server_port = 80;
    config.lru_purge_enable = true;
    
    // Verify configuration can handle all handlers
    if (config.max_uri_handlers < EXPECTED_HANDLER_COUNT) {
        Serial.printf("[WEBSERVER] ERROR: max_uri_handlers (%d) is less than expected handlers (%d)!\n", 
                      config.max_uri_handlers, EXPECTED_HANDLER_COUNT);
        Serial.println("[WEBSERVER] Some handlers will fail to register. Increase max_uri_handlers!");
        // Continue anyway to register what we can, but warn user
    }
    
    // Start HTTP server
    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        Serial.printf("[WEBSERVER] ERROR: Failed to start: %s\n", esp_err_to_name(ret));
        return;
    }
    
    Serial.printf("[WEBSERVER] Server started successfully\n");
    Serial.printf("[WEBSERVER] Accessible at: http://%s\n", WiFi.localIP().toString().c_str());
    
    // Register URI handlers (with counter for verification)
    int registered_count = 0;
    
    // Register pages (modular)
    if (register_settings_page(server) == ESP_OK) registered_count++;
    if (register_monitor_page(server) == ESP_OK) registered_count++;
    if (register_monitor2_page(server) == ESP_OK) registered_count++;
    if (register_systeminfo_page(server) == ESP_OK) registered_count++;
    if (register_reboot_page(server) == ESP_OK) registered_count++;
    if (register_ota_page(server) == ESP_OK) registered_count++;
    
    // Register all API handlers (consolidated)
    registered_count += register_all_api_handlers(server);
    
    // Verify all handlers registered successfully
    Serial.printf("[WEBSERVER] Handlers registered: %d/%d\n", registered_count, EXPECTED_HANDLER_COUNT);
    if (registered_count < EXPECTED_HANDLER_COUNT) {
        Serial.printf("[WEBSERVER] WARNING: Only %d of %d handlers registered! Increase max_uri_handlers!\n",
                      registered_count, EXPECTED_HANDLER_COUNT);
    } else {
        Serial.println("[WEBSERVER] All handlers registered successfully");
    }   
}

void stop_webserver() {
    if (server != NULL) {
        httpd_stop(server);
        server = NULL;
        Serial.println("[WEBSERVER] Server stopped");
    }
}

// ═══════════════════════════════════════════════════════════════════════
// PUBLIC API FUNCTIONS (delegated to utility classes)
// ═══════════════════════════════════════════════════════════════════════

// Notify SSE clients that battery monitor data has been updated
// Call this from ESP-NOW worker task or test data generator when data changes
void notify_sse_data_updated() {
    SSENotifier::notifyDataUpdated();
}

// Register the transmitter MAC address for sending control messages
void register_transmitter_mac(const uint8_t* mac) {
    TransmitterManager::registerMAC(mac);
}

// Store transmitter IP address data received via ESP-NOW
void store_transmitter_ip_data(const uint8_t* ip, const uint8_t* gateway, const uint8_t* subnet) {
    TransmitterManager::storeIPData(ip, gateway, subnet);
}
