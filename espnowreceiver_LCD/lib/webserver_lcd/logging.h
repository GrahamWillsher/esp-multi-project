// ─────────────────────────────────────────────────────────────────────────────
// logging.h — Logging shim for the receiver webserver library.
//
// Previously defined Serial-only LOG_*(fmt,...) macros (no tag parameter).
// Now delegates to the shared esp32common logging infrastructure which routes
// to both Serial and MQTT depending on build configuration.
//
// All log calls in this library use the tagged form:
//   LOG_ERROR(tag, fmt, ...)
//   LOG_WARN(tag,  fmt, ...)
//   LOG_INFO(tag,  fmt, ...)
//   LOG_DEBUG(tag, fmt, ...)
//   LOG_TRACE(tag, fmt, ...)
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

// Webserver hot-path logging must remain local-only to avoid adding MQTT
// publish overhead to HTTP/SSE request handling.
#ifdef LOG_USE_MQTT
#undef LOG_USE_MQTT
#endif
#define LOG_USE_MQTT 0

#include <esp32common/logging/logging_config.h>
