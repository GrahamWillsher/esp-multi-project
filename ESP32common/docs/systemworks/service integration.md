# Service Integration Investigation (MQTT / NTP / OTA)

Date: 2026-03-20
Scope: transmitter + receiver + shared common utilities
Priority: OTA safety and rollback first (anti-brick)

## Implementation Progress (Live)

- [x] Phase A.1 Shared OTA boot-guard utility added (`runtime_common_utils/ota_boot_guard`).
- [x] Phase A.2 Transmitter boot-guard integrated (pending-verify detect + setup health gate + confirm/rollback).
- [x] Phase A.3 Receiver boot-guard integrated (pending-verify detect + setup health gate + confirm/rollback).
- [x] Phase A.4 OTA status/health enriched with boot-guard and rollback state fields.
- [x] Phase B.1 Receiver self-OTA endpoint implemented and registered (`/api/ota_upload_receiver`).
- [x] Legacy cleanup: receiver OTA UI multipart upload path removed; raw binary upload standardized.
- [x] Phase NTP.1 NTP lifecycle moved under Ethernet `ServiceSupervisor` ownership (start/stop on link up/down).
- [x] Phase B.2a OTA transaction ID telemetry added (`ota_txn_id` exposed by transmitter and receiver status APIs).
- [x] Phase B.2b Full two-phase commit state telemetry added (`commit_state`, `commit_detail`, post-reboot validation polling).
- [x] Phase C.1 OTA PSK now loaded from provisioned source (NVS `security/ota_psk`), placeholder/default secret blocked for OTA auth.
- [x] Phase C.2a Manifest image-hash verification implemented (`X-OTA-Image-SHA256` verified during OTA stream on transmitter and receiver self-OTA).
- [x] Phase C.3a Receiver OTA UI preflight compatibility gate added (device-type + major-version checks from embedded metadata).
- [x] Phase C.3b Server-side compatibility enforcement added on OTA endpoints (receiver self-OTA and transmitter OTA upload).
- [x] Phase D.1 OTA commit-verification monitor hardened: rollback-by-txn-mismatch detection, boot_guard_error handling, reboot-window progress, and detailed timeout diagnostics.
- [ ] Phase C.2b ⏭️ Skipped for now: detached artifact signature/public-key verification.
- [x] Phase D.2 Shared compatibility policy utility extracted into common library and consumed by OTA endpoints.

---

## 1) Executive Summary

This review confirms that the core Ethernet-driven service gating architecture is in place, but there are still critical gaps to close before this can be considered production-safe end-to-end.

### What is already strong

- Ethernet state machine and callback lifecycle exist and are active.
- MQTT has a real connection state machine with backoff and readiness checks.
- Transmitter OTA has session-based auth, upload hardening, and partition size checks.
- Both devices use OTA partition layouts (`app0` + `app1` + `otadata`).

### What is still outstanding

1. **Transactional OTA commit telemetry is implemented** (`ota_txn_id` + `commit_state` + `commit_detail` + post-reboot validation polling).
2. **Artifact authenticity verification is partially complete** (manifest image-hash verification added; detached signature/public-key validation still pending).
3. **Rollback safety flow is incomplete at application level**:
   - ✅ explicit post-boot “mark app valid” stage now implemented,
   - ✅ explicit health-gated rollback trigger now implemented,
   - no controlled two-phase commit from receiver orchestration.
4. **OTA trust model hardening is partially complete** (provisioned OTA PSK enforcement added; signed artifact verification still pending).

If not completed, a bad OTA can still leave the field process brittle and recovery uncertain.

---

## 2) Current Integration State by Service

## 2.1 MQTT Gating

Status: **Mostly complete (transmitter side)**

Evidence:
- `MqttManager::update()` gates connection attempts using `EthernetManager::is_fully_ready()`.
- Retry backoff and reconnect logic are implemented.
- Lifecycle start/stop is tied to Ethernet callbacks via `ServiceSupervisor`.

Files:
- `ESPnowtransmitter2/espnowtransmitter2/src/network/mqtt_manager.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/network/mqtt_task.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/network/service_supervisor.cpp`

Remaining work:
- Add explicit integration tests validating cable pull/reconnect while MQTT publish loop is active.

---

## 2.2 NTP / Time Gating

Status: **Implemented (service lifecycle integrated)**

Evidence:
- Time sync continues to run in `ethernet_utilities` task.
- Service lifecycle is now controlled by Ethernet callback ownership in `ServiceSupervisor`.
- Direct bootstrap start path for NTP utilities was removed from transmitter `main.cpp`.

Files:
- `ESPnowtransmitter2/espnowtransmitter2/lib/ethernet_utilities/ethernet_utilities.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/main.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/network/service_supervisor.cpp`

Risk:
- Behavioral drift from the intended single owner of service lifecycle.
- Harder to reason about exact start/stop boundaries during link transitions.

Completed:
- NTP utility task lifecycle moved under `ServiceSupervisor` callback ownership:
   - on Ethernet connected: init/start NTP utilities,
   - on Ethernet disconnected: stop NTP utilities,
   - idempotent behavior preserved.

---

## 2.3 OTA Integration (Transmitter)

Status: **Functionally strong but safety-hardening incomplete**

Implemented:
- Auth-challenge flow (`/api/ota_arm`, signed session headers).
- Upload hardening (content-type validation, no chunked transfer, timeout handling).
- Partition size pre-check before write.
- Deferred reboot flow (`ready_for_reboot`) for controlled restart.

Files:
- `ESPnowtransmitter2/espnowtransmitter2/src/network/ota_manager.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/control_handlers.cpp`
- `esp32common/webserver_common_utils/src/ota_session_utils.cpp`

Key remaining issues:
- No explicit health-gated post-boot rollback contract in app layer.
- No signed firmware artifact verification (beyond transport/session auth).
- Default OTA PSK placeholder exists in config.

---

## 2.4 OTA Integration (Receiver Orchestration)

Status: **Implemented for endpoint parity and commit telemetry**

Implemented:
- Receiver can arm transmitter OTA session and stream binary payload to transmitter.
- Receiver polls transmitter OTA status and sends reboot command when ready.

Completed gap closure:
- OTA page uploads to `/api/ota_upload_receiver` are now backed by a real API handler and route registration.
- Receiver self-OTA now accepts raw `application/octet-stream` uploads and reboots after successful apply.

Files:
- `espnowreceiver_2/lib/webserver/pages/ota_page.cpp`
- `espnowreceiver_2/lib/webserver/api/api_handlers.cpp`
- `espnowreceiver_2/lib/webserver/api/api_control_handlers.cpp`
- `espnowreceiver_2/lib/webserver/api/api_control_handlers.h`

Completed impact:
- Receiver/transmitter OTA transaction-level commit telemetry now reports explicit commit state and detail, including post-reboot validation/rollback visibility.
- Receiver OTA page blocks obvious incompatible uploads at preflight (wrong target device type / major version mismatch).
- Server-side OTA compatibility checks now reject incompatible images on upload endpoints (target device mismatch and major-version incompatibility), while still allowing legacy images without embedded metadata.
- OTA commit-verification monitor now detects rollback by transaction ID mismatch (catches silent auto-rollback), handles `boot_guard_error` state explicitly, shows reboot-window progress messages, and surfaces detailed timeout diagnostics (reachability, last-known state, boot-loop suspicion).

---

## 3) OTA Anti-Brick Rollback Plan (Required)

This is the highest-risk area and should be done first.

## Phase A — Boot Safety Contract (must-have)

1. On startup, detect whether running image is pending validation.
2. Run a strict health gate within a bounded window (e.g., 30–90s):
   - scheduler alive,
   - ESP-NOW RX/TX tasks alive,
   - mandatory init complete,
   - memory floor check,
   - (transmitter) Ethernet state machine not in fatal state.
3. If pass: mark app valid and cancel rollback.
4. If fail/timeout: trigger rollback-and-reboot.

Implementation targets:
- ✅ Transmitter: `ota_boot_guard` module called early in boot.
- ✅ Receiver: same pattern implemented.

Expected ESP-IDF calls:
- `esp_ota_mark_app_valid_cancel_rollback()`
- `esp_ota_mark_app_invalid_rollback_and_reboot()`

Notes:
- App-level confirmation flow is now explicitly implemented in both setup paths via health-gated confirmation.
- Next improvement: expand health gate with deeper task liveness and protocol readiness checks.

---

## Phase B — Two-Phase OTA Commit (receiver orchestration)

1. **Prepare**: receiver uploads firmware to target, target applies to inactive slot but does not confirm success yet.
2. **Reboot**: receiver sends controlled reboot command.
3. **Validate**: receiver waits for target heartbeat/version + health endpoint indicating “boot guard passed”.
4. **Commit**: target marks app valid only after guard success.
5. **Abort path**: if no healthy return within timeout, auto rollback should occur on target.

Deliverables:
- ✅ Add explicit OTA transaction ID in status responses.
- ✅ Add health/boot state fields: `boot_guard_passed`, `rollback_pending`, `rollback_reason`.
- ✅ Add commit-state telemetry fields and UI monitoring: `commit_state`, `commit_detail`, `state_since_ms`, `last_update_ms`.

---

## Phase C — Artifact Trust Hardening

1. ✅ Replace `CHANGE_ME_OTA_PSK_32B_MIN` with provisioned secret source (NVS/provisioning), never static default in shipping builds.
2. Add firmware authenticity verification before finalizing update:
   - ✅ manifest + image hash verification (`X-OTA-Image-SHA256`) before `Update.end(true)`.
   - ⏭️ skipped for now: signature embedded in metadata verified against trusted public key.
3. Block update if compatibility policy fails:
   - device type mismatch,
   - minimum compatible version mismatch,
   - major protocol incompatibility.

---

## 4) Required Work Items by Repository Area

## 4.1 Transmitter (`ESPnowtransmitter2/espnowtransmitter2`)

1. ✅ Integrate NTP task lifecycle into `ServiceSupervisor` (start/stop with Ethernet).
2. ✅ Add boot guard module for rollback confirmation.
3. ✅ Extend `/api/ota_status` with boot/rollback state fields.
4. ⏭️ Skipped for now: Add integration tests for Ethernet flap + MQTT/NTP/OTA behavior.
5. Replace default OTA PSK path with provisioned secret path.

## 4.2 Receiver (`espnowreceiver_2`)

1. ✅ Implement and register `/api/ota_upload_receiver`.
2. ✅ Add receiver boot guard rollback flow (same contract as transmitter).
3. Strengthen OTA orchestrator logic:
   - ✅ preflight compatibility gate (UI path),
   - ✅ server-side compatibility gate on OTA upload endpoints,
   - ✅ timeout + rollback-aware monitoring (txn-mismatch rollback detection, boot_guard_error, reboot-window progress, detailed timeout messages),
   - ✅ explicit failure reason surfacing in UI (state labels, last-known-state in timeout, boot loop detection).

## 4.3 Common (`esp32common`)

1. ✅ Add shared `ota_boot_guard` utility module for both devices.
2. ✅ Add shared compatibility policy utility (`device`, `min_compatible`, protocol major).
3. Add common OTA transaction schema for status and telemetry.

---

## 5) Acceptance Criteria (Definition of Done)

### MQTT/NTP/OTA integration gating

- Cable unplug/replug during runtime does not leave orphaned service states.
- MQTT reconnects automatically when Ethernet returns.
- NTP task starts/stops deterministically with Ethernet lifecycle.

### OTA anti-brick safety

- Failed/invalid image always rolls back automatically to last known good image.
- Successful image is marked valid only after boot guard passes.
- Receiver UI clearly reports: uploaded, rebooted, validated, committed (or rolled back).

### Receiver OTA completeness

- Receiver self-OTA endpoint exists and passes end-to-end test, or UI path is removed until implemented.

---

## 6) Recommended Execution Order

1. **Implement boot guard rollback contract (both devices).**
2. **Fix receiver self-OTA endpoint mismatch (`/api/ota_upload_receiver`).**
3. **Bring NTP lifecycle under `ServiceSupervisor` gating.**
4. **Add compatibility and artifact verification checks.**
5. **Run integration + failure injection test matrix and sign-off.**

---

## 7) Immediate Next Step

Start with a dedicated implementation branch for **OTA boot guard + rollback confirmation**, then run controlled failure tests:

- corrupted image test,
- reboot loop test,
- Ethernet-loss during OTA test,
- timeout/no-heartbeat post-upgrade test.

Only after these pass should OTA be treated as release-safe for unattended remote upgrades.
