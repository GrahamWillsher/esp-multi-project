# Step 31 — Metadata-store workflow extraction

Date: 2026-03-19

## Summary

Extracted metadata write workflow from `TransmitterManager` into a dedicated helper module.

## Changes

- Added `lib/webserver/utils/transmitter_metadata_store_workflow.h`
- Added `lib/webserver/utils/transmitter_metadata_store_workflow.cpp`
- Updated `lib/webserver/utils/transmitter_manager.cpp`:
  - `storeMetadata(...)` now delegates to `TransmitterMetadataStoreWorkflow::store_metadata(...)`

## Behavior preserved

- Metadata is still stored through `TransmitterStatusCache`
- Metadata writes still persist to NVS immediately after store

## Legacy removed from `TransmitterManager`

- Direct metadata-store + persist orchestration in `storeMetadata(...)`

## Validation

- IDE diagnostics clean on modified files.
- Build validation complete:
  - `pio run -e receiver_tft -j 12` ✅ (RAM 28.7%, Flash 19.2%)
  - `pio run -e receiver_lvgl -j 12` ✅ (RAM 28.2%, Flash 21.4%)
