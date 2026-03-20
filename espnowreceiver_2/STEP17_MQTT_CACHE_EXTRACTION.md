# Step 17 — MQTT Cache Extraction

**Date:** 2026-03-19  
**Status:** ✅ Complete  
**Addresses:** M1 (TransmitterManager decomposition — continued follow-through)

## Summary

Extracted MQTT configuration/runtime cache ownership and MQTT NVS persistence from `TransmitterManager` into a dedicated `TransmitterMqttCache` module while keeping `TransmitterManager` as a stable facade.

## Changes

### New module created
- [lib/webserver/utils/transmitter_mqtt_cache.h](lib/webserver/utils/transmitter_mqtt_cache.h)
- [lib/webserver/utils/transmitter_mqtt_cache.cpp](lib/webserver/utils/transmitter_mqtt_cache.cpp)

### Ownership moved out of `TransmitterManager`
- MQTT enabled flag
- MQTT server IP, port, username, password, client ID
- MQTT runtime connection flag
- MQTT config-known flag and config version
- MQTT-specific NVS keys load/save logic

### `TransmitterManager` facade updates
- Delegates MQTT config store/get methods to `TransmitterMqttCache`
- Delegates MQTT load/save in NVS paths to `TransmitterMqttCache`
- Keeps existing public API signatures unchanged
- Keeps runtime status behavior unchanged (`updateRuntimeStatus(...)` still updates ETH via `TransmitterStatusCache` and MQTT via cache runtime update)

### Minor hardening included
- Logging in `storeMqttConfig(...)` now uses cached server with null-safe fallback instead of assuming non-null incoming server pointer.

## Legacy removed

- `TransmitterManager::MqttCache` struct/state
- MQTT key constants and MQTT NVS persistence code from `TransmitterManager`

## Build validation

| Environment | Result |
|---|---|
| `receiver_tft` | ✅ SUCCESS |
| `receiver_lvgl` | ✅ SUCCESS |

## Notes

This continues the pattern established in prior steps: move domain ownership into focused cache modules while preserving a compatibility facade in `TransmitterManager`.
