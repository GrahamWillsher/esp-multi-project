# Step 19 — Identity Cache Extraction

**Date:** 2026-03-19  
**Status:** ✅ Complete  
**Addresses:** M1 (TransmitterManager decomposition — continued follow-through)

## Summary

Extracted registered transmitter identity (MAC ownership + formatting helper) from `TransmitterManager` into a dedicated `TransmitterIdentityCache` module while preserving fallback behavior and public facade APIs.

## Changes

### New module created
- [lib/webserver/utils/transmitter_identity_cache.h](lib/webserver/utils/transmitter_identity_cache.h)
- [lib/webserver/utils/transmitter_identity_cache.cpp](lib/webserver/utils/transmitter_identity_cache.cpp)

### Ownership moved out of `TransmitterManager`
- Registered transmitter MAC storage
- Registered MAC known flag
- MAC string formatting helper

### `TransmitterManager` facade behavior preserved
- `registerMAC(...)` now delegates registration to identity cache
- `getMAC()` still preserves fallback to ESPNow global transmitter MAC when local registered identity is unavailable
- `getMACString()` now uses cache formatter
- ESP-NOW peer add path still runs from `registerMAC(...)` and uses registered MAC from cache

## Legacy removed

- Direct registered MAC state fields from `TransmitterManager` (`mac`, `mac_known`)

## Build validation

| Environment | Result |
|---|---|
| `receiver_tft` | ✅ SUCCESS |
| `receiver_lvgl` | ✅ SUCCESS |

## Notes

This step further narrows `TransmitterManager` to facade/orchestration responsibilities and removes one of the last remaining internal ownership fields unrelated to timer orchestration.
