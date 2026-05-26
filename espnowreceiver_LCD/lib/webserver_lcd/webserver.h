#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <esp_http_server.h>

// ESP-IDF HTTP Server handle
extern httpd_handle_t server;

struct WebserverRuntimeMetrics {
    bool running;
    uint16_t server_port;
    uint16_t max_open_sockets;
    uint16_t max_uri_handlers;
    uint16_t registered_handlers;
    uint16_t expected_handlers;
    uint16_t task_stack_size;
    uint8_t task_priority;
    uint8_t recv_wait_timeout_s;
    uint8_t send_wait_timeout_s;
    bool lru_purge_enabled;
    uint32_t init_attempts;
    uint32_t init_successes;
    uint32_t init_failures;
    uint32_t request_total;
    uint32_t request_failures;
    uint32_t last_request_complete_ms;
    uint32_t last_request_duration_ms;
    uint32_t max_request_duration_ms;
    uint32_t active_requests;
    uint32_t recycle_count;
};

/**
 * @brief Check if webserver startup is currently in backoff window
 * Respects deterministic backoff policy per FSM spec (section 7.1)
 * @return true if backoff is active and no init attempt should be made
 */
bool is_webserver_backoff_active();

/**
 * @brief Initialize webserver for Battery Emulator receiver
 * Sets up modular page handlers, API endpoints, and utilities
 * Note: Caller (main watchdog) is responsible for checking is_webserver_backoff_active() first
 * @return void
 */
void init_webserver();

/**
 * @brief Stop webserver and free resources
 * @return void
 */
void stop_webserver();
void get_webserver_runtime_metrics(WebserverRuntimeMetrics& out_metrics);
void webserver_on_request_start(uint32_t now_ms);
void webserver_on_request_progress(uint32_t now_ms);
void webserver_on_request_end(uint32_t now_ms, uint32_t duration_ms, bool success);
bool webserver_should_recycle(uint32_t now_ms, uint32_t& out_oldest_inflight_ms);

/**
 * @brief Notify SSE clients that battery data has been updated
 * Call this function whenever g_received_soc, g_received_power, or test mode changes
 * Uses SSENotifier utility class
 * @return void
 */
void notify_sse_data_updated();


#endif
