#include "webserver.h"
#include "page_definitions.h"
#include "page_registration_factory.h"
#include "utils/sse_notifier.h"
#include "pages/pages.h"
#include "api/api_handlers.h"
#include "logging.h"
#include <esp_netif.h>
#include <ESP.h>
#include <esp32common/contracts/shared_contracts.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <cerrno>
#include <atomic>

// No test-mode globals in LCD receiver — always live data.

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
    0,
    0,
    0,
    0,
    0,
    0,
    0
};

volatile bool g_webserver_init_in_progress = false;
uint32_t g_oldest_inflight_request_ms = 0;
uint32_t g_last_request_activity_ms = 0;

constexpr uint32_t kWebserverInflightMaxAgeMs = 120000;
constexpr uint32_t kWebserverInflightIdleMs = 30000;
constexpr uint16_t kHttpCtrlPortBase = 32768;
constexpr uint32_t kBackoffLogIntervalMs = 5000;
constexpr uint32_t kMinHeapForWebserverStartBytes = 32U * 1024U;

std::atomic<bool> g_webserver_stop_in_progress{false};

esp_err_t httpd_not_found_debug_handler(httpd_req_t* req, httpd_err_code_t err) {
    LOG_WARN("WEBSERVER", "HTTP %d not found: method=%d uri=%s heap=%lu ch=%d",
             static_cast<int>(err),
             req ? req->method : -1,
             (req && req->uri) ? req->uri : "<unknown>",
             static_cast<unsigned long>(ESP.getFreeHeap()),
             static_cast<int>(WiFi.channel()));
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Route not found");
}

esp_err_t httpd_method_not_allowed_debug_handler(httpd_req_t* req, httpd_err_code_t err) {
    LOG_WARN("WEBSERVER", "HTTP %d method-not-allowed: method=%d uri=%s heap=%lu ch=%d",
             static_cast<int>(err),
             req ? req->method : -1,
             (req && req->uri) ? req->uri : "<unknown>",
             static_cast<unsigned long>(ESP.getFreeHeap()),
             static_cast<int>(WiFi.channel()));
    return httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, "Method not allowed");
}
}  // namespace anonymous

// ═══════════════════════════════════════════════════════════════════════
// BACKOFF STATE GLOBALS (declared outside namespace for external visibility)
// ═══════════════════════════════════════════════════════════════════════

// Backoff state for FSM-compliant deterministic recovery (section 7.1)
// These are queried by main.cpp watchdog before calling init_webserver()
uint8_t g_http_start_failure_streak = 0;
uint32_t g_next_http_start_allowed_ms = 0;
uint32_t g_last_backoff_log_ms = 0;

// ═══════════════════════════════════════════════════════════════════════
// BACKOFF QUERY API (GLOBAL - outside anonymous namespace)
// ═══════════════════════════════════════════════════════════════════════

bool is_webserver_backoff_active() {
    if (g_next_http_start_allowed_ms == 0) {
        return false;  // Never been in backoff
    }
    const uint32_t now_ms = millis();
    // Account for millis() wraparound with 31-bit safe comparison
    return (now_ms - g_next_http_start_allowed_ms) >= 0x80000000UL;
}

uint32_t webserver_backoff_for_failure_streak(uint8_t streak) {
    // Match the project-wide deterministic backoff profile directionally:
    // 0 ms, 3000 ms, 5000 ms, then progressively slower bounded retries.
    // FSM spec (section 7.1): L1 cool-off gate reset window = 30000 ms
    if (streak == 0) {
        return 0;
    }
    if (streak == 1) {
        return 3000;
    }
    if (streak == 2) {
        return 5000;
    }
    if (streak == 3) {
        return 10000;
    }
    if (streak == 4) {
        return 20000;
    }
    return 30000;
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
    const uint32_t now_ms = millis();

    if (g_webserver_init_in_progress) {
        LOG_WARN("WEBSERVER", "Initialization already in progress; skipping duplicate request");
        return;
    }

    if (g_webserver_stop_in_progress.load()) {
        LOG_WARN("WEBSERVER", "Initialization skipped: stop already in progress");
        return;
    }

    // NOTE: Backoff gating is now handled by main.cpp watchdog (is_webserver_backoff_active())
    // This function is called ONLY when backoff window has expired.
    // FSM spec section 3.6: single-owner recovery coordinator prevents dual-ownership races.

    g_webserver_init_in_progress = true;

    LOG_INFO("WEBSERVER", "Initializing ESP-IDF http_server...");
    g_webserver_metrics.init_attempts++;
    
    // Check if server already running
    if (server != NULL) {
        LOG_INFO("WEBSERVER", "Server already running, skipping");
        g_http_start_failure_streak = 0;
        g_next_http_start_allowed_ms = 0;
        g_webserver_init_in_progress = false;
        return;
    }

    const uint32_t free_heap = ESP.getFreeHeap();
    if (free_heap < kMinHeapForWebserverStartBytes) {
        g_webserver_metrics.init_failures++;
        if (g_http_start_failure_streak < 255) {
            ++g_http_start_failure_streak;
        }
        const uint32_t backoff_ms = webserver_backoff_for_failure_streak(g_http_start_failure_streak);
        g_next_http_start_allowed_ms = now_ms + backoff_ms;
        LOG_ERROR("WEBSERVER",
                  "Initialization blocked: low heap (%lu bytes < %lu bytes minimum) streak=%u backoff=%lu ms",
                  static_cast<unsigned long>(free_heap),
                  static_cast<unsigned long>(kMinHeapForWebserverStartBytes),
                  static_cast<unsigned>(g_http_start_failure_streak),
                  static_cast<unsigned long>(backoff_ms));
        g_webserver_init_in_progress = false;
        return;
    }
    
    // Accept STA connected OR AP mode (LCD receiver supports AP fallback).
    auto wifi_ready = []() {
        return WiFi.status() == WL_CONNECTED
            || WiFi.getMode() == WIFI_MODE_AP
            || WiFi.getMode() == WIFI_MODE_APSTA;
    };
    int wifi_retries = 0;
    while (!wifi_ready() && wifi_retries < 5) {
        LOG_WARN("WEBSERVER", "WiFi not ready yet, retrying... (%d/5)", wifi_retries + 1);
        delay(500);
        wifi_retries++;
    }
    if (!wifi_ready()) {
        g_webserver_metrics.init_failures++;
        LOG_ERROR("WEBSERVER", "WiFi not available after retries - webserver startup aborted");
        g_webserver_init_in_progress = false;
        return;
    }
    LOG_INFO("WEBSERVER", "WiFi ready - proceeding with initialization");
    
    // Compute expected handlers from registries (prevents stale constants).
    const int expected_page_handler_count = PageRegistrationFactory::get_expected_page_handler_count();
    const int expected_api_handler_count = expected_all_api_handlers();
    const int expected_handler_count = expected_page_handler_count + expected_api_handler_count;
    
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
            g_webserver_init_in_progress = false;
            return;
        }
    }
    
    // Configure HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // Lower priority than ESP-NOW/WiFi driver tasks (+5) to prevent HTTP
    // from starving ESP-NOW ACK processing windows.
    config.task_priority = tskIDLE_PRIORITY + 2;
    config.stack_size = 12288;
    // Conservative socket cap: fewer sockets means fewer simultaneous TCP
    // send-buffer allocations competing with ESP-NOW internal heap usage.
    config.max_open_sockets = 6;
    config.max_uri_handlers = 80;  // Increased to accommodate 61 handlers with headroom
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.server_port = 80;
    // Use a rotating control port on each init attempt to avoid rare cases where
    // a stale ctrl socket remains bound after a failed/restarted server lifecycle.
    // Keep HTTP listener fixed at port 80.
    config.ctrl_port = kHttpCtrlPortBase;
    config.recv_wait_timeout = 10;  // Receive timeout for battery data uploads
    // 15 s: enough headroom for TCP socket sends under ESP-NOW radio contention
    // while preventing stalled sockets from holding internal heap indefinitely.
    config.send_wait_timeout = 15;
    config.lru_purge_enable = true;

    g_webserver_metrics.server_port = static_cast<uint16_t>(config.server_port);
    g_webserver_metrics.max_open_sockets = static_cast<uint16_t>(config.max_open_sockets);
    g_webserver_metrics.max_uri_handlers = static_cast<uint16_t>(config.max_uri_handlers);
    g_webserver_metrics.task_stack_size = static_cast<uint16_t>(config.stack_size);
    g_webserver_metrics.task_priority = static_cast<uint8_t>(config.task_priority);
    g_webserver_metrics.recv_wait_timeout_s = static_cast<uint8_t>(config.recv_wait_timeout);
    g_webserver_metrics.send_wait_timeout_s = static_cast<uint8_t>(config.send_wait_timeout);
    g_webserver_metrics.lru_purge_enabled = config.lru_purge_enable;
    g_webserver_metrics.expected_handlers = static_cast<uint16_t>(expected_handler_count);
    
    // Verify configuration can handle all handlers
    if (config.max_uri_handlers < expected_handler_count) {
        LOG_ERROR("WEBSERVER", "max_uri_handlers (%d) is less than expected handlers (%d)!", 
                      config.max_uri_handlers, expected_handler_count);
        LOG_ERROR("WEBSERVER", "Some handlers will fail to register. Increase max_uri_handlers!");
        // Continue anyway to register what we can, but warn user
    }
    
    // Single start attempt per init invocation.
    // Retry pacing is governed by deterministic backoff in this module.
    config.ctrl_port = static_cast<uint16_t>(kHttpCtrlPortBase +
                       (g_webserver_metrics.init_attempts % 128U));
    errno = 0;
    const esp_err_t ret = httpd_start(&server, &config);

    if (ret != ESP_OK) {
        g_webserver_metrics.init_failures++;
        if (g_http_start_failure_streak < 255) {
            ++g_http_start_failure_streak;
        }
        const uint32_t backoff_ms = webserver_backoff_for_failure_streak(g_http_start_failure_streak);
        g_next_http_start_allowed_ms = now_ms + backoff_ms;

        LOG_ERROR("WEBSERVER",
                  "httpd_start failed: %s (http=%u ctrl=%u failure_streak=%u backoff=%lu ms)",
                  esp_err_to_name(ret),
                  static_cast<unsigned>(config.server_port),
                  static_cast<unsigned>(config.ctrl_port),
                  static_cast<unsigned>(g_http_start_failure_streak),
                  static_cast<unsigned long>(backoff_ms));
        // Phase 8: LinkRecoveryCoordinator (ESPNOW stack) removed.
        // Webserver now relies on deterministic local backoff only.
        // Defensive cleanup for partial-start edge cases.
        if (server != NULL) {
            httpd_stop(server);
            server = NULL;
        }
        g_webserver_init_in_progress = false;
        return;
    }

    g_webserver_metrics.running = true;
    g_webserver_metrics.init_successes++;
    g_http_start_failure_streak = 0;
    g_next_http_start_allowed_ms = 0;
    g_last_backoff_log_ms = 0;
    g_webserver_metrics.active_requests = 0;
    g_oldest_inflight_request_ms = 0;
    g_last_request_activity_ms = millis();
    
    LOG_INFO("WEBSERVER", "Server started successfully (http=%u ctrl=%u)",
             static_cast<unsigned>(config.server_port),
             static_cast<unsigned>(config.ctrl_port));

    const esp_err_t err404_rc = httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, httpd_not_found_debug_handler);
    if (err404_rc != ESP_OK) {
        LOG_WARN("WEBSERVER", "Failed to register 404 error handler rc=%d (%s)",
                 static_cast<int>(err404_rc),
                 esp_err_to_name(err404_rc));
    }
    const esp_err_t err405_rc = httpd_register_err_handler(server, HTTPD_405_METHOD_NOT_ALLOWED, httpd_method_not_allowed_debug_handler);
    if (err405_rc != ESP_OK) {
        LOG_WARN("WEBSERVER", "Failed to register 405 error handler rc=%d (%s)",
                 static_cast<int>(err405_rc),
                 esp_err_to_name(err405_rc));
    }
    
    // Register all page handlers via factory (consolidated registration)
    int registered_count = PageRegistrationFactory::register_all_pages(server);
    
    // Register all API handlers (consolidated)
    int api_count = register_all_api_handlers(server);
    registered_count += api_count;
    LOG_DEBUG("WEBSERVER", "API handlers registered: %d", api_count);
    
    // Verify all handlers registered successfully
    LOG_INFO("WEBSERVER", "Handlers registered: %d/%d", registered_count, expected_handler_count);
    if (registered_count < expected_handler_count) {
        LOG_WARN("WEBSERVER", "Only %d of %d handlers registered! Increase max_uri_handlers!",
                      registered_count, expected_handler_count);
    } else {
        LOG_INFO("WEBSERVER", "All %d handlers registered successfully", registered_count);
    }

    g_webserver_metrics.registered_handlers = static_cast<uint16_t>(registered_count);
    
    // Log accessible URLs for debugging
    const wifi_mode_t mode = WiFi.getMode();
    if (mode == WIFI_MODE_APSTA) {
        LOG_INFO("WEBSERVER", "Access webserver at: http://%s (STA) or http://%s (AP)",
                 WiFi.localIP().toString().c_str(),
                 WiFi.softAPIP().toString().c_str());
    } else if (mode == WIFI_MODE_AP) {
        LOG_INFO("WEBSERVER", "Access webserver at: http://%s (AP mode)", WiFi.softAPIP().toString().c_str());
    } else {
        LOG_INFO("WEBSERVER", "Access webserver at: http://%s", WiFi.localIP().toString().c_str());
    }
    LOG_DEBUG("WEBSERVER", "Pages available:");
    for (int i = 0; i < PAGE_COUNT; i++) {
        LOG_DEBUG("WEBSERVER", "  - %s (%s)", PAGE_DEFINITIONS[i].uri, PAGE_DEFINITIONS[i].name);
    }

    LOG_INFO("WEBSERVER",
             "HTTP config: sockets=%u handlers=%u/%u prio=%u stack=%u recv_to=%us send_to=%us lru=%s",
             static_cast<unsigned>(g_webserver_metrics.max_open_sockets),
             static_cast<unsigned>(g_webserver_metrics.registered_handlers),
             static_cast<unsigned>(g_webserver_metrics.expected_handlers),
             static_cast<unsigned>(g_webserver_metrics.task_priority),
             static_cast<unsigned>(g_webserver_metrics.task_stack_size),
             static_cast<unsigned>(g_webserver_metrics.recv_wait_timeout_s),
             static_cast<unsigned>(g_webserver_metrics.send_wait_timeout_s),
             g_webserver_metrics.lru_purge_enabled ? "on" : "off");

    g_webserver_init_in_progress = false;
}

void stop_webserver() {
    bool expected = false;
    if (!g_webserver_stop_in_progress.compare_exchange_strong(expected, true)) {
        LOG_WARN("WEBSERVER", "stop_webserver ignored: stop already in progress");
        return;
    }

    if (server != NULL) {
        const esp_err_t rc = httpd_stop(server);
        if (rc != ESP_OK) {
            LOG_WARN("WEBSERVER", "httpd_stop returned: %s", esp_err_to_name(rc));
        }
        server = NULL;
        g_webserver_metrics.running = false;
        g_webserver_metrics.active_requests = 0;
        g_oldest_inflight_request_ms = 0;
        g_last_request_activity_ms = millis();
        LOG_INFO("WEBSERVER", "Server stopped");
    } else {
        LOG_INFO("WEBSERVER", "stop_webserver: server already stopped");
    }

    g_webserver_stop_in_progress.store(false);
}

void get_webserver_runtime_metrics(WebserverRuntimeMetrics& out_metrics) {
    out_metrics = g_webserver_metrics;
    out_metrics.running = (server != NULL);
}

void webserver_on_request_start(uint32_t now_ms) {
    if (g_webserver_metrics.active_requests == 0) {
        g_oldest_inflight_request_ms = now_ms;
    }
    g_webserver_metrics.active_requests++;
    g_last_request_activity_ms = now_ms;
}

void webserver_on_request_progress(uint32_t now_ms) {
    g_last_request_activity_ms = now_ms;
}

void webserver_on_request_end(uint32_t now_ms, uint32_t duration_ms, bool success) {
    g_webserver_metrics.request_total++;
    if (!success) {
        g_webserver_metrics.request_failures++;
    }
    g_webserver_metrics.last_request_complete_ms = now_ms;
    g_webserver_metrics.last_request_duration_ms = duration_ms;
    if (duration_ms > g_webserver_metrics.max_request_duration_ms) {
        g_webserver_metrics.max_request_duration_ms = duration_ms;
    }

    if (g_webserver_metrics.active_requests > 0) {
        g_webserver_metrics.active_requests--;
    }
    g_last_request_activity_ms = now_ms;
    if (g_webserver_metrics.active_requests == 0) {
        g_oldest_inflight_request_ms = 0;
    }
}

bool webserver_should_recycle(uint32_t now_ms, uint32_t& out_oldest_inflight_ms) {
    out_oldest_inflight_ms = 0;

    if (server == NULL || g_webserver_metrics.active_requests == 0 || g_oldest_inflight_request_ms == 0) {
        return false;
    }

    out_oldest_inflight_ms = now_ms - g_oldest_inflight_request_ms;
    const uint32_t inflight_idle_ms = now_ms - g_last_request_activity_ms;
    if (out_oldest_inflight_ms < kWebserverInflightMaxAgeMs ||
        inflight_idle_ms < kWebserverInflightIdleMs) {
        return false;
    }

    g_webserver_metrics.recycle_count++;
    return true;
}

// ═══════════════════════════════════════════════════════════════════════
// PUBLIC API FUNCTIONS (delegated to utility classes)
// ═══════════════════════════════════════════════════════════════════════

// Notify SSE clients that battery monitor data has been updated
// Call this from ESP-NOW worker task or test data generator when data changes
void notify_sse_data_updated() {
    SSENotifier::notifyDataUpdated();
}

