#include "webserver.h"
#include "page_definitions.h"
#include "utils/transmitter_manager.h"
#include "utils/sse_notifier.h"
#include "pages/pages.h"
#include "api/api_handlers.h"
#include "logging.h"
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
    LOG_INFO("[WEBSERVER] Initializing ESP-IDF http_server...");
    
    // Check if server already running
    if (server != NULL) {
        LOG_INFO("[WEBSERVER] Server already running, skipping");
        return;
    }
    
    // Count expected handlers (update this when adding/removing handlers)
    const int EXPECTED_HANDLER_COUNT = 34;  // 10 pages + 24 API handlers (23 specific + 1 catch-all 404)
    
    // Initialize SSE notification system
    SSENotifier::init();
    LOG_INFO("[WEBSERVER] SSE notification system initialized");
    
    // Ensure network stack initialized
    static bool netif_initialized = false;
    if (!netif_initialized) {
        esp_err_t ret = esp_netif_init();
        if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
            LOG_INFO("[WEBSERVER] Network interface initialized");
            netif_initialized = true;
        } else {
            LOG_ERROR("[WEBSERVER] esp_netif_init failed: %s", esp_err_to_name(ret));
            return;
        }
    }
    
    // Verify WiFi is connected
    if (WiFi.status() != WL_CONNECTED) {
        LOG_ERROR("[WEBSERVER] WiFi not connected");
        return;
    }
    
    // Configure HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.task_priority = tskIDLE_PRIORITY + 2;
    config.stack_size = 6144;
    config.max_open_sockets = 4;
    config.max_uri_handlers = 50;  // Phase 3: Increased to 50 for granular settings pages + future expansion
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.server_port = 80;
    config.lru_purge_enable = true;
    
    // Verify configuration can handle all handlers
    if (config.max_uri_handlers < EXPECTED_HANDLER_COUNT) {
        LOG_ERROR("[WEBSERVER] max_uri_handlers (%d) is less than expected handlers (%d)!", 
                      config.max_uri_handlers, EXPECTED_HANDLER_COUNT);
        LOG_ERROR("[WEBSERVER] Some handlers will fail to register. Increase max_uri_handlers!");
        // Continue anyway to register what we can, but warn user
    }
    
    // Start HTTP server
    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        LOG_ERROR("[WEBSERVER] Failed to start: %s", esp_err_to_name(ret));
        return;
    }
    
    LOG_INFO("[WEBSERVER] Server started successfully");
    LOG_INFO("[WEBSERVER] Accessible at: http://%s", WiFi.localIP().toString().c_str());
    
    // Register URI handlers (with counter for verification)
    int registered_count = 0;
    
    // V2: Register new landing and hub pages
    if (register_dashboard_page(server) == ESP_OK) registered_count++;
    if (register_transmitter_hub_page(server) == ESP_OK) registered_count++;
    
    // Register transmitter pages (now with /transmitter prefix)
    if (register_settings_page(server) == ESP_OK) registered_count++;
    if (register_battery_settings_page(server) == ESP_OK) registered_count++;
    if (register_monitor_page(server) == ESP_OK) registered_count++;
    if (register_monitor2_page(server) == ESP_OK) registered_count++;
    if (register_reboot_page(server) == ESP_OK) registered_count++;
    
    // Register receiver pages
    if (register_systeminfo_page(server) == ESP_OK) registered_count++;
    
    // Register system tool pages
    if (register_ota_page(server) == ESP_OK) registered_count++;
    if (register_debug_page(server) == ESP_OK) registered_count++;
    
    // Register all API handlers (consolidated)
    registered_count += register_all_api_handlers(server);
    
    // Verify all handlers registered successfully
    LOG_INFO("[WEBSERVER] Handlers registered: %d/%d", registered_count, EXPECTED_HANDLER_COUNT);
    if (registered_count < EXPECTED_HANDLER_COUNT) {
        LOG_WARN("[WEBSERVER] Only %d of %d handlers registered! Increase max_uri_handlers!",
                      registered_count, EXPECTED_HANDLER_COUNT);
    } else {
        LOG_INFO("[WEBSERVER] All handlers registered successfully");
    }   
}

void stop_webserver() {
    if (server != NULL) {
        httpd_stop(server);
        server = NULL;
        LOG_INFO("[WEBSERVER] Server stopped");
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
