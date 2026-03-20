# Step 21 — ESP-NOW peer registry extraction

**Date:** 2026-03-19  
**Status:** ✅ Complete  
**Addresses:** M1 (decomposition follow-through)

## Summary

Extracted ESP-NOW peer registration logic from `TransmitterManager::registerMAC(...)` into a dedicated `TransmitterPeerRegistry` module.

## Changes

### New module created
- `lib/webserver/utils/transmitter_peer_registry.h`
- `lib/webserver/utils/transmitter_peer_registry.cpp`

### Ownership moved out of `TransmitterManager`
- `esp_now_is_peer_exist(...)` check
- `esp_now_peer_info_t` construction and initialization
- `esp_now_add_peer(...)` execution and result logging

### `TransmitterManager` facade behavior preserved
- `registerMAC(...)` still:
  - registers MAC in `TransmitterIdentityCache`
  - logs MAC string
  - notifies SSE dashboard updates
- `registerMAC(...)` now delegates peer registration to:
  - `TransmitterPeerRegistry::ensure_peer_registered(...)`

## Legacy removed

- Direct ESP-NOW peer-add logic from `TransmitterManager::registerMAC(...)`
- Direct ESP-NOW includes from `transmitter_manager.cpp` (`esp_now.h`, `string.h`)

## Build validation

| Environment | Result |
|---|---|
| `receiver_tft` | ✅ SUCCESS |
| `receiver_lvgl` | ✅ SUCCESS |

## Notes

This further narrows `TransmitterManager` to compatibility-facade responsibilities while pushing transport-specific operations into dedicated helper modules.
