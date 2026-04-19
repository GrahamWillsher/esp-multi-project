# ESP-NOW Checksum Rationalisation: Migration to CRC32 (2026-04-18)

## Objective

Migrate all ESP-NOW message integrity checks to a **single algorithm — CRC32** — using the shared helpers already present in `esp32common`.

This eliminates the current mix of additive sum, XOR16, and legacy `soc+power` checks across ESP-NOW message types, which has grown into a maintenance and debugging burden without providing meaningful differentiation in protection.

OTA cryptographic protections (SHA-256, HMAC-SHA256) and MQTT routing hashes (FNV-1a) are **out of scope** — they serve different purposes and are not changing.

---

## Current ESP-NOW checksum landscape

There are currently three distinct integrity algorithms in use across ESP-NOW messages:

### 1) Additive 16-bit sum

Shared helper: `EspnowPacketUtils::calculate_checksum(...)` in `esp32common/espnow_common_utils/espnow_packet_utils.h`

Used in:

- `espnowreceiver_LCD/src/espnow/battery_handlers.cpp`
- `espnowreceiver_LCD/src/espnow/component_config_handler.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/component_catalog_handlers.cpp`

Also duplicated as manual inline loops (not using the helper) in:

- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/request_data_handlers.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/version_beacon_manager.cpp`

### 2) XOR16

Shared helpers: `EspnowPacketUtils::calculate_message_checksum(...)` / `verify_message_checksum(...)` in `esp32common/espnow_common_utils/espnow_packet_utils.h`

Used in:

- `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_espnow.cpp`
- `espnowreceiver_LCD/src/espnow/espnow_settings_sync.cpp`

Also duplicated as manual inline loops in:

- `espnowreceiver_LCD/lib/webserver_lcd/api/api_settings_handlers.cpp`
- `espnowreceiver_2/lib/webserver/api/api_settings_handlers.cpp`

### 3) Legacy `soc + power` field sum

Inline calculation with no shared helper, used in:

- `esp32common/espnow_transmitter/espnow_transmitter.cpp`
- `espnowreceiver_2/src/helpers.cpp`
- `espnowreceiver_LCD/src/espnow/espnow_runtime.cpp`

### 4) CRC32 — target algorithm (already in use)

Shared helpers already exist and are used correctly in heartbeat and settings persistence paths:

- `EspnowPacketUtils::calculate_message_crc32_zeroed(...)`
- `EspnowPacketUtils::verify_message_crc32(...)`
- `EspnowPacketUtils::crc32_packet(...)`

in `esp32common/espnow_common_utils/espnow_packet_utils.h`

Currently used in:

- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/heartbeat_manager.cpp`
- `espnowreceiver_LCD/src/espnow/rx_heartbeat_manager.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_persistence.cpp`

---

## Target state

All ESP-NOW message types use a single trailing `uint32_t checksum` field computed as CRC32, using the canonical shared helpers only:

- **compute:** `EspnowPacketUtils::calculate_message_crc32_zeroed(&msg, sizeof(msg))`
- **verify:** `EspnowPacketUtils::verify_message_crc32(&msg, sizeof(msg))`

No additive sum, XOR16, or `soc+power` checks remain in any ESP-NOW send or receive path.

---

## Migration plan

### Phase 0 — Preparation (no wire change, no compatibility risk)

**Goal:** lay groundwork and prevent regression before any protocol changes.

1. Add a `CHECKSUM_POLICY.md` to `esp32common/espnow_common_utils/` listing every message struct, its current checksum field name, type, and algorithm.
2. Replace all manual inline XOR/sum loops with calls to the existing shared helpers. This is a refactor only — no wire format change. Affected files:
   - `ESPnowtransmitter2/espnowtransmitter2/src/espnow/request_data_handlers.cpp`
   - `ESPnowtransmitter2/espnowtransmitter2/src/espnow/version_beacon_manager.cpp`
   - `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_espnow.cpp`
   - `espnowreceiver_LCD/lib/webserver_lcd/api/api_settings_handlers.cpp`
   - `espnowreceiver_2/lib/webserver/api/api_settings_handlers.cpp`
3. Wrap the legacy `soc+power` check behind a single shared helper — `EspnowPacketUtils::verify_legacy_msg_data_checksum(...)` — so there is one place to update or remove it later.
4. Add unit tests asserting that each helper produces output matching current wire behavior, so Phase 1 changes can be validated without hardware.

### Phase 1 — Migrate structured control/config/state messages to CRC32

**Goal:** all non-high-rate structured messages use CRC32. Transmitter and receiver must be updated together (or dual-accept logic added — see below).

**Message families to migrate (additive sum → CRC32):**

| Message type | Current algorithm | Checksum field | Change |
|---|---|---|---|
| battery info/status/config | additive sum | `uint16_t checksum` | → `uint32_t checksum` (CRC32) |
| component config apply | additive sum | `uint16_t checksum` | → `uint32_t checksum` (CRC32) |
| component catalog fragment | additive sum | `uint16_t checksum` | → `uint32_t checksum` (CRC32) |
| version beacon | additive sum (inline) | `uint16_t checksum` | → `uint32_t checksum` (CRC32) |
| request data | additive sum (inline) | `uint16_t checksum` | → `uint32_t checksum` (CRC32) |

**Message families to migrate (XOR16 → CRC32):**

| Message type | Current algorithm | Checksum field | Change |
|---|---|---|---|
| settings change / sync | XOR16 | `uint16_t checksum` | → `uint32_t checksum` (CRC32) |
| settings ACK | XOR16 | `uint16_t checksum` | → `uint32_t checksum` (CRC32) |

**Migration procedure for each message family:**

1. Rename the checksum field from `uint16_t checksum` to `uint32_t checksum` in the message struct. This is a wire-breaking change: bump the message version or protocol generation constant at the same time.
2. Update the sender to call `calculate_message_crc32_zeroed`.
3. Update the receiver to call `verify_message_crc32`.
4. If transmitter and receiver cannot be flashed simultaneously, add a dual-accept window:
   - Accept both old (16-bit) and new (32-bit) formats, keyed on message version field.
   - Log old-format receptions as `[WARN] legacy checksum format received`.
   - Remove dual-accept path once all devices on the network are updated.

**Completion criterion:** no call to `calculate_checksum`, `calculate_message_checksum`, or `verify_message_checksum` remains in any ESP-NOW send or receive path.

### Phase 2 — Migrate high-rate telemetry (`msg_data` / `soc+power`)

**Goal:** retire the `soc+power` path. Deferred to Phase 2 because `msg_data` is the highest-rate message type and has the widest compatibility surface.

1. Define a new `msg_data_v2` struct with a trailing `uint32_t checksum` (CRC32) field.
2. Transmitter emits `msg_data_v2` alongside `msg_data` during a compatibility window (keyed on a new message type ID).
3. Receiver prefers `msg_data_v2` when recognised; falls back to legacy for older transmitters.
4. Once all transmitters on the network are updated, remove `msg_data` emission and delete `verify_legacy_msg_data_checksum`.

### Phase 3 — Cleanup

1. Delete deprecated helpers: `calculate_checksum`, `calculate_message_checksum`, `verify_message_checksum`, `verify_legacy_msg_data_checksum`.
2. Update `CHECKSUM_POLICY.md` to reflect the completed single-algorithm state.
3. Add a CI/lint assertion: no symbol matching `calculate_checksum|calculate_message_checksum|verify_message_checksum` exists outside `espnow_packet_utils.h`.

---

## Phase summary

| Phase | What changes | Wire change? | Risk |
|---|---|---|---|
| 0 | Centralise inline loops, wrap legacy helper, add tests | No | Low |
| 1 | Structured messages: sum/XOR → CRC32 | Yes (version-gated) | Medium |
| 2 | High-rate telemetry: soc+power → CRC32 | Yes (dual-accept) | Medium |
| 3 | Delete deprecated helpers and old code paths | No | Low |

After Phase 3, every ESP-NOW message integrity check uses CRC32, computed and verified via two shared functions in `espnow_packet_utils.h`. No per-feature checksum logic remains in any message handler.
