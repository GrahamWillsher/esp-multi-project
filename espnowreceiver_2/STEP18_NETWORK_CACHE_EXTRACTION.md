# Step 18 — Network Cache Extraction

**Date:** 2026-03-19  
**Status:** ✅ Complete  
**Addresses:** M1 (TransmitterManager decomposition — continued follow-through)

## Summary

Extracted network configuration ownership and network NVS persistence from `TransmitterManager` into a dedicated `TransmitterNetworkCache` module while preserving `TransmitterManager` as a stable facade.

## Changes

### New module created
- [lib/webserver/utils/transmitter_network_cache.h](lib/webserver/utils/transmitter_network_cache.h)
- [lib/webserver/utils/transmitter_network_cache.cpp](lib/webserver/utils/transmitter_network_cache.cpp)

### Ownership moved out of `TransmitterManager`
- Current IP/gateway/subnet
- Static IP/gateway/subnet
- Static primary/secondary DNS
- IP-known flag
- Static/DHCP mode flag
- Network config version
- Network-specific NVS key load/save logic

### `TransmitterManager` facade updates
- Delegates network NVS load/save to `TransmitterNetworkCache`
- Delegates all network API methods (`store*`, getters, mode/version, URL helpers)
- Preserves existing signatures and caller behavior
- Retains dashboard update + persistence orchestration in facade methods (`SSENotifier::notifyDataUpdated()`, `saveToNVS()`)

## Legacy removed

- `TransmitterManager::NetworkCache` struct/state
- Network NVS key constants and network load/save logic from `TransmitterManager`

## Build validation

| Environment | Result |
|---|---|
| `receiver_tft` | ✅ SUCCESS |
| `receiver_lvgl` | ✅ SUCCESS |

## Notes

This step removes the last major non-MAC ownership domain from `TransmitterManager`. Remaining direct ownership is now mostly identity/facade orchestration (`mac`, timer/debounce wiring) rather than broad data caching.
