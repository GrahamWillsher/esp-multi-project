# Agent Contract / Engineering Principles

## 1. Core Philosophy
- Prefer reuse over rewriting
- Keep code minimal and readable
- Avoid unnecessary abstraction

## 2. Code Rules
- Follow SOLID principles / insdustry standards where possible
- Prefer composition over inheritance
- Do not introduce breaking changes for minor improvements
- Clarity over cleverness.
- One responsibility per module.

## 3. Refactoring Guidelines
- Small signature changes → refactor existing code
- Large behavioural change → create new implementation

## 4. Safety Checks
Before finalising code:
- ✅ Does it align with existing architecture?
- ✅ Does it reuse existing components?
- ✅ Does it introduce duplication?
- ✅ Does it comply with this document?
- if it fails on any of these go backand do it again

## 5. Anti-patterns
- Code duplication
- Over-engineering
- Silent breaking changes

## 6. Agent Behaviour Rules
- Always check this document before modifying code
- Prefer modifying existing code over creating new files
- Do not ignore constraints in this document

## Standing Policies (apply to every step)

**1. Legacy removal is mandatory at each step.**
- All redundant, obsolete, duplicate, or compatibility-only code introduced or superseded by a step is removed at that step's completion. Code that is merely "unused but harmless" does not survive. 

**2. This document is updated at each step completion.**
- The tracking table and step notes are updated to record exactly what was changed and what was removed. The document is the single source of truth for the state of the refactor.

## 7. Step Tracking (Phase 9 closeout)

| Step | Status | Date | What changed | What legacy was removed |
|---|---|---|---|---|
| Slice A | ✅ Completed | 2026-05-25 | Transmitter ACK/control flow confirmed on MQTT publish paths (`publish_control_ack`, `publish_component_apply_ack`, `publish_network_ack`, `publish_mqtt_ack`) | Remaining guarded ESPNOW send callsites removed from active runtime path |
| Slice B | ✅ Completed | 2026-05-25 | Transport layer cleaned to MQTT-only command/ack behavior | `tx_send_guard.*` and `guarded_send_backend.*` deleted |
| Slice C | ✅ Completed | 2026-05-25 | LCD page generation logic retained with heap-only throttling behavior | `radio_pressure_state.*` stub and dependent usage removed |
| Slice D | ✅ Completed | 2026-05-25 | Shared include surface aligned to transport-neutral contracts | Empty `esp32common/include/espnow/` artifact deleted |
| Validation | ✅ Completed | 2026-05-25 | Rebuilt transmitter + both receivers and rechecked symbol hygiene | Temporary one-off migration scripts removed from repo root |
| Alignment pass 2 | ✅ Completed | 2026-05-25 | Receiver/LCD runtime and web API telemetry now bind to `RuntimeState` + canonical MQTT topic constants | Residual `ESPNow::*` compatibility globals, stale `<esp_now.h>` includes, and old packet-utils test includes removed |

### Alignment Check Result (2026-05-25)
- Documented Phase 9 completion criteria now match active source state.
- No active Phase 9 legacy symbols remain in source (`tx_send_guard`, `guarded_send_backend`, `radio_pressure_state`, `esp_now_send`).
- No active receiver/LCD source includes remain for `<esp_now.h>`.
- No active tests include `esp32common/espnow/packet_utils.h`.