#ifndef API_SSE_HANDLERS_H
#define API_SSE_HANDLERS_H

#include <stdint.h>
#include <esp_http_server.h>

struct SseRuntimeMetrics {
	uint32_t cell_connects;
	uint32_t cell_disconnects;
	uint32_t cell_send_failures;
	uint32_t cell_ping_failures;
	uint32_t cell_active_clients;
	uint32_t cell_last_session_ms;
	uint32_t cell_max_session_ms;

	uint32_t monitor_connects;
	uint32_t monitor_disconnects;
	uint32_t monitor_send_failures;
	uint32_t monitor_ping_failures;
	uint32_t monitor_active_clients;
	uint32_t monitor_last_session_ms;
	uint32_t monitor_max_session_ms;
};

esp_err_t api_cell_data_sse_handler(httpd_req_t *req);
esp_err_t api_monitor_sse_handler(httpd_req_t *req);
void get_sse_runtime_metrics(SseRuntimeMetrics& out_metrics);

#endif // API_SSE_HANDLERS_H
