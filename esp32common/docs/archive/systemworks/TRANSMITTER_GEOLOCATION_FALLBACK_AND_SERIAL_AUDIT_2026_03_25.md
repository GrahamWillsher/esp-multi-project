# Transmitter/Receiver Geolocation Fallback and Serial Audit — 2026-03-25

## Scope (Updated)

This report is now explicitly constrained to:

- `ESPnowtransmitter2/espnowtransmitter2`
- `espnowreceiver_2`
- shared common code: `esp32common`

It excludes legacy external codebases.

Serial audit scope is only:

- `Serial.print(...)`
- `Serial.println(...)`
- `Serial.printf(...)`

---

## Executive Summary

1. Active transmitter geolocation/timezone ownership is in `lib/ethernet_utilities/ethernet_utilities.cpp` and service lifecycle is started from `ServiceSupervisor`.
2. Receiver codebase does not appear to own an equivalent external geolocation provider lookup path.
3. The fallback design should be normalized to strict NTP-style sequential provider handling with bounded timeout and explicit success/failure semantics.
4. Direct `Serial.print/println/printf` remains in transmitter, receiver, and common modules.
5. Your observation about repeated `LOG_*` + `MQTT_LOG_*` lines is valid; this should be consolidated behind a routed common helper (bitmask sink selection).
6. **Current re-check (2026-03-26):** geolocation fallback implementation is active in transmitter `ethernet_utilities.cpp` (provider list + bounded timeout + explicit success/failure); no `LOG_E/LOG_W/LOG_I/LOG_D` aliases remain in active transmitter/receiver/common source headers or implementations.

---

## 1. Where the Transmitter Geolocation Code Is

### 1.1 Active ownership chain

- `ESPnowtransmitter2/espnowtransmitter2/src/network/service_supervisor.cpp`
    - `handle_ethernet_connected()` starts:
        - `init_ethernet_utilities()`
        - `start_ethernet_utilities_task()`

- `ESPnowtransmitter2/espnowtransmitter2/lib/ethernet_utilities/ethernet_utilities.cpp`
    - owns timezone/public-IP lookup and timezone configuration
    - handles retry loop and NTP sync orchestration

- `ESPnowtransmitter2/espnowtransmitter2/src/network/time_manager.cpp`
    - consumes synchronized system time
    - does not own provider-lookup lifecycle

### 1.2 Receiver interaction

- No receiver-side equivalent ownership of external geolocation lookup was identified.
- Receiver primarily consumes telemetry/time state from transmitter paths.

### 1.3 Architecture conclusion

The correct active owner for transmitter geolocation/timezone lookup is `lib/ethernet_utilities/ethernet_utilities.cpp`.

### 1.4 Current implementation state (re-verified 2026-03-26)

Geolocation fallback is now implemented in active transmitter code:

- `kGeoLookupPerProviderTimeoutMs = 5000`
- fixed response buffer (`kGeoResponseCapacity = 768`)
- ordered `GeoProvider` list (`ip-api.com` → `worldtimeapi.org` → `timeapi.world`)
- `lookup_timezone_with_fallback(...)` + `configure_timezone_from_location()` bool-return flow

This confirms the report's fallback design intent is reflected in current runtime code, not only proposal text.

---

## 2. Main Problems in the Current Geolocation / Timezone Path

### 2.1 Current `ethernet_utilities` issues

In `lib/ethernet_utilities/ethernet_utilities.cpp`:

- `get_public_ip_and_timezone(...)` accumulates the HTTP body into a `String`.
- It uses a long blocking wait pattern.
- It uses only one effective provider path (`ip-api.com`) in the current implementation.
- It returns `"UTC"` for several failure cases.
- `configure_timezone_from_location()` then rejects `"UTC"` as failure.

This creates an ambiguous contract:

- `UTC` can be a valid real timezone for some deployments.
- `UTC` is also being used as “lookup failed”.

That should be replaced with an explicit success/failure result.

### 2.2 Legacy hardware path status

Legacy external hardware-specific timezone paths are out of active scope and have
been removed from this implementation plan. The active owner is the shared
`ethernet_utilities` path only.

### 2.3 Better fallback model

The NTP code already follows the right philosophy:

- maintain a small ordered provider list,
- try one provider at a time,
- use a bounded per-provider timeout,
- stop on first success,
- fail cleanly if all providers fail.

The timezone/geolocation path should do the same.

---

## 3. Recommended Fallback Design

### 3.1 Design goals

Use one authoritative implementation in `lib/ethernet_utilities/ethernet_utilities.cpp` with:

- **sequential provider fallback** like NTP,
- **bounded per-provider timeout** (5 seconds is consistent with current NTP behavior),
- **fixed-size buffers** instead of uncontrolled `String` body growth,
- **explicit success/failure result** instead of `UTC`/`UTC0` sentinels,
- reuse of the current timezone-name → POSIX mapping logic,
- compatibility with the existing `configure_timezone_from_location()` public API.

### 3.2 Suggested provider order

Recommended order:

1. `ip-api.com` — already used in active code and returns timezone/city/country/IP in one call.
2. `worldtimeapi.org` — available as an alternate provider.
3. `timeapi.world` — available as an alternate provider.

### 3.3 Timing policy

Mirror the NTP approach:

- `5s` per provider total timeout,
- try providers one-by-one,
- stop after the first valid timezone result,
- do not wait a full long timeout on every failed read stage.

---

## 4. Complete Fallback Code Proposal

### Intended placement

Target module:

- `ESPnowtransmitter2/espnowtransmitter2/lib/ethernet_utilities/ethernet_utilities.cpp`

The following complete proposal keeps the existing public API shape and applies bounded NTP-style provider fallback.

```cpp
namespace {

constexpr uint32_t kGeoLookupPerProviderTimeoutMs = 5000;
constexpr uint32_t kGeoLookupIdleTimeoutMs = 1000;
constexpr size_t kGeoResponseCapacity = 768;

struct TimezoneLookupResult {
    char timezone_name[64]{};
    char timezone_abbreviation[16]{};
    char city[48]{};
    char country[48]{};
    char public_ip[32]{};
};

struct GeoProvider {
    const char* host;
    const char* path;
    bool (*parser)(JsonDocument&, TimezoneLookupResult&);
};

static bool read_http_body(WiFiClient& client,
                           char* body,
                           size_t body_capacity,
                           uint32_t total_timeout_ms,
                           uint32_t idle_timeout_ms);

static bool parse_ip_api_response(JsonDocument& doc, TimezoneLookupResult& out) {
    const char* status = doc["status"] | "";
    if (strcmp(status, "success") != 0) {
        return false;
    }

    strlcpy(out.timezone_name, doc["timezone"] | "", sizeof(out.timezone_name));
    strlcpy(out.city, doc["city"] | "", sizeof(out.city));
    strlcpy(out.country, doc["country"] | "", sizeof(out.country));
    strlcpy(out.public_ip, doc["query"] | "", sizeof(out.public_ip));
    out.timezone_abbreviation[0] = '\0';
    return out.timezone_name[0] != '\0';
}

static bool parse_worldtime_response(JsonDocument& doc, TimezoneLookupResult& out) {
    strlcpy(out.timezone_name, doc["timezone"] | "", sizeof(out.timezone_name));
    strlcpy(out.timezone_abbreviation, doc["abbreviation"] | "", sizeof(out.timezone_abbreviation));
    strlcpy(out.public_ip, doc["client_ip"] | "", sizeof(out.public_ip));
    return out.timezone_name[0] != '\0';
}

static bool http_get_json(const GeoProvider& provider,
                          StaticJsonDocument<768>& doc,
                          TimezoneLookupResult& out_result) {
    WiFiClient client;
    if (!client.connect(provider.host, 80)) {
        LOG_WARN("TZ_LOOKUP", "Connect failed: %s", provider.host);
        return false;
    }

    client.print("GET ");
    client.print(provider.path);
    client.println(" HTTP/1.1");
    client.print("Host: ");
    client.println(provider.host);
    client.println("Connection: close");
    client.println();

    char body[kGeoResponseCapacity]{};
    if (!read_http_body(client,
                        body,
                        sizeof(body),
                        kGeoLookupPerProviderTimeoutMs,
                        kGeoLookupIdleTimeoutMs)) {
        client.stop();
        LOG_WARN("TZ_LOOKUP", "No valid body from %s", provider.host);
        return false;
    }
    client.stop();

    DeserializationError error = deserializeJson(doc, body);
    if (error) {
        LOG_WARN("TZ_LOOKUP", "JSON parse failed for %s: %s", provider.host, error.c_str());
        return false;
    }
    return provider.parser(doc, out_result);
}

static bool lookup_timezone_with_fallback(TimezoneLookupResult& out) {
    static const GeoProvider kProviders[] = {
        {"ip-api.com", "/json/?fields=status,timezone,country,city,query", parse_ip_api_response},
        {"worldtimeapi.org", "/api/ip", parse_worldtime_response},
        {"timeapi.world", "/api/ip", parse_worldtime_response},
    };

    for (const auto& provider : kProviders) {
        StaticJsonDocument<768> doc;
        TimezoneLookupResult candidate{};

        LOG_INFO("TZ_LOOKUP", "Trying provider: %s", provider.host);
        if (http_get_json(provider, doc, candidate)) {
            out = candidate;
            LOG_INFO("TZ_LOOKUP", "Provider success: %s -> %s", provider.host, out.timezone_name);
            return true;
        }
        LOG_WARN("TZ_LOOKUP", "Provider failed: %s", provider.host);
    }
    return false;
}

}  // namespace

bool configure_timezone_from_location() {
    LOG_INFO("TZ_CONFIG", "Getting timezone from location with fallback...");

    if (!is_network_connected()) {
        LOG_WARN("TZ_CONFIG", "No network connection for timezone lookup");
        return false;
    }

    TimezoneLookupResult result{};
    if (!lookup_timezone_with_fallback(result)) {
        LOG_ERROR("TZ_CONFIG", "All timezone providers failed");
        MQTT_LOG_ERROR("TZ", "All providers failed");
        return false;
    }

    char posix_tz[64]{};
    char tz_abbrev[16]{};
    if (!map_timezone_name_to_posix(result.timezone_name,
                                    posix_tz,
                                    sizeof(posix_tz),
                                    tz_abbrev,
                                    sizeof(tz_abbrev))) {
        LOG_WARN("TZ_CONFIG", "Unknown timezone mapping: %s", result.timezone_name);
        return false;
    }

    setenv("TZ", posix_tz, 1);
    tzset();

    detected_timezone_name = result.timezone_name;
    detected_timezone_abbreviation = tz_abbrev;
    public_ip_address = result.public_ip;
    last_public_ip_check = millis();

    LOG_INFO("TZ_CONFIG", "Timezone configured: %s -> %s", result.timezone_name, posix_tz);
    return true;
}
```

### Why this fallback variant is better

- Uses **ordered provider fallback** like NTP.
- Uses **5-second per-provider timeout**, matching the project’s current NTP style more closely than the existing long blocking body reads.
- Removes the `UTC`/`UTC0` failure-sentinel ambiguity.
- Avoids unbounded body growth by using a fixed body buffer.
- Keeps the current public API (`configure_timezone_from_location()`) intact.
- Places all ownership in the **active** utility layer.

---

## 5. Recommendations for the Geolocation / Timezone Area

### High-priority

1. **Keep only one owner**
   - Make `lib/ethernet_utilities/ethernet_utilities.cpp` the single authoritative timezone/geolocation implementation.
    - Keep all timezone/geolocation ownership in shared runtime utilities only.

2. **Stop using `UTC` as a failure sentinel**
   - Return `bool` success/failure plus a structured result.
   - This avoids rejecting legitimate UTC deployments.

3. **Adopt NTP-style sequential fallback**
   - A small provider list with short per-provider timeouts is more predictable and easier to debug.

4. **Bound memory use**
   - Avoid body accumulation into long `String` objects for external-service responses.

### Medium-priority

5. **Separate lookup from mapping**
   - Keep external lookup (`timezone name`) separate from POSIX mapping (`timezone name -> TZ string`).
   - This reduces failure ambiguity and improves testability.

6. **Add provider-level observability**
   - Log which provider succeeded, how long it took, and why failures occurred.
   - That will help identify provider outages versus local network issues.

7. **Add cooldown/backoff after repeated failure**
   - If all providers fail several times in a row, extend the retry interval instead of continuously retrying every 30 seconds forever.

### Low-priority

8. **Persist last known good timezone**
   - Store the last good timezone name/POSIX string in NVS.
   - Use it on boot until network lookup succeeds.
   - This would reduce boot-time dependence on external timezone services.

---

## 6. Serial Audit — Summary (Scoped)

Scope for this section:

- included: transmitter + receiver + `esp32common`
- excluded: legacy external codebases
- pattern: active non-comment direct `Serial.print/println/printf`

### 6.1 Current status (2026-03-26)

- Active application call-sites have been migrated to unified logging (`LOG_*` and
    targeted `log_routed(...)` where sink control is required).
- Residual direct `Serial.*` is intentionally limited to logging infrastructure and
    one timer-callback safety path in `espnow_send_utils.cpp`.
- Uncompiled legacy webserver trees were removed from `esp32common`:
    - deleted `esp32common/webserver/*`
    - deleted `esp32common/webserver_common/*`
    - deleted `esp32common/include/esp32common/webserver/*`
    - removed `webserver` entries from `esp32common/library.json` `srcFilter`

---

## 7. File-by-File Serial Inventory (Scoped)

### 7.1 Historical baseline note

The original line-by-line serial inventory in this section has been superseded by
completed migrations and dead-code removal. Current implementation status is tracked
in §9 (Step 2 + Step 5), which is now the authoritative source.

---

## 8. Investigation: Repeated `LOG_*` + `MQTT_LOG_*` Pairs

A full codebase-wide grep (719 user source files, excluding `.pio` build output) has been run.
The investigation reveals a more significant problem than simple code duplication.

---

### 8.1 Complete variant inventory (codebase-wide totals)

#### Local `LOG_*` macros (defined in `esp32common/logging_utilities/logging_config.h`)

| Macro | Total calls | Notes |
| --- | ---: | --- |
| `LOG_INFO` | 958 | highest volume |
| `LOG_ERROR` | 338 | |
| `LOG_WARN` | 289 | |
| `LOG_DEBUG` | 238 | |
| `LOG_TRACE` | 18 | lowest volume |

#### `MQTT_LOG_*` macros (defined in `esp32common/logging_utilities/mqtt_logger.h`)

| Macro | Total calls | Notes |
| --- | ---: | --- |
| `MQTT_LOG_INFO` | 27 | |
| `MQTT_LOG_ERROR` | 23 | |
| `MQTT_LOG_DEBUG` | 21 | |
| `MQTT_LOG_WARNING` | 14 | |
| `MQTT_LOG_NOTICE` | 6 | no direct `LOG_*` equivalent |
| `MQTT_LOG_CRIT` | 3 | no direct `LOG_*` equivalent |
| `MQTT_LOG_ALERT` | 1 | no direct `LOG_*` equivalent |
| `MQTT_LOG_EMERG` | 1 | no direct `LOG_*` equivalent |

---

### 8.2 Critical finding: `logging_config.h` already routes `LOG_*` to MQTT

**Reading `logging_config.h` reveals that `LOG_*` macros are already conditionally
routed to MQTT** when `mqtt_logger.h` is present at build time (`LOG_USE_MQTT = 1`).

Actual routing when `LOG_USE_MQTT = 1`:

| Macro | Serial | MQTT |
| --- | --- | --- |
| `LOG_ERROR(tag, fmt, ...)` | `Serial.printf` always | `MQTT_LOG_ERROR` always |
| `LOG_WARN(tag, fmt, ...)` | via `MqttLogger` fallback only | `MQTT_LOG_WARNING` |
| `LOG_INFO(tag, fmt, ...)` | via `MqttLogger` fallback only | `MQTT_LOG_INFO` |
| `LOG_DEBUG(tag, fmt, ...)` | via `MqttLogger` fallback only | `MQTT_LOG_DEBUG` |
| `LOG_TRACE(tag, fmt, ...)` | via `MqttLogger` fallback only | `MQTT_LOG_DEBUG` (mapped down) |

`MqttLogger::log()` always writes to Serial as a fallback when MQTT is unavailable.
`MQTT_LOG_CRIT` and above additionally always go to Serial regardless of MQTT state.

**Consequence:** Any call site that calls both `LOG_ERROR("TZ", "msg")` and
`MQTT_LOG_ERROR("TZ", "msg")` in sequence is **publishing to MQTT twice**.
The same double-publish applies to all `LOG_*` + `MQTT_LOG_*` pairs.

This is a real functional defect in the current codebase, not just a style issue.
The primary fix is **call-site cleanup**: remove the explicit `MQTT_LOG_*` calls from
files that also use `LOG_*` macros, since the macros already handle routing.

---

### 8.3 Alias inventory — all redundant short-form macros

`mqtt_logger.h` currently defines four additional short-form aliases:

```cpp
#define LOG_E(...) MQTT_LOG_ERROR(__func__, __VA_ARGS__)
#define LOG_W(...) MQTT_LOG_WARNING(__func__, __VA_ARGS__)
#define LOG_I(...) MQTT_LOG_INFO(__func__, __VA_ARGS__)
#define LOG_D(...) MQTT_LOG_DEBUG(__func__, __VA_ARGS__)
```

These must be removed as part of the cleanup. Problems:

1. They use `__func__` as tag — produces raw C++ mangled/anonymous namespace strings
   at call sites inside lambdas or anonymous namespaces, which are not useful tags.
2. They bypass `logging_config.h` entirely — they do NOT respect `current_log_level`.
3. They produce no Serial output when MQTT is available (MQTT-sink only).
4. They create a second set of `LOG_`-prefixed names that conflict visually with the
   tagged `LOG_ERROR/WARN/INFO/DEBUG` macros from `logging_config.h`.

Migration: all call sites using `LOG_E/W/I/D` are replaced with explicit-tagged
`LOG_ERROR/WARN/INFO/DEBUG(tag, ...)` from `logging_config.h`.

---

### 8.4 All files with the dual-sink double-publish pattern

After implementation, the scan was re-run using strict word-boundary patterns:

- local calls: `\bLOG_(ERROR|WARN|INFO|DEBUG|TRACE)\s*\(`
- MQTT calls: `\bMQTT_LOG_\w+\s*\(`

Files currently containing **both** local `LOG_*` calls and explicit `MQTT_LOG_*` calls:

None in application call sites after the `mqtt_task.cpp` cleanup.

`esp32common/logging_utilities/logging_config.h` also contains both families, but this
is intentional infrastructure routing and not a call-site migration target.

`ESPnowtransmitter2/espnowtransmitter2/lib/ethernet_utilities/ethernet_utilities.cpp`
now has **zero** explicit `MQTT_LOG_*` calls after migration.

`ESPnowtransmitter2/espnowtransmitter2/src/network/mqtt_task.cpp`
now uses `log_routed(LogSink::Mqtt, ...)` for intentional MQTT-only startup/telemetry
messages and no longer mixes `LOG_*` with explicit `MQTT_LOG_*` macros.

---

### 8.5 Hardened `log_routed` helper — header

Placement: `esp32common/logging_utilities/log_routed.h`

Primary role: explicit per-call sink control where the compile-time `LOG_USE_MQTT` routing
is not sufficient — e.g. high-frequency debug that must be Local-only, or critical paths
that must always reach both sinks unconditionally.

**Note:** `RoutedLevel` is used (not `LogLevel`) to avoid a name collision with the
existing `LogLevel` enum defined in `logging_config.h`. Numeric values of `RoutedLevel`
are defined to match `MqttLogLevel` for levels 0–7, verified by `static_assert`.

```cpp
// ─────────────────────────────────────────────────────────────────────────────
// log_routed.h — Explicit multi-sink log routing
//
// Use for per-call-site sink control independent of LOG_USE_MQTT.
// Prefer plain LOG_* macros for the common (single-sink) case.
//
// Placement: esp32common/logging_utilities/log_routed.h
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <stdint.h>
#include "logging_config.h"   // LogLevel, current_log_level
#include "mqtt_logger.h"      // MqttLogLevel, MqttLogger

// ── Sink selection bitmask ────────────────────────────────────────────────────

enum class LogSink : uint8_t {
    None  = 0,
    Local = 1u << 0,   ///< Serial output, respects current_log_level
    Mqtt  = 1u << 1,   ///< MQTT via MqttLogger singleton, respects min_level
    Both  = Local | Mqtt
};

constexpr LogSink operator|(LogSink a, LogSink b) noexcept {
    return static_cast<LogSink>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
constexpr LogSink operator&(LogSink a, LogSink b) noexcept {
    return static_cast<LogSink>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
constexpr bool any(LogSink s) noexcept { return static_cast<uint8_t>(s) != 0u; }

// ── Routed level enum ─────────────────────────────────────────────────────────
// Numeric values deliberately match MqttLogLevel (0–7) for compile-time safety.
// Trace (8) is local-only; maps to MQTT_LOG_DEBUG on the MQTT sink.

enum class RoutedLevel : uint8_t {
    Emerg  = 0,  ///< System unusable          MQTT_LOG_EMERG   / Serial [EMERG]
    Alert  = 1,  ///< Immediate action needed  MQTT_LOG_ALERT   / Serial [ALERT]
    Crit   = 2,  ///< Critical condition       MQTT_LOG_CRIT    / Serial [ERROR]
    Error  = 3,  ///< Error condition          MQTT_LOG_ERROR   / Serial [ERROR]
    Warn   = 4,  ///< Warning condition        MQTT_LOG_WARNING / Serial [WARN ]
    Notice = 5,  ///< Normal but significant   MQTT_LOG_NOTICE  / Serial [INFO ]
    Info   = 6,  ///< Informational            MQTT_LOG_INFO    / Serial [INFO ]
    Debug  = 7,  ///< Debug message            MQTT_LOG_DEBUG   / Serial [DEBUG]
    Trace  = 8   ///< Very verbose (local only)                 / Serial [TRACE]
};

// Compile-time guard: RoutedLevel values 0–7 must match MqttLogLevel exactly.
static_assert(static_cast<uint8_t>(RoutedLevel::Emerg) == MQTT_LOG_EMERG,
              "RoutedLevel::Emerg != MQTT_LOG_EMERG");
static_assert(static_cast<uint8_t>(RoutedLevel::Debug) == MQTT_LOG_DEBUG,
              "RoutedLevel::Debug != MQTT_LOG_DEBUG");

/**
 * @brief Route a log message to one or both output sinks with explicit control.
 *
 * Formats the message once into a fixed stack buffer (no heap allocation).
 * Dispatches to the Serial sink (respecting current_log_level runtime filter)
 * and/or MqttLogger singleton (respecting its own min_level filter).
 *
 * Use this where LOG_* macro routing is insufficient:
 *   - LogSink::Local  — high-frequency output that must NOT go to MQTT
 *   - LogSink::Mqtt   — telemetry that must reach MQTT even with Serial suppressed
 *   - LogSink::Both   — critical events that must appear on both unconditionally
 *
 * @param sinks  Target sinks (LogSink bitmask).
 * @param level  Severity (RoutedLevel enum).
 * @param tag    Module/component tag string, e.g. "TZ_CONFIG".
 * @param fmt    printf-style format string.
 * @param ...    Format arguments.
 */
void log_routed(LogSink      sinks,
                RoutedLevel  level,
                const char*  tag,
                const char*  fmt, ...)
    __attribute__((format(printf, 4, 5)));
```

---

### 8.6 Hardened `log_routed` helper — implementation

Placement: `esp32common/logging_utilities/log_routed.cpp`

```cpp
// ─────────────────────────────────────────────────────────────────────────────
// log_routed.cpp
// Placement: esp32common/logging_utilities/log_routed.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "log_routed.h"
#include <stdarg.h>
#include <Arduino.h>

// ── Internal helpers ──────────────────────────────────────────────────────────

static MqttLogLevel to_mqtt_level(RoutedLevel level) noexcept {
    if (level == RoutedLevel::Trace) {
        return MQTT_LOG_DEBUG;  // No MQTT Trace; map down to DEBUG
    }
    // Numeric values match for Emerg(0)…Debug(7) — verified by static_assert
    return static_cast<MqttLogLevel>(static_cast<uint8_t>(level));
}

static LogLevel to_local_level(RoutedLevel level) noexcept {
    switch (level) {
        case RoutedLevel::Emerg:
        case RoutedLevel::Alert:
        case RoutedLevel::Crit:
        case RoutedLevel::Error:   return LOG_ERROR;
        case RoutedLevel::Warn:    return LOG_WARN;
        case RoutedLevel::Notice:
        case RoutedLevel::Info:    return LOG_INFO;
        case RoutedLevel::Debug:   return LOG_DEBUG;
        case RoutedLevel::Trace:   return LOG_TRACE;
    }
    return LOG_ERROR;  // Unreachable; safe default
}

static const char* level_prefix(RoutedLevel level) noexcept {
    switch (level) {
        case RoutedLevel::Emerg:   return "[EMERG]";
        case RoutedLevel::Alert:   return "[ALERT]";
        case RoutedLevel::Crit:
        case RoutedLevel::Error:   return "[ERROR]";
        case RoutedLevel::Warn:    return "[WARN ]";
        case RoutedLevel::Notice:
        case RoutedLevel::Info:    return "[INFO ]";
        case RoutedLevel::Debug:   return "[DEBUG]";
        case RoutedLevel::Trace:   return "[TRACE]";
    }
    return "[?????]";  // Unreachable
}

// ── Public API ────────────────────────────────────────────────────────────────

void log_routed(LogSink      sinks,
                RoutedLevel  level,
                const char*  tag,
                const char*  fmt, ...) {
    // Format once into a fixed stack buffer (matches MqttLogger::log buffer size)
    char msg[256];
    {
        va_list args;
        va_start(args, fmt);
        vsnprintf(msg, sizeof(msg), fmt, args);
        va_end(args);
    }

    // Local (Serial) sink — checked against runtime current_log_level
    if (any(sinks & LogSink::Local)) {
        if (current_log_level >= to_local_level(level)) {
            Serial.printf("%s[%s] %s\n", level_prefix(level), tag, msg);
        }
    }

    // MQTT sink — MqttLogger applies its own min_level filter internally
    if (any(sinks & LogSink::Mqtt)) {
        MqttLogger::instance().log(to_mqtt_level(level), tag, "%s", msg);
    }
}
```

---

### 8.7 Cleanup priority order (by double-publish impact)

1. `mqtt_task.cpp` — only remaining non-infrastructure mixed-sink file (14 local + 4 MQTT)
2. Serial cleanup in active compiled sources (independent of sink routing)
3. Dead-code removal for uncompiled legacy mirrors

---

## 9. Confirmed Implementation Order

Each step lists what is **added** and what **legacy/redundant code is deleted** at
completion. No step leaves dead code behind.

### Implementation status snapshot (2026-03-26)

- ✅ Step 2 complete for all **compiled** active sources: all `Serial.*`, `MQTT_LOG_*` call sites in `espnow_common_utils`, receiver webserver lib, runtime utils, transmitter app sources migrated.
- ✅ Dead-code cleanup complete: removed uncompiled legacy webserver trees from `esp32common` and removed related `srcFilter` entries.
- ✅ Bracket-style log tag normalization complete in active receiver sources (`"[SSE]"`, `"[SUBSCRIPTION]"`, `"[MQTT]"` → `"SSE"`, `"SUBSCRIPTION"`, `"MQTT"`).
- ✅ Step 5 complete (clean builds + residual grep validations complete for active scoped roots)

---

### Step 1 — Add `log_routed` helper; retire `LOG_E/W/I/D` aliases

**Status:** ✅ Completed

**Where:** `esp32common/logging_utilities/`

**Code added:**
- `log_routed.h` — `LogSink` bitmask, `RoutedLevel` enum, `log_routed()` declaration
  (§8.5 full header)
- `log_routed.cpp` — `to_mqtt_level()`, `to_local_level()`, `level_prefix()`,
  `log_routed()` implementation (§8.6 full implementation)
- Register in `esp32common/library.json` so both transmitter and receiver can consume it

**Legacy code deleted at completion:**
- From `mqtt_logger.h`: remove the four auto-tag alias macros entirely:
  ```cpp
  // DELETE these four lines:
  #define LOG_E(...) MQTT_LOG_ERROR(__func__, __VA_ARGS__)
  #define LOG_W(...) MQTT_LOG_WARNING(__func__, __VA_ARGS__)
  #define LOG_I(...) MQTT_LOG_INFO(__func__, __VA_ARGS__)
  #define LOG_D(...) MQTT_LOG_DEBUG(__func__, __VA_ARGS__)
  ```
  Any call site using them is replaced with explicit-tagged `LOG_ERROR/WARN/INFO/DEBUG`.

**Why first:** `log_routed.h` must exist before any call site can reference it.
Removing `LOG_E/W/I/D` at this step prevents new uses appearing during later steps.

---

### Step 2 — Serial cleanup

**What:** Replace direct `Serial.print/println/printf` call sites with `LOG_*` macros.
For files in the double-publish list (§8.4) that will also be cleaned in step 3,
use `LOG_*` only (not `log_routed`) here to keep the diffs separate and reviewable.

**Progress update (current):**
- `esp32common/espnow_common_utils/connection_manager.cpp` fully migrated.
- `esp32common/espnow_common_utils/channel_manager.cpp` fully migrated.
- `espnowreceiver_2/lib/receiver_config/receiver_config_manager.cpp` (46 calls) fully migrated.
- `esp32common/espnow_common_utils/connection_event_processor.cpp` (3 calls) fully migrated.
- `esp32common/espnow_common_utils/espnow_message_router.cpp` (3 calls) fully migrated.
- `esp32common/runtime_common_utils/src/ota_boot_guard.cpp` (9 calls) fully migrated.
- `esp32common/runtime_common_utils/src/setup_health_gate.cpp` (3 calls) fully migrated.
- `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/communication/can/comm_can.cpp` (15 calls) fully migrated.
- `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/inverter/SUNGROW-CAN.cpp` (1 call) fully migrated.
- `ESPnowtransmitter2/espnowtransmitter2/src/datalayer/static_data.cpp` (2 calls) fully migrated.
- `ESPnowtransmitter2/espnowtransmitter2/src/network/mqtt_manager.cpp` (1 call) fully migrated.
- `ESPnowtransmitter2/espnowtransmitter2/src/network/ota_manager.cpp` (1 call) fully migrated.
- `espnowreceiver_2/src/config/wifi_setup.cpp` (15 calls) fully migrated.
- `espnowreceiver_2/src/config/littlefs_init.cpp` (8 calls) fully migrated.
- `espnowreceiver_2/src/espnow/espnow_callbacks.cpp` (2 calls) fully migrated.
- `espnowreceiver_2/src/display/display_led.cpp` (1 call) fully migrated.
- `espnowreceiver_2/lib/webserver/logging.h` migrated to a shared logging shim (`esp32common/logging/logging_config.h`) and all receiver webserver `LOG_*` call-sites updated to tagged form.
- Removed uncompiled legacy webserver trees from `esp32common` (`webserver`, `webserver_common`, and legacy include wrappers) after confirming no active source dependencies.
- `esp32common/espnow_common_utils/espnow_discovery.cpp` (14 `MQTT_LOG_*` calls) migrated to `LOG_*`.
- `esp32common/espnow_common_utils/espnow_peer_manager.cpp` (6 `MQTT_LOG_*` calls) migrated to `LOG_*`.
- `esp32common/espnow_common_utils/espnow_standard_handlers.cpp` (19 `MQTT_LOG_*` calls) migrated to `LOG_*`.
- `esp32common/espnow_common_utils/espnow_send_utils.cpp`: regular task-context `MQTT_LOG_*` calls (4) migrated to `LOG_*`; deferred `handle_deferred_logging()` call migrated to `log_routed(LogSink::Mqtt, RoutedLevel::Info, ...)` to avoid double-Serial-log (timer callback already writes to Serial directly).
- `espnowreceiver_2/lib/webserver/api/api_sse_handlers.cpp` and `espnowreceiver_2/src/mqtt/mqtt_client.cpp` tag-style cleanup complete (`LOG_*` tags normalized from bracket-prefixed literals to canonical plain tags).
- **Intentional Serial remaining:** `espnow_send_utils.cpp` timer callback `unpause_callback()` (lightweight, stack-safe, immediate Serial.println — deferred MQTT path handled by `handle_deferred_logging`); `log_routed.cpp` and `mqtt_logger.cpp` infrastructure.

**Legacy code deleted at completion of each file:**
- All `Serial.print(...)`, `Serial.println(...)`, `Serial.printf(...)` call sites
  (not counting the intentional Serial uses inside `logging_config.h` and
  `mqtt_logger.cpp` — these are infrastructure, not call sites)

---

### Step 3 — Fix double-publish: remove explicit `MQTT_LOG_*` calls from `LOG_*` files

**Status:** ✅ Completed

**What:** Reviewed `src/network/mqtt_task.cpp` call-sites and replaced the remaining
intentional MQTT-only startup/telemetry messages with `log_routed(LogSink::Mqtt, ...)`.
This removes the last application-level mixed macro family without changing sink intent.

For call sites where `log_routed()` adds value (explicit `LogSink::Local` suppression
or explicit `LogSink::Mqtt`-only with no Serial output), migrate to `log_routed()`
rather than reverting to plain `LOG_*`.

**File completed:**
1. `mqtt_task.cpp`

**Legacy code deleted at completion:**
- Removed explicit `MQTT_LOG_NOTICE` / `MQTT_LOG_INFO` call lines from `mqtt_task.cpp`
- Replaced them with explicit `log_routed(LogSink::Mqtt, ...)` calls for the
    intentional MQTT-only cases

---

### Step 4 — Migrate `ethernet_utilities.cpp`: fix double-publish + NTP geolocation fallback

**Status:** ✅ Completed

**What:** Combined pass on `ethernet_utilities.cpp` — highest-density file (99 combined).
Done as one pass to minimise churn and keep the diff reviewable as a single unit.

**Sub-step A — Remove double-publish:**
- Remove all 21 explicit `MQTT_LOG_*` calls that are paired with `LOG_*` calls
- For `MQTT_LOG_NOTICE` calls (4 in this file — no `LOG_*` equivalent): migrate to
  `log_routed(LogSink::Mqtt, RoutedLevel::Notice, tag, ...)`

**Sub-step B — Apply NTP-style geolocation fallback (§4 code proposal):**
- Replace `get_public_ip_and_timezone()` and existing `configure_timezone_from_location()`
  with the `TimezoneLookupResult` struct, `GeoProvider[]` array, fixed-buffer
  `http_get_json()`, `lookup_timezone_with_fallback()`, and updated
  `configure_timezone_from_location()` that returns `bool`
- Preserve the existing `map_timezone_name_to_posix()` mapping logic unchanged

**Legacy code deleted at completion:**
- Old `get_public_ip_and_timezone()` body (String accumulation, single provider,
  UTC sentinel return)
- Old `configure_timezone_from_location()` body (UTC string rejection logic)
- Old `get_timezone_from_location()` if present as separate function
- All 21 removed explicit `MQTT_LOG_*` duplicate call lines

---

### Step 5 — Verify and close

**Status:** ✅ Completed

**What:**
- Build transmitter and receiver clean from scratch (`pio run --target clean` then full build)
- Confirm no remaining `MQTT_LOG_*` calls appear alongside `LOG_*` calls for the same
  message in any source file
- Confirm no `LOG_E / LOG_W / LOG_I / LOG_D` references remain anywhere
- Confirm no legacy external hardware-path dependencies remain in active scope
- Confirm no `Serial.print/println/printf` remain outside of the
  designated infrastructure files

**Completed verification so far:**
- ✅ `receiver_tft` rebuild passes after `espnowreceiver_2/lib/receiver_config/receiver_config_manager.cpp` migration
- ✅ Both builds pass after `esp32common/espnow_common_utils/connection_event_processor.cpp` migration
- ✅ Both builds pass after `esp32common/espnow_common_utils/espnow_message_router.cpp` migration
- ✅ Both builds pass after `esp32common/runtime_common_utils/src/ota_boot_guard.cpp` migration
- ✅ Both builds pass after `esp32common/runtime_common_utils/src/setup_health_gate.cpp` migration
- ✅ Both builds pass after transmitter application batch (`comm_can.cpp`, `SUNGROW-CAN.cpp`, `static_data.cpp`, `mqtt_manager.cpp`, `ota_manager.cpp`) migration
- ✅ Both builds pass after receiver runtime batch (`wifi_setup.cpp`, `littlefs_init.cpp`, `espnow_callbacks.cpp`, `display_led.cpp`) migration
- ✅ Clean transmitter build passes (`pio run -t clean` then `pio run -j 12`)
- ✅ Clean `receiver_tft` build passes (`pio run -e receiver_tft -t clean` then `pio run -e receiver_tft -j 12`)
- ✅ No `LOG_E / LOG_W / LOG_I / LOG_D` alias call-sites remain in active source roots (`ESPnowtransmitter2/espnowtransmitter2/src+lib`, `espnowreceiver_2/src+lib`, `esp32common` excluding docs/build artifacts)
- ✅ Re-check confirms no alias macro definitions remain in active headers (`#define LOG_E/LOG_W/LOG_I/LOG_D` not present in transmitter/receiver/common code)
- ✅ No mixed `LOG_*` + explicit `MQTT_LOG_*` call-site files remain in active roots (only intentional infrastructure router file: `esp32common/logging_utilities/logging_config.h`)
- ✅ No legacy external hardware-path dependencies remain in active scoped roots
- ✅ Uncompiled dead-code webserver mirrors removed from `esp32common`
- ✅ Residual `Serial.*` outside infrastructure is limited to intentional infrastructure/timer-callback safety paths

Alias audit note:

- Historical alias lines still appear in this document as explanatory snippets only.
- They do **not** appear in active `.h/.cpp` code under transmitter/receiver/common roots.

**Verification commands:**
```powershell
# Confirm LOG_E/W/I/D aliases gone from call sites
Select-String -Recurse -Include '*.cpp','*.h' -Pattern '\bLOG_[EWID]\s*\(' `
    src, lib -Path ESPnowtransmitter2\espnowtransmitter2, espnowreceiver_2, esp32common

# Confirm no residual double-publish pairs
Select-String -Recurse -Include '*.cpp' -Pattern 'MQTT_LOG_' `
    src, lib -Path ESPnowtransmitter2\espnowtransmitter2, espnowreceiver_2, esp32common
```

## Final Recommendation

The confirmed implementation order:

| Step | Action | Key deletions |
| --- | --- | --- |
| **1** | Add `log_routed.h/.cpp` and remove aliases | `LOG_E/W/I/D` aliases from `mqtt_logger.h` (**done**) |
| **2** | Serial call-site cleanup in active compiled sources | `Serial.*` direct call-sites migrated to unified logging |
| **3** | Review remaining mixed-sink file (`mqtt_task.cpp`) | Replace explicit `MQTT_LOG_*` with `log_routed(LogSink::Mqtt, ...)` (**done**) |
| **4** | `ethernet_utilities.cpp`: fallback + duplicate removal | Old geolocation helpers and all explicit `MQTT_LOG_*` in file (**done**) |
| **5** | Final verification + dead-code removal | Clean builds; removed `esp32common` uncompiled legacy webserver trees |

Each step produces a working build before the next step starts. No dead or legacy code
persists beyond the step that supersedes it.
