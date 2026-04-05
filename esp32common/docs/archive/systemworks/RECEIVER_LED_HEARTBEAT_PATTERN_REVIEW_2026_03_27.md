# Receiver Simulated LED Heartbeat Pattern Review (2026-03-27)

## Scope
Review of current simulated LED behavior on the receiver for:
- `Energy Flow` mode
- `Heartbeat` mode

And recommendations for implementing a true two-beat cardiac pattern (`duh-duh ... pause`) with minimal risk.

---

## Executive Summary
Current implementation does **not** produce a true 2-beat cardiac heartbeat.  
`Heartbeat` is currently a single pulse pattern, while `Energy Flow` is a symmetric on/off blink. This explains why heartbeat appears like a faster/simple blink rather than `duh-duh`.

### Confirmed implementation decision
The implementation is now locked to:
1. **Option A**: non-blocking 2-beat heartbeat inside the receiver renderer task.
2. **Profile 1 timings**.
3. Timings moved into a **shared config struct** (single source of truth).
4. Remove all old/redundant/legacy helper animation paths on completion (no helper fallback left).

---

## Findings

## 1) Wire protocol and mode mapping are correct
- Transmitter sends:
  - Classic -> `LED_WIRE_CONTINUOUS`
  - Energy Flow -> `LED_WIRE_FLASH`
  - Heartbeat -> `LED_WIRE_HEARTBEAT`
- Receiver stores and applies effect codes correctly.

So the issue is **not** in ESP-NOW mode mapping.

## 2) Runtime renderer heartbeat is single pulse, not two-beat
Current receiver renderer behavior:
- `FLASH`: ~500ms ON, ~500ms OFF (symmetric blink)
- `HEARTBEAT`: ~180ms ON, ~1020ms OFF (single pulse)

No second beat exists in the heartbeat cycle.

## 3) `heartbeat_led()` helper is not suitable as-is
- Current helper is blocking (uses delays), single pulse, and not used by the non-blocking renderer path.
- Reusing it directly would regress task responsiveness and display mutex contention behavior.

Conclusion: helper should be rewritten before reuse, or removed.

---

## Design Options

## Option A (recommended): Non-blocking 2-beat in renderer
Implement heartbeat with explicit phases in the existing renderer task:

Confirmed cycle (**Profile 1**):
1. Beat 1 ON: 120ms
2. Gap 1 OFF: 100ms
3. Beat 2 ON: 120ms
4. Long pause OFF: 760ms

Total cycle: 1100ms (~54.5 bpm visual cadence, subjectively clear cardiac pattern)

Advantages:
- Preserves current non-blocking architecture
- Minimal changes in one place
- No protocol/API changes required
- Easy to tune timings

Risks:
- None significant if only effect timing logic is touched

## Option B: Reuse helper by rewriting helper internals
Refactor `heartbeat_led()` into a non-blocking phase function and call from renderer.

Advantages:
- Centralized effect logic

Disadvantages:
- Requires broader API rethink (helpers currently delay/block)
- More invasive than needed for immediate fix

## Option C: Add richer wire protocol (not recommended now)
Add per-effect phase/timing payload from transmitter.

Advantages:
- Dynamic control

Disadvantages:
- Expands protocol surface, unnecessary for this bug
- Increases compatibility complexity

---

## Suggested Timing Profiles

## Profile 1 (balanced)
- Beat1 ON 120ms
- Gap 100ms
- Beat2 ON 120ms
- Pause 760ms

## Profile 2 (punchier)
- Beat1 ON 100ms
- Gap 80ms
- Beat2 ON 110ms
- Pause 710ms

## Profile 3 (slower, calm)
- Beat1 ON 140ms
- Gap 120ms
- Beat2 ON 140ms
- Pause 900ms

Recommendation: start with **Profile 1**.

---

## Keep/Remove Helper Decision

Current helper status:
- `heartbeat_led()` and `flash_led()` are delay-based animation helpers
- Renderer task now owns runtime LED animation with non-blocking logic

Confirmed decision:
- Remove legacy helper animation code on completion so runtime behavior is owned by one path only.
- No compatibility wrapper layer will be retained for heartbeat/flash helper animations.
- Shared timing config struct becomes the sole timing authority.

---

## Additional Suggestions
1. Move effect timings into a shared config struct (constants) in receiver config header. **(Confirmed)**
2. Add debug logging once per effect transition (not every frame) for easy tuning.
3. Optionally expose current heartbeat phase in `/api/get_led_runtime_status` for UI diagnostics.
4. Add a small unit/integration timing check for effect sequencing.

---

## Acceptance Criteria for Fix
1. In `Heartbeat` mode the visual sequence is clearly `duh-duh ... pause`.
2. In `Energy Flow` mode behavior remains unchanged.
3. No blocking delays introduced into renderer task loop.
4. No regressions in UI status endpoint fields (`current_effect_name`, `expected_effect_name`).

---

## Final Recommendation
Proceed with **Option A**:
- Implement 2-beat heartbeat directly in the receiver renderer non-blocking state machine.
- Use **Profile 1** timing (120 / 100 / 120 / 760 ms).
- Put all effect timings into a shared config struct.
- Remove old helper/legacy animation code after implementation so there is one authoritative runtime path.
