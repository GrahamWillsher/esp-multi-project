# Receiver `/events` Clear Workflow Investigation (2026-04-07)

## Scope
Investigate current `/events` clear behavior on receiver and verify alignment with requested behavior:
1. Use standard confirmation UX.
2. Send clear command to transmitter.
3. Transmitter performs event-log clear.
4. Receiver returns user to `/` after success.

Transport policy for this investigation:
- **No HTTP transport is used for event log messaging between receiver and transmitter.**
- Event-log control and data-plane coordination are ESP-NOW + MQTT only.

## Current State (Observed)

### 1) Confirmation UX is custom local arm/disarm, not standard confirm dialog
- Receiver page script uses a two-click arming state (`clearArmed`) and temporary button relabel to **"Confirm Clear Event Logs"**.
- This is implemented by `armClearButton()` / `disarmClearButton()`.
- There is no shared confirmation helper used by this page.

Evidence:
- `armClearButton()` sets `btn.textContent = 'Confirm Clear Event Logs';`
- `clearEventLogs()` first click only arms; second click executes clear.

File:
- `espnowreceiver_2/lib/webserver/pages/event_logs_page_script.cpp`

### 2) Receiver clear API currently clears only local receiver cache
- `/api/clear_event_logs` handler calls `TransmitterManager::clearEventLogs()` and returns success message **"Receiver event log cache cleared (MQTT-only mode)"**.
- No command is forwarded to transmitter in this handler.

File:
- `espnowreceiver_2/lib/webserver/api/api_telemetry_handlers.cpp`

### 3) Redirect to `/` already exists on successful clear
- In `clearEventLogs()`, after successful response, status text is updated and a delayed redirect is executed:
  - `setTimeout(() => { window.location.href = '/'; }, 3000);`

File:
- `espnowreceiver_2/lib/webserver/pages/event_logs_page_script.cpp`

### 4) Transmitter clear operation exists, but receiver clear flow does not currently invoke it
- Transmitter firmware already contains clear logic (`reset_all_events()`), but receiver `/api/clear_event_logs` currently does not trigger transmitter-side clear.
- Any transmitter-local HTTP clear endpoint is **not** part of the receiver↔transmitter event-log transport model.

File:
- `ESPnowtransmitter2/espnowtransmitter2/src/network/ota_status_handlers.cpp`

### 5) ESP-NOW control plane currently supports subscribe/unsubscribe + summary request, but no dedicated clear action
- Receiver sends event-log control via `send_event_logs_control(bool subscribe)` using `event_logs_control_t.action` where:
  - `0 = unsubscribe`
  - `1 = subscribe`
- Transmitter route for `msg_event_logs_control` handles only subscribe/unsubscribe paths.

Files:
- `espnowreceiver_2/src/espnow/espnow_send.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/message_routes.cpp`
- `esp32common/espnow_transmitter/espnow_common.h`

## Gap vs Requested Behavior

Requested behavior | Current behavior | Status
--- | --- | ---
Standard confirmation UX | Local two-click arm/disarm button | ❌ Not aligned
Receiver sends clear to transmitter | No transmitter clear command from receiver API | ❌ Not aligned
Transmitter clears logs | Clear logic exists, but not triggered by current receiver clear flow | ⚠️ Partially available
Return to `/` after clear | Already implemented in page script | ✅ Aligned

## Recommended Implementation (Preferred)

### A) Use standard confirmation UX on `/events`
Replace arm/disarm flow with browser-standard confirmation dialog in `clearEventLogs()`:
- `if (!window.confirm('Clear all transmitter event logs?')) return;`

This removes page-specific state (`clearArmed`, timer) and aligns with standard confirmation behavior.

### B) Add explicit ESP-NOW clear command (preferred over HTTP fallback)
To keep event-log control plane consistent with current architecture:
1. Extend protocol:
   - In `event_logs_control_t.action`, reserve a new value:
     - `2 = clear`.
2. Receiver send path:
   - Add `send_event_logs_clear_request()` in receiver ESP-NOW send module.
3. Transmitter route:
   - In `msg_event_logs_control` handler, on `action == 2` call `reset_all_events()`.
4. Receiver API:
   - In `api_clear_event_logs_handler()`, send ESP-NOW clear request first.
   - If send accepted, clear receiver cache and return success.
   - If not connected/send fails, return actionable error.

Note:
- This preserves the project rule that event-log transport does not use HTTP between devices.

### C) Keep redirect behavior after success
Retain existing delayed redirect to `/` after successful clear response.

## HTTP Transport Status for Event Logs
- Receiver and transmitter event-log interaction is MQTT + ESP-NOW only.
- HTTP is not used as a receiver↔transmitter transport for event-log subscribe, unsubscribe, fetch, summary, or clear commands.
- Receiver `/api/*` endpoints are UI-facing local handlers only.

## Conclusion
Current receiver `/events` clear flow does **not** yet satisfy the requested end-to-end behavior because clear is local-only and confirmation UX is custom arm/disarm. Redirect behavior is already correct. The clean architectural fix is to add an explicit ESP-NOW clear command and switch UI to a standard confirmation dialog, while keeping event-log transport strictly non-HTTP between receiver and transmitter.
