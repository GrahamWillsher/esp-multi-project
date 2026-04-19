# Checksum Mechanisms Review and Rationalisation Recommendations (2026-04-18)

## Executive summary

Yes: there are currently too many integrity/hash mechanisms in active use, and the overlap is now creating more maintenance and migration risk than benefit.

The current state is understandable (incremental evolution + backward compatibility), but it is now a **complexity tax**:

- different algorithms for similar message classes,
- duplicated ad-hoc implementations,
- weak checks in some critical control paths,
- and protocol semantics that are not obvious from message type alone.

### Bottom line

- Keep multiple mechanisms only when they serve **different threat models** (integrity vs authenticity vs artifact identity).
- Collapse ESP-NOW payload integrity to **one standard algorithm** for structured messages.
- Move all checksum computation/verification through **shared helpers only**.
- Keep OTA cryptographic protections (SHA-256 + HMAC-SHA256), but centralise implementation.

---

## What is in use today

## 1) Additive 16-bit payload checksum (sum of bytes)

Used via shared helper:

- `EspnowPacketUtils::calculate_checksum(...)` in:
  - esp32common/espnow_common_utils/espnow_packet_utils.h

Used in packet payload and several message validators/senders, for example:

- espnowreceiver_LCD/src/espnow/espnow_runtime.cpp
- espnowreceiver_LCD/src/espnow/battery_handlers.cpp
- espnowreceiver_LCD/src/espnow/component_config_handler.cpp
- ESPnowtransmitter2/espnowtransmitter2/src/espnow/component_catalog_handlers.cpp

Also appears in ad-hoc/manual loops for some full-struct messages (not helper-driven):

- ESPnowtransmitter2/espnowtransmitter2/src/espnow/request_data_handlers.cpp
- ESPnowtransmitter2/espnowtransmitter2/src/espnow/version_beacon_manager.cpp

## 2) XOR “message checksum” (stored in uint16)

Shared helper exists:

- `EspnowPacketUtils::calculate_message_checksum(...)`
- `EspnowPacketUtils::verify_message_checksum(...)`
- in esp32common/espnow_common_utils/espnow_packet_utils.h

Used in settings sync ACK/change flows, for example:

- ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_espnow.cpp
- espnowreceiver_LCD/src/espnow/espnow_settings_sync.cpp

But there are still duplicate manual XOR implementations, for example:

- espnowreceiver_LCD/lib/webserver_lcd/api/api_settings_handlers.cpp
- espnowreceiver_2/lib/webserver/api/api_settings_handlers.cpp

## 3) Legacy `soc + power` checksum for `msg_data`

Legacy helper and validation style:

- esp32common/espnow_transmitter/espnow_transmitter.cpp
- espnowreceiver_2/src/helpers.cpp
- espnowreceiver_LCD/src/espnow/espnow_runtime.cpp (inline calc in handler)

This is effectively a special-case checksum path separate from generic packet/message helpers.

## 4) CRC32 (stronger integrity)

Shared CRC32 helpers exist and are already used in key areas:

- `crc32_packet(...)`
- `calculate_message_crc32_zeroed(...)`
- `verify_message_crc32(...)`
- in esp32common/espnow_common_utils/espnow_packet_utils.h

Used in:

- heartbeats and heartbeat ACKs:
  - ESPnowtransmitter2/espnowtransmitter2/src/espnow/heartbeat_manager.cpp
  - espnowreceiver_LCD/src/espnow/rx_heartbeat_manager.cpp
- persisted settings blobs:
  - ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_persistence.cpp

## 5) OTA SHA-256 image hash

Used for firmware artifact integrity verification:

- ESPnowtransmitter2/espnowtransmitter2/src/network/ota_upload_handler.cpp
- espnowreceiver_2/lib/webserver/api/api_control_handlers.cpp

## 6) OTA HMAC-SHA256 signatures

Used for OTA session authentication (PSK-based):

- esp32common/webserver_common_utils/src/ota_auth_utils.cpp
- esp32common/webserver_common_utils/src/ota_session_utils.cpp

## 7) FNV-1a hash in MQTT routing (not integrity)

Used for topic dispatch speed, not corruption/auth checks:

- espnowreceiver_2/src/mqtt/mqtt_client.cpp

This should not be treated as a checksum policy mechanism.

---

## Why this happened (and why it is not “wrong”, just now costly)

1. **Protocol eras overlap**
   - Early lightweight checks (sum/XOR) remained while newer CRC32 and OTA crypto were added.

2. **Different channels had different goals**
   - ESP-NOW packet corruption check vs NVS blob integrity vs OTA authenticity.

3. **Incremental migration with compatibility requirements**
   - Example: settings persistence still has legacy fallback paths.

4. **Local implementations survived after shared utilities were introduced**
   - Manual loops in multiple files now duplicate logic.

---

## Does it cause more issues than it fixes?

## What it still fixes well

- OTA SHA-256 + HMAC-SHA256 are absolutely justified and should remain.
- CRC32 on persisted blobs and heartbeat control-plane traffic is appropriate.

## What is now net-negative

- Multiple non-cryptographic checksum variants (sum, XOR, `soc+power`) for neighboring message families.
- Same conceptual message class using different checksum rules depending on code path.
- Manual per-file checksum code duplication instead of one canonical helper.
- Weak XOR checks in settings update path for control-plane messages.

### Net assessment

For ESP-NOW app-layer integrity paths, complexity is now high enough that it likely causes more integration/debug burden than protection value. The protection itself is uneven because weaker algorithms and ad-hoc implementations remain in active paths.

---

## Recommendation: target integrity architecture

## A) Keep three layers, each with a single clear purpose

1. **Artifact integrity (OTA):** SHA-256
2. **Artifact/session authenticity (OTA):** HMAC-SHA256
3. **Runtime message integrity (ESP-NOW app payloads + persisted blobs):** CRC32

Everything else should be deprecated over time.

## B) Standardise ESP-NOW structured messages on CRC32

For all structured control/config/state messages (settings, component apply, battery/info/status/config, etc):

- use trailing `uint32_t checksum` interpreted as CRC32,
- compute with shared helper only (`calculate_message_crc32_zeroed`),
- verify with shared helper only (`verify_message_crc32`).

## C) Contain legacy fast-path data checks

For ultra-high-rate tiny telemetry (`msg_data`):

- Option 1 (preferred long-term): migrate to CRC32 and version-gate protocol.
- Option 2 (short-term compatibility): keep legacy `soc+power` verification but isolate it behind one shared helper (`verify_legacy_msg_data_checksum`) and mark deprecated.

## D) Remove manual checksum loops

Replace all local XOR/sum loops with calls to canonical helpers.

This includes at least:

- ESPnowtransmitter2/espnowtransmitter2/src/espnow/request_data_handlers.cpp
- ESPnowtransmitter2/espnowtransmitter2/src/espnow/version_beacon_manager.cpp
- ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_espnow.cpp (inbound verify path)
- espnowreceiver_LCD/lib/webserver_lcd/api/api_settings_handlers.cpp
- espnowreceiver_2/lib/webserver/api/api_settings_handlers.cpp

## E) Introduce explicit checksum policy metadata

In common protocol header/docs, define per message type:

- algorithm (`LEGACY_SOC_POWER`, `SUM16`, `XOR16`, `CRC32`),
- field location and width,
- migration state (`legacy`, `current`, `deprecated_after`).

This avoids “guess from code” behavior.

---

## Concrete tidy-up plan (phased)

## Phase 0 (documentation + guardrails, low risk)

1. Add `CHECKSUM_POLICY.md` under esp32common with message-to-algorithm matrix.
2. Add lint/test rule: no raw checksum loops outside `espnow_packet_utils.h` (except clearly marked legacy wrappers).
3. Add naming rules:
   - only `*_crc32` for CRC32 fields,
   - avoid generic `checksum` for new messages.

## Phase 1 (deduplicate implementations, no wire change)

1. Replace manual XOR/sum loops with helper calls only.
2. Add helper wrappers for legacy formulas (`soc+power`) and use them everywhere that path remains.
3. Add tests ensuring helper parity with current wire behavior.

## Phase 2 (strengthen control-plane integrity)

1. Migrate settings update path from XOR16 to CRC32 (new message version or new type IDs).
2. Support dual-accept during migration window:
   - accept old XOR messages,
   - emit new CRC32 messages,
   - log old format usage.

## Phase 3 (retire weak algorithms)

1. Remove XOR16 for settings paths.
2. Remove additive sum for structured config/control payloads.
3. Keep only:
   - CRC32 for runtime structured data and persisted blobs,
   - SHA-256 + HMAC-SHA256 for OTA/security.

---

## Priority recommendations (what to do first)

1. **Immediate:** stop adding any new XOR/sum checksum paths.
2. **Immediate:** centralise remaining legacy checks into shared helpers.
3. **Next:** migrate settings/control paths to CRC32 before adding more config message families.
4. **Keep:** OTA SHA-256/HMAC design (already aligned with good practice).

---

## Expected impact

### Benefits

- fewer protocol regressions during refactors,
- easier onboarding/debugging,
- clearer security/integrity model,
- better resilience against accidental corruption in control paths.

### Costs

- temporary compatibility complexity during dual-format migration,
- small wire-size increase when moving 16-bit checks to CRC32,
- test matrix updates.

Net result is strongly positive.

---

## Final recommendation

Treat checksum/hashing as a **platform contract**, not a per-feature local choice.

- Keep cryptographic OTA protections unchanged.
- Converge ESP-NOW message integrity on CRC32.
- Remove ad-hoc checksum code from feature modules.
- Plan and execute a compatibility-window migration, then retire legacy sum/XOR paths.

This will reduce defects and integration friction without sacrificing safety.