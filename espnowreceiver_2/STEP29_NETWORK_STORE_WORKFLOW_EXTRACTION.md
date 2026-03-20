# Step 29 — Network-store workflow extraction

Date: 2026-03-19

## Summary

Extracted network write workflows from `TransmitterManager` into a dedicated helper module.

## Changes

- Added `lib/webserver/utils/transmitter_network_store_workflow.h`
- Added `lib/webserver/utils/transmitter_network_store_workflow.cpp`
- Updated `lib/webserver/utils/transmitter_manager.cpp`:
  - `storeIPData(...)` now delegates to `TransmitterNetworkStoreWorkflow::store_ip_data(...)`
  - `storeNetworkConfig(...)` now delegates to `TransmitterNetworkStoreWorkflow::store_network_config(...)`

## Behavior preserved

- Both workflows still store through `TransmitterNetworkCache`
- Both workflows still short-circuit when no change is stored
- Both workflows still perform dashboard notify + write-through persistence after successful store

## Legacy removed from `TransmitterManager`

- Direct network store + notify/persist orchestration in `storeIPData(...)`
- Direct network-config store + notify/persist orchestration in `storeNetworkConfig(...)`

## Validation

- IDE diagnostics clean on modified files.
- Build validation complete:
  - `pio run -e receiver_tft -j 12` ✅ (RAM 28.7%, Flash 19.2%)
  - `pio run -e receiver_lvgl -j 12` ✅ (RAM 28.2%, Flash 21.4%)