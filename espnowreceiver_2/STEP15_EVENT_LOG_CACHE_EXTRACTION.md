# Step 15 — Event Log Cache Extraction

**Date:** 2026-03-19  
**Status:** ✅ Complete  
**Addresses:** M1 (TransmitterManager decomposition — continued follow-through)

## Summary

Extracted event-log ownership from `TransmitterManager` into a dedicated `TransmitterEventLogCache` module while preserving `TransmitterManager` public APIs as a compatibility facade.

## Changes

### New Module Created
- **File:** `lib/webserver/utils/transmitter_event_log_cache.h`
- **File:** `lib/webserver/utils/transmitter_event_log_cache.cpp`

**Ownership transferred to TransmitterEventLogCache:**
- Parsed event-log vector (bounded to 200 entries)
- Known/availability flag
- Last-update timestamp
- SSE notify-on-update behavior

### TransmitterManager facade behavior

`TransmitterManager` still exposes:
- `storeEventLogs(...)`
- `hasEventLogs()`
- `getEventLogsSnapshot(...)`
- `getEventLogCount()`
- `getEventLogsLastUpdateMs()`

Internally these now delegate to `TransmitterEventLogCache`.

For `getEventLogsSnapshot(...)`, `TransmitterManager` performs a shallow field copy from cache entries to preserve the existing `TransmitterManager::EventLogEntry` API type.

## Legacy Removed

- Event-log state (`event_logs_`, `event_logs_known_`, `event_logs_last_update_ms_`) from `TransmitterManager`
- Event-log parse/store/mutex logic from `TransmitterManager`

## Build Validation

| Environment | Result |
|---|---|
| `receiver_tft` | ✅ SUCCESS |
| `receiver_lvgl` | ✅ SUCCESS |

## Notes

This keeps all current API handlers and MQTT ingest call sites unchanged while further shrinking `TransmitterManager` ownership scope.
