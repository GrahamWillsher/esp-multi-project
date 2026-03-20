# Step 28 — MQTT-config workflow extraction

Date: 2026-03-19

## Summary

Extracted MQTT-config storage orchestration from `TransmitterManager::storeMqttConfig(...)` into a dedicated helper module.

## Changes

- Added `lib/webserver/utils/transmitter_mqtt_config_workflow.h`
- Added `lib/webserver/utils/transmitter_mqtt_config_workflow.cpp`
- Updated `lib/webserver/utils/transmitter_manager.cpp` so `storeMqttConfig(...)` delegates to `TransmitterMqttConfigWorkflow::store_mqtt_config(...)`

## Behavior preserved

- MQTT config still stores through `TransmitterMqttCache::store_config(...)`
- Existing comment/semantics for ignoring stale `connected` config input are unchanged
- Existing null-safe server-IP logging fallback and log format are unchanged
- Write-through NVS persistence still occurs after config storage

## Legacy removed from `TransmitterManager`

- Direct MQTT-config workflow orchestration in `storeMqttConfig(...)`

## Validation

- IDE diagnostics clean on modified files.
- `pio run -e receiver_tft -j 12` ✅
- `pio run -e receiver_lvgl -j 12` ✅