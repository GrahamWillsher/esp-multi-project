# Receiver Splash Screen Fade Rework Request

## Problem Statement

The receiver splash screen image:

- `C:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_2\data\BatteryEmulator4_320x170.jpg`

does not fade smoothly. There is visible **jumping/stepping**:

- at the **end of fade-in**
- at the **start of fade-out**

## Required Outcome

Re-implement splash fade behavior as a **new, separate function**, replacing only the fade execution path (not the fade duration policy).

## Hard Constraints

1. Fade timings must remain exactly aligned with the current implementation.
2. Only rewrite the mechanism used to apply fade transitions.
3. New implementation must be isolated from legacy fade logic (separate function/path).
4. No in-place “small tweak” approach; treat this as a clean replacement of the fade routine.

## Investigation Requirements (Before Coding)

Before any code change, investigate:

1. LILYGO T-Display-S3 hardware/driver considerations:
	- https://lilygo.cc/products/t-display-s3
2. Relevant LILYGO GitHub repositories and display/backlight guidance.
3. Prior project history where smooth fade behavior previously worked.

## Mandatory Pre-Code Deliverable

Create a full analysis document under:

- `esp32common/docs/systemworks/`

The report must include:

1. **Current implementation issues**
	- root-cause candidates for fade jump artifacts
	- timing, PWM, TFT render path interactions, and buffer/update order concerns

2. **Findings from external references**
	- device-specific constraints
	- best-practice fade sequencing for this display/backlight stack

3. **Proposed new design**
	- architecture of the replacement fade function
	- why it resolves observed jump points
	- compatibility with current startup flow

4. **Validation plan**
	- visual pass/fail criteria
	- repeatability checks across multiple boots
	- regression checks (timings unchanged)

## Acceptance Criteria

The rework is accepted only when:

1. Fade-in and fade-out are visually smooth (no terminal jump artifacts).
2. Existing fade durations are preserved.
3. Implementation is encapsulated in a new dedicated function/path.
4. Analysis report is completed before coding begins.


