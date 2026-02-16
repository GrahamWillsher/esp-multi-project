# Logging Migration Recommendations (Tagged API)

## Summary
You want a **single, consistent, tagged logging API** across transmitter and receiver, with **all legacy logging removed**. The tagged API is:

```
LOG_INFO("TAG", "format", ...)
```

This document outlines the recommended **single source of truth** for logging and a migration path that removes all legacy usage.

---

## ✅ Current Situation
### Transmitter (working model)
Transmitter uses a single logging header:
- [ESPnowtransmitter2/espnowtransmitter2/src/config/logging_config.h](ESPnowtransmitter2/espnowtransmitter2/src/config/logging_config.h)

It provides:
- Tagged macros (`LOG_INFO(tag, fmt, ...)`)
- Optional MQTT logging integration

### Receiver (migration needed)
Receiver currently mixes:
- Old 1-argument logging calls (e.g. `LOG_INFO("foo")`)
- New tagged logging in some files
- Custom logging macros in [espnowreciever_2/src/common.h](espnowreciever_2/src/common.h) (now removed)

This causes:
- Build errors when using 3-arg macro signatures
- Inconsistent tag formatting
- Duplication across project

---

## ✅ Target End State
- **Single logging header** per project or shared across both
- Tagged logging used consistently in *all* source files
- **No legacy logging macros or one-parameter usage**
- No `src_filter` exclusions required to build

---

## Recommended Approach (Best Practice)
### 1) Make a **single shared logging header** for both transmitter and receiver
Place in shared repo and include from both projects:

Suggested location:
- [esp32common/logging_utilities/logging_config.h](esp32common/logging_utilities/logging_config.h)

Then replace receiver and transmitter logging header includes to use the shared header.

**Why:** Avoids divergence between projects and keeps the API centralized.

---

### 2) Provide **single tagged macro API only**
In the shared header:

**Tagged API (required):**
```
LOG_INFO("TAG", "message %d", value)
```

**Do not provide legacy adapters**. This enforces consistent usage and prevents regressions.

---

### 3) Remove all file-scoped wrappers
Do **not** use temporary wrappers for legacy calls. Convert all call sites directly to the tagged API.

---

### 4) Full migration via **multi-replace** per file
Convert all calls like:
```
LOG_INFO("text")
LOG_WARN("text %d", x)
LOG_ERROR("error")
```

to tagged calls:
```
LOG_INFO("MODULE", "text")
LOG_WARN("MODULE", "text %d", x)
LOG_ERROR("MODULE", "error")
```

Use file-specific tags (`MAIN`, `ESPNOW`, `STATE`, `DISPLAY`, `CONFIG`, `BATTERY`, etc.).

---

## Specific Receiver Issues Found
### a) Legacy call sites
Legacy logging still exists in:
- [espnowreciever_2/src/espnow/espnow_tasks.cpp](espnowreciever_2/src/espnow/espnow_tasks.cpp)
- [espnowreciever_2/src/espnow/battery_handlers.cpp](espnowreciever_2/src/espnow/battery_handlers.cpp)
- [espnowreciever_2/src/display/display_splash.cpp](espnowreciever_2/src/display/display_splash.cpp)
- [espnowreciever_2/src/test/test_data.cpp](espnowreciever_2/src/test/test_data.cpp)
- [espnowreciever_2/src/state_machine.cpp](espnowreciever_2/src/state_machine.cpp)
- [espnowreciever_2/src/config/config_receiver.cpp](espnowreciever_2/src/config/config_receiver.cpp)

These require full conversion to tagged format.

### b) `src_filter` exclusion
The old `src_filter` exclusion in [espnowreciever_2/platformio.ini](espnowreciever_2/platformio.ini) was masking compile issues.
Now it should be removed once logging is unified (current build should not require it).

---

## Recommended Tags by Module
| File/Module | Suggested Tag |
|------------|----------------|
| main.cpp | MAIN |
| state_machine.cpp | STATE |
| espnow_tasks.cpp | ESPNOW |
| battery_handlers.cpp | BATTERY |
| config_receiver.cpp | CONFIG |
| display_splash.cpp | DISPLAY |
| test_data.cpp | TEST |

---

## Improvements for Single Source Logging
1. **Move logging config to shared header** in esp32common so both projects include the same file.
2. **Keep tagged API as the only API** (no legacy adapters).
3. **Require tag usage in new code** (add lint note or review checklist).
4. **Avoid duplicate `LOG_*` definitions** in shared ESP-NOW utilities (already addressed in transmitter).

---

## Suggested Migration Steps
1. Create shared logging header in esp32common.
2. Update transmitter/receiver to include shared header only.
3. Convert each file to tagged logging (multi-replace).
4. Delete any old logging macro definitions and any legacy wrappers.

---

## Automated Migration Template
For each file:
1. Pick tag `TAG`
2. Replace:
   - `LOG_INFO("` → `LOG_INFO("TAG", "`
   - `LOG_WARN("` → `LOG_WARN("TAG", "`
   - `LOG_ERROR("` → `LOG_ERROR("TAG", "`
   - `LOG_DEBUG("` → `LOG_DEBUG("TAG", "`
   - `LOG_TRACE("` → `LOG_TRACE("TAG", "`
3. Fix any format strings that already embed `[TAG]` in text

---

## Next Actions (if approved)
I can:
- Create a shared logging header in esp32common
- Convert all receiver and transmitter files to tagged logging via multi-replace
- Remove all legacy macros and any wrappers
- Run full builds for transmitter + receiver

---

If you want me to proceed, say the word and I’ll implement the migration.
