# Receiver Transmitter Section: Ethernet Stage UI Findings and Recommendations (2026-03-26)

## 1) Objective

Prepare a pre-implementation assessment for showing transmitter Ethernet status in the receiver transmitter UI section.

### Decision update (quick fix approved)

For immediate usability, use a **2-state visual model**:

- `Connected` → **Green**
- everything else → `Not Connected` → **Red**

Detailed diagnostics will be handled through logs instead of multi-state UI for now.

---

## 2) What the transmitter actually has today

Transmitter Ethernet manager already implements a **9-stage finite state machine**:

1. `UNINITIALIZED`
2. `PHY_RESET`
3. `CONFIG_APPLYING`
4. `LINK_ACQUIRING`
5. `IP_ACQUIRING`
6. `CONNECTED`
7. `LINK_LOST`
8. `RECOVERING`
9. `ERROR_STATE`

This state model is sufficiently detailed for high-quality UX messaging.

---

## 3) What the receiver currently receives and can render

Receiver runtime/status path currently has only coarse connectivity data from periodic version beacon updates:

- `mqtt_connected` (bool)
- `ethernet_connected` (bool)

Receiver cache and APIs therefore expose coarse status only, and current dashboard/transmitter card rendering is effectively a Connected/Disconnected model.

**Conclusion:** the receiver cannot truthfully render all 9 Ethernet FSM stages yet because that state is not currently carried over the wire into receiver cache/API.

### Verification result: does receiver get a disconnected status?

**Yes.** Receiver already receives Ethernet disconnected status via `version_beacon_t.ethernet_connected` (`false`).

### Field evidence from live test (cable removal)

Observed on receiver after Ethernet cable removal from transmitter:

- `Runtime: ETH=DISCONNECTED`
- `Runtime status updated: MQTT=DISCONNECTED, ETH=DISCONNECTED`

Then later in the same session the receiver logged:

- `[VERSION_BEACON] Received ... (MQTT:CONN, ETH:UP)`
- `Runtime: ETH=CONNECTED`
- `Runtime status updated: MQTT=CONNECTED, ETH=CONNECTED`

This confirms two important facts:

1. Receiver already handles and stores the **disconnected** state.
2. Receiver also updates back to **connected** when a later beacon reports Ethernet/MQTT as up.

So the 2-state UI is already supported by real runtime data.

Clarification:

- It is not a separate dedicated "disconnected message type".
- It is a runtime field inside `msg_version_beacon` payload.
- Receiver consumes it and updates runtime cache through `TransmitterManager::updateRuntimeStatus(mqtt, ethernet)`.

Observed timing behavior:

- Periodic beacon path provides status refresh every **30 seconds** (`PERIODIC_INTERVAL_MS = 30000`).
- There is also forced beacon sending on some state changes (for example MQTT connect/disconnect), which can make updates arrive sooner.
- If ESP-NOW link is unavailable at that moment, beacon delivery can be delayed until connectivity resumes.
- Latest received runtime beacon wins; a later `ETH:UP` beacon will overwrite an earlier disconnected status in receiver cache.

Interpretation of the supplied log:

- The receiver side is behaving correctly for the requested 2-state model.
- The later `ETH:UP` / `MQTT:CONN` beacon means transmitter subsequently reported those services as available again.
- That may represent a genuine recovery/reconnect, or a later runtime re-sample from transmitter that superseded the earlier disconnected status.

Implication for requested quick fix:

- The required two states are already present in receiver data model.
- Implementation is straightforward UI mapping on existing `ethernet_connected` bool.

---

## 4) Gap analysis

### Gap A: Protocol fidelity gap

No dedicated field for transmitter Ethernet FSM stage in current receiver-consumed runtime status payload.

### Gap B: Receiver model/API gap

Receiver state cache and telemetry API currently store/serve booleans rather than stage enum.

### Gap C: UI semantics gap

UI has single-dot style + coarse text. No stage wording, no transition/stabilization semantics, and no explicit error/recovery presentation.

---

## 5) Recommended LED + wording standard

### 5.1 Immediate implementation (requested quick fix)

Use only this mapping on receiver web UI:

| Available signal | UI wording | LED |
|---|---|---|
| `ethernet_connected=true` | `Connected` | Green solid |
| `ethernet_connected=false` | `Not Connected` | Red solid |

This works with current payloads and does not require protocol changes.

Data source for this mapping is already live: receiver runtime cache updated from incoming version beacon runtime status.

### 5.2 Full-stage target model (future)

Recommended canonical mapping once full stage telemetry is available:

| Transmitter stage | UI wording | LED color/effect | Notes |
|---|---|---|---|
| `UNINITIALIZED` | Ethernet: Initializing | Blue pulse | Boot/startup |
| `PHY_RESET` | Ethernet: Resetting PHY | Blue flash | Hardware reset in progress |
| `CONFIG_APPLYING` | Ethernet: Applying network config | Blue pulse | Static/DHCP config apply |
| `LINK_ACQUIRING` | Ethernet: Waiting for link | Orange pulse | Cable/switch negotiation |
| `IP_ACQUIRING` | Ethernet: Obtaining IP | Orange flash | DHCP/static bring-up |
| `CONNECTED` | Ethernet: Connected | Green solid | Normal online state |
| `LINK_LOST` | Ethernet: Link lost | Red solid | Immediate fault indication |
| `RECOVERING` | Ethernet: Recovering | Orange flash | Auto-retry and backoff |
| `ERROR_STATE` | Ethernet: Error | Red flash | Terminal/explicit error state |

### 5.3 MQTT diagnostics availability (checked)

Short answer: **yes, detailed Ethernet errors are available via MQTT logging when MQTT log transport is available and log levels allow them**.

Observed behavior from current implementation:

- Ethernet FSM emits explicit error/warning/info logs (timeouts, link loss, recovery, state transitions).
- Transmitter initializes `MqttLogger` in MQTT task and updates availability state on connect/disconnect.
- Buffered logs are flushed from MQTT task after reconnect.

Important limits (so not strictly "guaranteed complete" in all outage scenarios):

- MQTT logger buffer is finite (`BUFFER_SIZE = 20`), so prolonged disconnect + high log rate can drop older messages.
- Messages are level-filtered (runtime level + MQTT logger level).
- Message formatting uses fixed buffer sizes, so very long lines can be truncated.

Operational recommendation:

- Keep UI as 2-state red/green quick indicator.
- Use MQTT logs as primary diagnostics stream.
- Keep Serial logs enabled as fallback during MQTT outages.

---

## 6) Implementation recommendations (phased)

### Phase 0 (now): quick visual fix

1. Receiver transmitter card: map only `ethernet_connected` bool to:
   - `Connected` + green
   - `Not Connected` + red
2. Keep wording intentionally simple for immediate operator clarity.
3. Do not block on protocol/API extension.

### Phase 1 (recommended first): Protocol + receiver data-path enablement

1. Add explicit `ethernet_state` enum field to transmitter runtime beacon/status payload.
2. Keep existing booleans for backward compatibility (`ethernet_connected`, `mqtt_connected`).
3. Update receiver ingestion/cache model to persist both:
   - raw stage enum
   - derived booleans (for legacy consumers)
4. Update receiver APIs to expose:
   - `ethernet_state` (numeric)
   - `ethernet_state_text` (server-derived canonical string)
   - legacy booleans unchanged

### Phase 2: UI transmitter section enhancement

1. Update transmitter card rendering logic to use stage enum first.
2. Apply LED color/effect mapping table from section 5.
3. Show stage text and optional subtext (e.g., "retry 2/5", timeout reason).
4. Retain compatibility fallback when stage enum absent.

### Phase 3: Diagnostics hardening

1. Add transition timestamp and last error code/message in API for supportability.
2. Add unit/integration checks for state-to-text and state-to-LED mapping.
3. Add a simulated test mode in UI for all 9 states (dev/debug toggle).

---

## 7) Backward compatibility strategy

- Do not remove existing booleans.
- Treat `ethernet_state` as optional until both ends are rolled out.
- Receiver UI logic should follow:
  1. use `ethernet_state` if present;
  2. else fallback to existing boolean-based status.

This prevents breakage during staggered deployments.

---

## 8) Risk assessment

- **Low risk:** UI-only text/LED changes if stage enum already available.
- **Medium risk:** protocol struct update (wire format) if not version-gated.
- **Mitigation:** additive field + compatibility fallback + staged rollout.

---

## 9) Final recommendation

Proceed with a **two-track approach**:

1. **Immediate UX**: deploy 2-state receiver UI (`Connected` green / `Not Connected` red).
2. **Diagnostics**: rely on MQTT logs for detailed Ethernet fault context, with Serial fallback.
3. **Future enhancement**: add full `ethernet_state` telemetry later for true multi-stage UI.

This delivers a fast visual improvement now while preserving a path to stage-accurate status in a later iteration.
