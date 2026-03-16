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
#include <esp32common/espnow/common.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <LittleFS.h>

// External references to global variables (backward-compatible aliases)
// These reference the namespaced variables defined in globals.cpp
extern bool& test_mode_enabled;
extern volatile int& g_test_soc;
extern volatile int32_t& g_test_power;

// ═══════════════════════════════════════════════════════════════════════
// NOTE: PAGE_DEFINITIONS moved to page_definitions.h/cpp
// Navigation buttons moved to common/nav_buttons.h/cpp
// Page generator moved to common/page_generator.h/cpp
// Utilities moved to utils/ directory
// ═══════════════════════════════════════════════════════════════════════

// ESP-IDF HTTP Server handle
httpd_handle_t server = NULL;

namespace {
WebserverRuntimeMetrics g_webserver_metrics = {
    false,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    false,
    0,
    0,
    0
};
}

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
    LOG_INFO("WEBSERVER", "Initializing ESP-IDF http_server...");
    g_webserver_metrics.init_attempts++;
    
    // Check if server already running
    if (server != NULL) {
        LOG_INFO("WEBSERVER", "Server already running, skipping");
        return;
    }
    
    // Verify WiFi is connected - retry a few times if not yet ready
    int wifi_retries = 0;
    while (WiFi.status() != WL_CONNECTED && wifi_retries < 5) {
        LOG_WARN("WEBSERVER", "WiFi not connected yet, retrying... (%d/5)", wifi_retries + 1);
        delay(500);
        wifi_retries++;
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        g_webserver_metrics.init_failures++;
        LOG_ERROR("WEBSERVER", "WiFi still not connected after retries - webserver startup delayed");
        LOG_INFO("WEBSERVER", "Will try to start webserver when WiFi connects");
        return;
    }
    
    LOG_INFO("WEBSERVER", "WiFi connected - proceeding with initialization");
    
    // Count expected handlers (update this when adding/removing handlers)
    const int EXPECTED_HANDLER_COUNT = 64;  // 18 pages + 46 API handlers (44 specific + 1 firmware + 1 catch-all 404)
    
    // Initialize SSE notification system
    SSENotifier::init();
    LOG_INFO("WEBSERVER", "SSE notification system initialized");
    
    // Ensure network stack initialized
    static bool netif_initialized = false;
    if (!netif_initialized) {
        esp_err_t ret = esp_netif_init();
        if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
            LOG_INFO("WEBSERVER", "Network interface initialized");
            netif_initialized = true;
        } else {
            g_webserver_metrics.init_failures++;
            LOG_ERROR("WEBSERVER", "esp_netif_init failed: %s", esp_err_to_name(ret));
            return;
        }
    }
    
    // Configure HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.task_priority = tskIDLE_PRIORITY + 2;
    config.stack_size = 8192;  // Increased from 6144 for battery emulator data handling
    config.max_open_sockets = 4;
    config.max_uri_handlers = 80;  // Increased to accommodate 61 handlers with headroom
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.server_port = 80;
    config.recv_wait_timeout = 10;  // Receive timeout for battery data uploads
    config.send_wait_timeout = 10;  // Send timeout for large JSON responses
    config.lru_purge_enable = true;

    g_webserver_metrics.server_port = static_cast<uint16_t>(config.server_port);
    g_webserver_metrics.max_open_sockets = static_cast<uint16_t>(config.max_open_sockets);
    g_webserver_metrics.max_uri_handlers = static_cast<uint16_t>(config.max_uri_handlers);
    g_webserver_metrics.task_stack_size = static_cast<uint16_t>(config.stack_size);
    g_webserver_metrics.task_priority = static_cast<uint8_t>(config.task_priority);
    g_webserver_metrics.recv_wait_timeout_s = static_cast<uint8_t>(config.recv_wait_timeout);
    g_webserver_metrics.send_wait_timeout_s = static_cast<uint8_t>(config.send_wait_timeout);
    g_webserver_metrics.lru_purge_enabled = config.lru_purge_enable;
    g_webserver_metrics.expected_handlers = static_cast<uint16_t>(EXPECTED_HANDLER_COUNT);
    
    // Verify configuration can handle all handlers
    if (config.max_uri_handlers < EXPECTED_HANDLER_COUNT) {
        LOG_ERROR("WEBSERVER", "max_uri_handlers (%d) is less than expected handlers (%d)!", 
                      config.max_uri_handlers, EXPECTED_HANDLER_COUNT);
        LOG_ERROR("WEBSERVER", "Some handlers will fail to register. Increase max_uri_handlers!");
        // Continue anyway to register what we can, but warn user
    }
    
    // Start HTTP server
    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        g_webserver_metrics.init_failures++;
        LOG_ERROR("WEBSERVER", "Failed to start: %s", esp_err_to_name(ret));
        return;
    }

    g_webserver_metrics.running = true;
    g_webserver_metrics.init_successes++;
    
    LOG_INFO("WEBSERVER", "Server started successfully");
    LOG_INFO("WEBSERVER", "Accessible at: http://%s", WiFi.localIP().toString().c_str());
    
    // Register URI handlers (with counter for verification)
    int registered_count = 0;
    
    // V2: Register new landing and hub pages
    if (register_dashboard_page(server) == ESP_OK) registered_count++;
    if (register_transmitter_hub_page(server) == ESP_OK) registered_count++;
    
    // Register transmitter pages (now with /transmitter prefix)
    if (register_settings_page(server) == ESP_OK) registered_count++;
    if (register_hardware_config_page(server) == ESP_OK) registered_count++;
    if (register_battery_settings_page(server) == ESP_OK) registered_count++;
    if (register_inverter_settings_page(server) == ESP_OK) registered_count++;
    if (register_monitor_page(server) == ESP_OK) registered_count++;
    if (register_monitor2_page(server) == ESP_OK) registered_count++;
    if (register_reboot_page(server) == ESP_OK) registered_count++;
    
    // Register receiver pages
    if (register_systeminfo_page(server) == ESP_OK) registered_count++;

    if (register_cellmonitor_page(server) == ESP_OK) registered_count++;
    
    // Register battery emulator spec pages (Phase 3)
    if (register_battery_specs_page(server) == ESP_OK) registered_count++;
    if (register_inverter_specs_page(server) == ESP_OK) registered_count++;
    if (register_charger_specs_page(server) == ESP_OK) registered_count++;
    if (register_system_specs_page(server) == ESP_OK) registered_count++;
    
    // Register system tool pages
    if (register_ota_page(server) == ESP_OK) registered_count++;
    if (register_debug_page(server) == ESP_OK) registered_count++;
    if (register_event_logs_page(server) == ESP_OK) registered_count++;
    
    // Register all API handlers (consolidated)
    int api_count = register_all_api_handlers(server);
    registered_count += api_count;
    LOG_DEBUG("WEBSERVER", "API handlers registered: %d", api_count);
    
    // Verify all handlers registered successfully
    LOG_INFO("WEBSERVER", "Handlers registered: %d/%d", registered_count, EXPECTED_HANDLER_COUNT);
    if (registered_count < EXPECTED_HANDLER_COUNT) {
        LOG_WARN("WEBSERVER", "Only %d of %d handlers registered! Increase max_uri_handlers!",
                      registered_count, EXPECTED_HANDLER_COUNT);
    } else {
        LOG_INFO("WEBSERVER", "All %d handlers registered successfully", registered_count);
    }

    g_webserver_metrics.registered_handlers = static_cast<uint16_t>(registered_count);
    
    // Log accessible URLs for debugging
    LOG_INFO("WEBSERVER", "Access webserver at: http://%s", WiFi.localIP().toString().c_str());
    LOG_DEBUG("WEBSERVER", "Pages available:");
    LOG_DEBUG("WEBSERVER", "  - / (Dashboard)");
    LOG_DEBUG("WEBSERVER", "  - /transmitter (Transmitter Hub)");
    LOG_DEBUG("WEBSERVER", "  - /transmitter/config (Settings)");
    LOG_DEBUG("WEBSERVER", "  - /transmitter/hardware (Hardware Config)");
    LOG_DEBUG("WEBSERVER", "  - /transmitter/battery (Battery Settings)");
    LOG_DEBUG("WEBSERVER", "  - /transmitter/monitor (Monitor Page)");
    LOG_DEBUG("WEBSERVER", "  - /receiver/config (Receiver Info)");
    LOG_DEBUG("WEBSERVER", "  - /battery_settings.html (Battery Specs - BE/MQTT)");
    LOG_DEBUG("WEBSERVER", "  - /inverter_settings.html (Inverter Specs - BE/MQTT)");
    LOG_DEBUG("WEBSERVER", "  - /charger_settings.html (Charger Specs - BE/MQTT)");
    LOG_DEBUG("WEBSERVER", "  - /system_settings.html (System Specs - BE/MQTT)");
    LOG_DEBUG("WEBSERVER", "  - /ota (OTA Updates)");
    LOG_DEBUG("WEBSERVER", "  - /debug (Debug Info)");
    LOG_DEBUG("WEBSERVER", "  - /events (Event Logs)");
}

void stop_webserver() {
    if (server != NULL) {
        httpd_stop(server);
        server = NULL;
        g_webserver_metrics.running = false;
        LOG_INFO("WEBSERVER", "Server stopped");
    }
}

void get_webserver_runtime_metrics(WebserverRuntimeMetrics& out_metrics) {
    out_metrics = g_webserver_metrics;
    out_metrics.running = (server != NULL);
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
