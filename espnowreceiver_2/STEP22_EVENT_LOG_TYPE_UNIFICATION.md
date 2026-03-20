# Step 22 — Event-log type unification

**Date:** 2026-03-19  
**Status:** ✅ Complete  
**Addresses:** M1 (decomposition follow-through)

## Summary

Extracted the event-log entry type into a shared header and removed conversion glue from `TransmitterManager`.

## Changes

### New shared type header
- `lib/webserver/utils/transmitter_event_log_types.h`

### Type ownership change
- Introduced canonical event-log entry type:
  - `TransmitterEventLogTypes::EventLogEntry`
- Updated both modules to use the shared type:
  - `TransmitterEventLogCache` now aliases `EventLogEntry` to shared type
  - `TransmitterManager` public `EventLogEntry` now aliases the same shared type

### `TransmitterManager` simplification
- `getEventLogsSnapshot(...)` no longer performs per-entry conversion/copy.
- It now directly delegates snapshot fill to `TransmitterEventLogCache::get_event_logs_snapshot(...)`.

## Legacy removed

- Duplicate struct definitions for event-log entries between cache and facade
- Manual conversion loop in `TransmitterManager::getEventLogsSnapshot(...)`

## Build validation

| Environment | Result |
|---|---|
| `receiver_tft` | ✅ SUCCESS |
| `receiver_lvgl` | ✅ SUCCESS |

## Notes

This step reduces facade glue code and keeps event-log data contracts centralized in one shared type definition.
