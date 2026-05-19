#pragma once

#include <esp_http_server.h>
#include "webserver.h"

// Re-export the full webserver entry points for compatibility with main.cpp.
// The LCD app calls WebserverLcd::init() and WebserverLcd::stop(); internally
// these forward to init_webserver() / stop_webserver() from webserver.h.

namespace WebserverLcd {

/**
 * Check if webserver startup is currently in backoff window.
 * Respects deterministic backoff policy per FSM spec (section 7.1).
 */
inline bool is_webserver_backoff_active() { return ::is_webserver_backoff_active(); }

/**
 * Start the full ESP-IDF httpd server (all pages, API, SSE).
 * Idempotent: safe to call twice.
 */
inline void init()  { init_webserver(); }

/**
 * Stop and free the server.
 */
inline void stop()  { stop_webserver(); }

}  // namespace WebserverLcd
