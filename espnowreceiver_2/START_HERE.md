# Display Architecture Redesign - Start Here! 🎯

# espnowreceiver_2 — Start Here

ESP-NOW battery telemetry receiver for **LilyGo T-Display-S3** (ESP32-S3, 16 MB flash, 8 MB PSRAM).

This project is the **production-ready** receiver. For the 7-inch Waveshare display variant, see `espnowreceiver_LCD`.

---

## Essential reading order

1. **`PROJECT_ARCHITECTURE_MASTER.md`** — canonical task map, data flow, and module ownership
2. **`CHANGES_QUICK_SUMMARY.txt`** — chronological changelog (read from bottom up for recent changes)
3. **`ARCHITECTURE_REDESIGN.md`** — historical context on the display path refactor (now stable; read for background only)

---

## What this firmware does

- Receives battery telemetry from an ESP-NOW transmitter (ESPnowtransmitter2)
- Renders SOC%, power, and link state on the T-Display-S3 screen
- Hosts a web dashboard for configuration and live monitoring via SSE
- Forwards MQTT topics to a broker when configured
- Supports OTA firmware updates over WiFi

---

## Build and flash

```bash
# Build
pio run -j 2

# Flash firmware
pio run --target upload --environment lilygo_t_display_s3

# Flash LittleFS (first flash or after erase)
pio run --target uploadfs --environment lilygo_t_display_s3
```

---

## Key architecture constraints

- **Dual-core:** ESP-NOW worker on APP_CPU; HTTP/SSE on PRO_CPU via AsyncWebServer
- **Display mutex:** All LVGL/TFT access must be guarded — see `RTOS::lvgl_mutex`
- **Transmitter caches** (`lib/webserver/utils/transmitter_*.cpp`) — all protected by `ScopedMutex` as of 2026-04-21
- **MQTT task** — broker reconfiguration takes effect without reboot (config fingerprint, not one-shot flag)
- **OTA:** Dual-partition table (`partitions_16mb_ota.csv`); both slots must be valid before enabling rollback

---

## Project status

| Area | Status |
|---|---|
| ESP-NOW receive + telemetry decode | ✅ Production |
| Web dashboard + SSE | ✅ Production |
| MQTT client | ✅ Production |
| OTA | ✅ Production |
| Display render (TFT/LVGL) | ✅ Production |
| Concurrency hardening | ✅ Complete 2026-04-21 |
- Testing checklist

### 6. **DISPLAY_DOCS_INDEX.md** (OPTIONAL - REFERENCE)
**What**: Navigation guide, FAQ, document map
**Read**: As needed
**When**: If you need to find something specific

---

## 🚀 QUICKEST PATH (30 minutes to coding)

```
1. Read this file (SESSION_SUMMARY.md) → 5 minutes
   └─ You are here

2. Read DISPLAY_QUICK_REFERENCE.md → 10 minutes
   └─ Skim "Quick Facts" and "Implementation Checklist"

3. Read TFT_IMPLEMENTATION_GUIDE.md Steps 1-3 → 15 minutes
   └─ Read about init_hardware(), init_backlight(), set_backlight()

4. Start coding → 2-4 hours
   └─ Follow guide step-by-step
   └─ Keep guide open in another window
   └─ Refer to DISPLAY_QUICK_REFERENCE.md if stuck
```

---

## 📋 COMPLETE PATH (2 hours reading, then coding)

```
1. Read SESSION_SUMMARY.md → 5 minutes
2. Read DISPLAY_ARCHITECTURE_SUMMARY.md → 20 minutes
   └─ Understand the big picture
3. Read DISPLAY_QUICK_REFERENCE.md → 15 minutes
   └─ Get practical reference material
4. Read TFT_IMPLEMENTATION_GUIDE.md fully → 45 minutes
   └─ Understand all 9 steps before coding
5. Start coding → 2-4 hours
   └─ Follow guide with full understanding
   └─ Fewer surprises, smoother process
```

---

## ⚡ MINIMUM PATH (Just code it - 10 minutes reading)

```
1. Skim DISPLAY_QUICK_REFERENCE.md Quick Facts → 5 minutes
2. Skim DISPLAY_QUICK_REFERENCE.md Implementation Checklist → 5 minutes
3. Start TFT_IMPLEMENTATION_GUIDE.md Step 1 → Start coding now
   └─ Keep guide and reference open
   └─ Look things up as you go
```

---

## 🎯 CHOOSE YOUR PATH

### Path A: "I'm in a hurry" ⚡
→ Take the MINIMUM PATH above
→ You'll be coding in 10 minutes
→ Refer to guide as needed while coding

### Path B: "I want to understand everything" 📚
→ Take the COMPLETE PATH above
→ You'll understand the full picture
→ Smoother implementation with fewer surprises

### Path C: "I want to be super productive" 🚀
→ Take the QUICKEST PATH above
→ Balance of understanding and speed
→ 30 minutes reading then full steam ahead

---

## 💡 WHAT YOU NEED TO KNOW RIGHT NOW

### The Big Picture
- Display system was monolithic (mixed TFT + LVGL)
- Now it's modular with compile-time selection
- You choose: `-DUSE_TFT` or `-DUSE_LVGL` at build time
- Only one implementation compiles into final binary

### What's Been Done
✅ Architecture designed
✅ Code structure created
✅ All method signatures defined
✅ Comprehensive documentation written
✅ Step-by-step implementation guide created

### What You Need to Do
1. Implement 12 methods in TFT class
2. Test on hardware
3. Create LVGL parallel implementation (later)
4. Test both versions

### Time Investment
- TFT implementation: 2-4 hours
- TFT testing: 1 hour
- LVGL implementation: 3-5 hours
- LVGL testing: 1-2 hours
- Integration: 1-2 hours
- **Total**: 9-15 hours

---

## 📁 FILES YOU'LL BE WORKING WITH

### Main Architecture Files
- `src/display/display_interface.h` - Abstract interface
- `src/display/tft_impl/tft_display.h` - TFT class declaration
- `src/display/tft_impl/tft_display.cpp` - TFT implementation (YOUR WORK)
- `src/display/display.cpp` - Dispatcher

### Documentation Files
- `DISPLAY_QUICK_REFERENCE.md` - KEEP THIS OPEN WHILE CODING
- `TFT_IMPLEMENTATION_GUIDE.md` - FOLLOW THIS STEP BY STEP
- `DISPLAY_ARCHITECTURE_SUMMARY.md` - Reference for context
- `DISPLAY_ARCHITECTURE_PROGRESS.md` - Reference for details
- `DISPLAY_DOCS_INDEX.md` - Navigation guide

---

## ✅ IMPLEMENTATION CHECKLIST

The TFT implementation has 12 methods to implement. They appear in order of dependency:

```
[ ] Step 1: init() - Wrapper method (10 min)
[ ] Step 2: init_hardware() - TFT_eSPI setup (20 min)
[ ] Step 3: init_backlight() - PWM configuration (20 min)
[ ] Step 4: set_backlight() - PWM write (15 min)
[ ] Step 5: animate_backlight() - Fade animation (30 min)
[ ] Step 6: load_and_draw_splash() - JPEG decode (45 min)
[ ] Step 7: display_splash_with_fade() - Orchestrate (20 min)
[ ] Step 8: display_initial_screen() - Ready message (15 min)
[ ] Step 9: update_soc() & draw_soc() - Text display (20 min)
[ ] Step 10: update_power() & draw_power() - Text display (20 min)
[ ] Step 11: show_status_page() - Main layout (30 min)
[ ] Step 12: Error displays - Red screens (20 min)

Total: ~4 hours
```

---

## 🔧 NEXT IMMEDIATE STEPS

### Right Now (Next 5-10 minutes)
1. ✅ Read this file (you are doing it)
2. Open DISPLAY_QUICK_REFERENCE.md in a new window
3. Scan the Quick Facts table
4. You're ready to start!

### In 15 Minutes
5. Open TFT_IMPLEMENTATION_GUIDE.md
6. Read Steps 1-3
7. You're ready to code!

### In 30 Minutes
8. Start Step 1: init()
9. Follow the guide
10. Code, compile, test

---

## 🚨 IF YOU GET STUCK

### Compilation Error?
→ Check DISPLAY_QUICK_REFERENCE.md - Common Errors section

### Don't know what to code?
→ Check TFT_IMPLEMENTATION_GUIDE.md - The specific step

### Want to understand why?
→ Check DISPLAY_ARCHITECTURE_PROGRESS.md - Design Decisions

### Need a code example?
→ Check TFT_IMPLEMENTATION_GUIDE.md - Code snippet in the step

### Hardware won't work?
→ Check DISPLAY_ARCHITECTURE_PROGRESS.md - Testing Checklist

---

## 📊 PROJECT STATUS

| Phase | Task | Status |
|-------|------|--------|
| **1-2** | Architecture & docs | ✅ COMPLETE |
| **3** | TFT implementation | ⏳ YOUR WORK |
| **4** | TFT testing | 📅 AFTER PHASE 3 |
| **5** | LVGL implementation | 📅 AFTER PHASE 4 |
| **6** | Build variants | 📅 AFTER PHASE 5 |
| **7** | Integration | 📅 AFTER PHASE 6 |

---

## 💼 PROJECT SCOPE

### What's Included
✅ Complete architecture design
✅ TFT skeleton (stubs)
✅ LVGL skeleton (not implemented)
✅ Dispatcher system
✅ Documentation
✅ Implementation guide
✅ Testing plan

### What's Not Included (Future)
- ePaper support
- OLED support
- Theme system
- Advanced animations
- Touchscreen support
- Cloud synchronization

---

## 🎓 KEY CONCEPTS (Quick)

**Compile-time selection**: Choose between TFT and LVGL at build time using `-DUSE_TFT` or `-DUSE_LVGL`

**IDisplay interface**: Abstract base class that both TFT and LVGL implement. Guarantees both have the same methods.

**Dispatcher**: `display.cpp` handles the selection and provides a global instance `Display::g_display`

**Implementation isolation**: TFT code never mixes with LVGL code. They're completely separate.

**Binary optimization**: Final binary only contains the implementation you selected. No bloat from unused code.

---

## 📞 QUICK ANSWERS

| Question | Answer |
|----------|--------|
| **Where do I start?** | Read DISPLAY_QUICK_REFERENCE.md then TFT_IMPLEMENTATION_GUIDE.md |
| **How much time?** | 2-4 hours for TFT + 1 hour testing + 3-5 hours LVGL |
| **How many files?** | Modify 1 main file: tft_display.cpp |
| **What do I implement?** | 12 methods following the guide |
| **How do I test?** | Follow DISPLAY_ARCHITECTURE_PROGRESS.md - Testing Checklist |
| **What if I'm stuck?** | Check DISPLAY_QUICK_REFERENCE.md - Common Errors |
| **How do I switch to LVGL?** | Create mirror implementation in lvgl_impl/ folder |
| **Can I reference existing code?** | Yes! TFT_IMPLEMENTATION_GUIDE.md tells you which files |

---

## 🎉 YOU ARE READY!

Everything you need has been prepared:

✅ Architecture is designed
✅ Code structure is created
✅ Documentation is written
✅ Implementation guide is ready
✅ Code examples are provided
✅ Testing plan is created
✅ Success criteria are defined

**All that's left is for you to follow the guide and code it.**

---

## 🚀 READY? HERE'S WHAT TO DO NOW

1. **Open**: DISPLAY_QUICK_REFERENCE.md in a new window
2. **Read**: Quick Facts table (2 min)
3. **Then open**: TFT_IMPLEMENTATION_GUIDE.md
4. **Read**: Steps 1-3 (15 min)
5. **Start**: Step 1 implementation (following the guide)
6. **Keep open**: Both guides and reference source files
7. **Refer back**: When you get stuck or have questions

---

## 📝 GOOD LUCK! 

The architecture is solid. The plan is clear. The documentation is comprehensive.

**You've got this.** 💪

---

**Start with**: DISPLAY_QUICK_REFERENCE.md (next file to read)

**Then follow**: TFT_IMPLEMENTATION_GUIDE.md (step-by-step coding)

**Keep open**: Both of the above + your editor + reference source files

**Total time to completion**: 9-15 hours for full implementation

**First step**: Read DISPLAY_QUICK_REFERENCE.md right now
