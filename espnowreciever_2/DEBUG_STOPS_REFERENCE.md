# Debug Stops Reference - Splash Screen Sequence

**Added:** March 5, 2026  
**Purpose:** Provide visibility into each phase of the LVGL splash screen display sequence to verify the white block fix is working correctly

---

## Debug Stops Overview

The splash screen sequence has been instrumented with **16 strategic debug stops** (DEBUG STOP 1-16) that pause for 300-500ms at critical points. These pauses allow visual inspection of the display at each phase.

---

## Debug Stop Locations & Timeline

| # | Location | Timing | Duration | Purpose | Expected Display |
|---|----------|--------|----------|---------|------------------|
| **1** | Screen load setup | 0ms | 500ms | About to load splash to LVGL | Dark/transitioning |
| **2** | After lv_scr_load() | 500ms | 500ms | Screen loaded, about to pump LVGL | Still dark |
| **3** | Before LVGL refresh | 1000ms | Instant | Pumping LVGL timer & refresh | May start showing |
| **4** | Before DISPON command | 1100ms | 500ms | Check for white block BEFORE panel enable | Black or artifact? |
| **5** | Sending DISPON(0x29) | 1600ms | Instant | Sending display ON command | Panel enabling |
| **6** | After DISPON | 1600ms | 500ms | Panel is now ON - check for white block | **KEY: Should be BLACK, not white!** |
| **7** | Before backlight fade | 2100ms | 300ms | Finalizing before backlight ramp | Screen settling |
| **8** | Backlight ramp complete | 2400ms | 500ms | Backlight at full brightness | **KEY: No white block should exist** |
| **9** | Before fade-in animation | 2900ms | 300ms | About to start fade-in | Opaque black image |
| **10** | Fade-in running | 3200ms | (5000ms animation) | Fade-in animation in progress | Stripes gradually appearing |
| **11** | Fade-in complete | 8200ms | 500ms | Stripes fully visible | **KEY: Full color stripes visible** |
| **12** | Hold phase starting | 8700ms | (2000ms hold) | Splash displayed for 2 seconds | Steady stripes display |
| **13** | Before fade-out | 10700ms | 300ms | About to start fade-out | Stripes fading... |
| **14** | Fade-out running | 11000ms | (5000ms animation) | Fade-out animation | Stripes gradually disappearing |
| **15** | Fade-out complete | 16000ms | 500ms | Screen black, splash gone | Clean black screen |
| **16** | Ready screen loaded | 16500ms | 500ms | Main display is now active | Ready screen visible |

---

## Phase Timeline (Approximate)

```
0ms     ├─ DEBUG STOP 1: Screen load setup
500ms   ├─ DEBUG STOP 2: Screen loaded
1000ms  ├─ DEBUG STOP 3: LVGL refresh
1100ms  ├─ DEBUG STOP 4: Before DISPON ←── CRITICAL OBSERVATION POINT 1
1600ms  ├─ DEBUG STOP 5-6: DISPON command ←── CRITICAL OBSERVATION POINT 2
2100ms  ├─ DEBUG STOP 7: Before backlight
2400ms  ├─ DEBUG STOP 8: Backlight ready ←── KEY VERIFICATION POINT
2900ms  ├─ DEBUG STOP 9: Before fade-in
3200ms  ├─ DEBUG STOP 10: Fade-in running (0→255 opacity over 5s)
8200ms  ├─ DEBUG STOP 11: Fade-in complete ←── STRIPES SHOULD BE VISIBLE
8700ms  ├─ DEBUG STOP 12: Hold phase (display 2s)
10700ms ├─ DEBUG STOP 13: Before fade-out
11000ms ├─ DEBUG STOP 14: Fade-out running (255→0 opacity over 5s)
16000ms ├─ DEBUG STOP 15: Fade-out complete
16500ms └─ DEBUG STOP 16: Ready screen loaded
```

---

## Critical Observation Points

### 🔴 CRITICAL: DEBUG STOP 6 - Panel ON (After DISPON)
**Timing:** ~1600ms into sequence  
**What to Look For:** 
- ✅ **EXPECTED:** Black screen (clean, uniform black)
- ❌ **PROBLEM:** White block, garbage content, noise pattern, colored artifacts
- ⚠️ **ISSUE:** If white block appears here, the DISPOFF/DISPON fix isn't working

**What This Tells You:**
- If BLACK: The DISPOFF/DISPON sequencing is working correctly
- If WHITE BLOCK: LCD panel is showing uninitialized GRAM (display controller issue)

---

### 🟢 CRITICAL: DEBUG STOP 8 - Backlight Ready
**Timing:** ~2400ms into sequence  
**What to Look For:**
- ✅ **EXPECTED:** Screen gradually brightening, ending at full brightness
- ❌ **PROBLEM:** Sudden jump to brightness, flicker, or darkness
- ⚠️ **ISSUE:** If backlight doesn't fade smoothly, the gradient fade needs adjustment

**What This Tells You:**
- If SMOOTH FADE: Backlight PWM gradient is working correctly
- If SUDDEN ON: Backlight PWM timing issue (try increasing 2ms delay)
- If DARK: Backlight PWM not enabled or GPIO misconfigured

---

### 🟡 CRITICAL: DEBUG STOP 11 - Fade-In Complete
**Timing:** ~8200ms into sequence  
**What to Look For:**
- ✅ **EXPECTED:** Full color stripes (Red, Green, Blue, Yellow, Cyan, Magenta)
- ❌ **PROBLEM:** Black screen, partial stripes, garbled colors, no image
- ⚠️ **ISSUE:** If no stripes visible, LVGL image rendering needs debugging

**What This Tells You:**
- If FULL STRIPES: LVGL rendering and animation system is working
- If BLACK: Image widget not rendering (check LVGL config)
- If GARBLED: Color format mismatch (check RGB565 endianness)

---

## Using These Debug Stops to Troubleshoot

### Scenario 1: "White Block Still Appears Before Splash"
1. **Monitor DEBUG STOP 4:** What does screen show BEFORE DISPON?
2. **Monitor DEBUG STOP 6:** What does screen show AFTER DISPON?
3. **If white appears at STOP 6:**
   - Issue is LCD panel showing garbage GRAM
   - Check: GPIO15 timing, DISPOFF/DISPON commands, ST7789 reset sequence
4. **If white appears BEFORE STOP 6 but not after:**
   - Issue is something else in initialization
   - Check: Backlight timing, LVGL bootstrap rendering

### Scenario 2: "Screen Stays Black, No Stripes"
1. **Monitor DEBUG STOP 11:** Should see full stripes here
2. **If still black:**
   - LVGL isn't rendering image
   - Check: Image buffer allocation, LVGL image descriptor, display driver registration
3. **If partially black:**
   - Opacity animation may be stuck
   - Check: LVGL animation system, lv_anim_count_running()

### Scenario 3: "Backlight Too Dim or Sudden"
1. **Monitor DEBUG STOP 8:** Should see smooth gradual brightness increase
2. **If sudden jump:**
   - PWM not ramping gradually
   - Increase `smart_delay(2)` to `smart_delay(5)` in backlight loop
3. **If too dim:**
   - Check final brightness value (should be 255)
   - Verify `ledcWrite()` is being called with 255

---

## Serial Output Format

When the firmware boots with these debug stops, you'll see output like:

```
[SPLASH] ✓ Splash screen loaded (DISPLAY SHOULD SHOW NOW)
[SPLASH] DEBUG STOP 1: About to load splash screen to LVGL display
[SPLASH] DEBUG STOP 2: Screen loaded, about to pump LVGL and render
[SPLASH] DEBUG STOP 3: Pumping LVGL timer handler and refreshing display...
[SPLASH] ✓ LVGL refresh complete
[SPLASH] DEBUG STOP 4: LVGL render complete, checking for white block before DISPON
[SPLASH] DEBUG STOP 5: Sending DISPON(0x29) command to ST7789
[SPLASH] ✓ Display panel enabled - black frame is visible
[SPLASH] DEBUG STOP 6: Panel is now ON. Check if white block appeared BEFORE this.
[SPLASH] DEBUG STOP 7: Starting backlight ramp (0->255 over ~500ms)
[SPLASH] ✓ Backlight ramp complete (0->255)
[SPLASH] DEBUG STOP 8: Backlight is now at full brightness. Check: no white block should be visible!
[SPLASH] --- PHASE 6: FADE-IN ANIMATION ---
[SPLASH] DEBUG STOP 9: About to start fade-in animation
[SPLASH] DEBUG: You should see stripes FADE IN gradually
[SPLASH] ✓ Fade-in animation STARTED
[SPLASH] DEBUG STOP 10: Fade-in animation is running (opacity: 0->255)
[SPLASH] ✓ Fade-in COMPLETE: X frames, Xms
[SPLASH] DEBUG STOP 11: Fade-in animation complete, stripes should be fully visible!
[SPLASH] --- PHASE 7: HOLD PHASE ---
[SPLASH] DEBUG STOP 12: Entering hold phase for 2000ms - OBSERVE STRIPES NOW
[SPLASH] ✓ Hold COMPLETE: X frames, Xms
[SPLASH] --- PHASE 8: FADE-OUT ANIMATION ---
[SPLASH] DEBUG STOP 13: About to start fade-out animation
[SPLASH] ✓ Fade-out animation STARTED
[SPLASH] DEBUG STOP 14: Fade-out animation is running (opacity: 255->0)
[SPLASH] ✓ Fade-out COMPLETE: X frames, Xms
[SPLASH] DEBUG STOP 15: Splash fade-out complete, loading Ready screen
[SPLASH] ✓ Ready screen loaded
[SPLASH] DEBUG STOP 16: Ready screen is now visible
[SPLASH] ╔════════════════════════════════════════════════════╗
[SPLASH] ║  === SPLASH SEQUENCE COMPLETE ===                 ║
[SPLASH] ║  ✓ Black background loaded (NO WHITE BLOCK!)      ║
[SPLASH] ║  ✓ Stripes displayed and faded                    ║
[SPLASH] ║  ✓ Ready screen loaded                            ║
[SPLASH] ║  ✓ System ready for operation                     ║
[SPLASH] ╚════════════════════════════════════════════════════╝
```

---

## Removing Debug Stops (Production Build)

To remove debug stops for final production build, simply replace the `smart_delay()` calls in [src/display/display_splash_lvgl.cpp](src/display/display_splash_lvgl.cpp):

```cpp
// Change this:
smart_delay(500);  // DEBUG STOP pause

// To this:
// smart_delay(500);  // DEBUG STOP - removed for production
```

Or define a compile-time flag:
```cpp
#ifdef DEBUG_SPLASH_STOPS
    smart_delay(500);
#endif
```

---

## Notes

- **Each debug stop pauses for 300-500ms** to allow visual inspection
- **Timing is approximate** - actual duration depends on system load
- **All debug stops are LOG_INFO level** - they'll show in serial monitor with log level INFO or higher
- **Performance impact:** ~5 seconds added to splash sequence during debugging (removed in production)

---

## Summary

These 16 debug stops create a "visual debugging" experience where you can observe:
1. ✅ Black background loads without white block
2. ✅ Panel initialization sequence completes correctly
3. ✅ Backlight fades smoothly
4. ✅ LVGL renders image stripes cleanly
5. ✅ Animations progress smoothly
6. ✅ Ready screen loads when splash completes

**Use these to verify the white block fix is working on your hardware!**

---

**Status:** Integrated into firmware and ready to test  
**Next:** Flash firmware and monitor boot sequence with these debug stops  
**Expected Result:** Observe each phase completing successfully, with NO white block artifact
