# espnowreceiver_LCD — Webserver Heap Failure: Root Cause Analysis and Fix Options

**Date:** 2026-05-19  
**Author:** Investigation via GitHub Copilot  
**Status:** Pending fix selection  
**Related:** `MQTT_ONLY_TRANSPORT_FEASIBILITY_2026_05_14.md`

---

## 1. Executive Summary

The webserver in `espnowreceiver_LCD` fails to start because free internal heap at init time (~30 KB) is
below the admission threshold of 32 KB (`kMinHeapForWebserverStartBytes`). The backoff loop retries
indefinitely but the heap never recovers to the required level.

**Primary cause:** ~16.5 KB of static `.bss` internal DRAM is consumed at link time by the MQTT chunk
reassembly buffers (`g_rx_slots[2]`). This allocation is unconditional, permanent, and currently uses
internal RAM despite the board having 8 MB of idle PSRAM.

**Historical note:** A comment in `task_config.h` states task stacks are *"sized for PSRAM allocation
(see Phase B notes)"* — PSRAM stack placement was planned but **never implemented**. No code in the
entire `espnowreceiver_LCD` project calls `heap_caps_malloc`, `ps_malloc`, or uses
`MALLOC_CAP_SPIRAM`. The PSRAM hardware is initialised (via `BOARD_HAS_PSRAM` and
`board_build.arduino.memory_type = qio_opi`) but the application makes zero use of it.

---

## 2. Memory Budget at Webserver Init Time

### 2.1 Internal DRAM consumers (measured / calculated)

| Source | Internal DRAM Cost | Notes |
|--------|--------------------|-------|
| `g_rx_slots[2]` (ChunkSlot static `.bss`) | **~16,464 bytes** | Allocated at link time, never freed |
| WiFi TX buffer pool (`DYNAMIC_TX_BUFFER_NUM=64`) | **~100 KB** | WiFi driver internal; 64 × ~1.6 KB DMA buffers |
| `task_lvgl` FreeRTOS stack | 4,096 bytes | `xTaskCreatePinnedToCore`, core 1 |
| `MqttLaunch` task stack | 3,072 bytes | Hardcoded in `runtime_task_startup.cpp` |
| `MqttClient` task stack | 6,144 bytes | `MQTT_CLIENT_STACK` constant |
| `ws_init` webserver startup task | 8,192 bytes | `xTaskCreatePinnedToCore` in `main.cpp` |
| httpd server internal task + socket buffers | ~12,288+ bytes | Allocated *after* heap gate — never reached |
| LovyanGFX / LVGL frame buffers | variable | Depends on display config; likely large |
| PubSubClient rx/tx buffer | ~256–6144 bytes | Default 256 unless `setBufferSize` called |
| FreeRTOS idle/timer tasks | ~2,048 bytes | System |

**Resulting free internal heap at webserver init time: ~30 KB (observed: 30,096 – 30,148 bytes)**

### 2.2 The shortfall

```
32,768  (kMinHeapForWebserverStartBytes threshold)
- 30,096 (measured free heap at init time)
= ~2,672 bytes below threshold
```

The margin is very tight. Freeing or re-routing even the `g_rx_slots` buffers alone (16.4 KB saved)
would put the system comfortably above threshold.

---

## 3. Key File Locations

| File | Relevant Content |
|------|-----------------|
| `espnowreceiver_LCD/lib/webserver_lcd/webserver.cpp` line 57 | `kMinHeapForWebserverStartBytes = 32U * 1024U` |
| `espnowreceiver_LCD/lib/webserver_lcd/webserver.cpp` lines 176–191 | Heap gate + backoff logic |
| `espnowreceiver_LCD/src/mqtt/mqtt_client.cpp` lines 79–95 | Chunk buffer constants + `g_rx_slots[2]` static |
| `espnowreceiver_LCD/include/task_config.h` | All task stack size constants |
| `espnowreceiver_LCD/platformio.ini` line 55 | `build_src_filter` (ESP-NOW runtime excluded) |
| `espnowreceiver_LCD/sdkconfig.defaults` | WiFi TX buffer count; **no PSRAM config** |
| `espnowreceiver_LCD/boards/waveshare_esp32s3_n16r8.json` | 8 MB PSRAM, OPI interface confirmed |

---

## 4. Why `g_rx_slots` Is In Internal RAM

The buffers are declared as static globals in an anonymous namespace:

```cpp
// mqtt_client.cpp lines 79–95
constexpr int      kRxReassemblyTimeoutMs = 3000;
constexpr int      kRxMaxSlots            = 2;
constexpr int      kRxChunkPartLen        = 1024;
constexpr int      kRxMaxParts            = 8;
constexpr int      kRxAssemblyBufLen      = kRxMaxParts * kRxChunkPartLen + 1; // 8193

struct ChunkSlot {
    bool     active;
    char     chunk_id[24];
    uint32_t start_ms;
    uint16_t total_parts;
    uint8_t  parts_mask;
    size_t   assembled_len;
    char     buf[kRxAssemblyBufLen];  // 8193 bytes each
};

static ChunkSlot g_rx_slots[kRxMaxSlots]{};  // 2 × ~8232 = ~16,464 bytes in .bss
```

Static globals go into `.bss` (zero-initialised) or `.data` at link time. The linker places `.bss`
in internal DRAM by default. Unless the symbol is explicitly routed via `heap_caps_malloc` or a
linker attribute like `__attribute__((section(".ext_ram.bss")))`, it stays in internal RAM.

The ESP32-S3 on this board has **8 MB of idle PSRAM** — none of which is consumed by the application.

---

## 5. Comparison with espnowreceiver_2 (TFT Receiver)

The TFT receiver (`espnowreceiver_2/src/mqtt/mqtt_client.cpp`) does **not** have `g_rx_slots` or
any static chunk reassembly array. Its MQTT client uses dynamic (heap) allocations only. This is why
the TFT receiver does not exhibit the same webserver startup failure — it has ~16 KB more free
internal heap at startup.

**Neither** receiver uses PSRAM for any allocation. Both define `BOARD_HAS_PSRAM` and set
`board_build.arduino.memory_type = qio_opi`, but neither calls `heap_caps_malloc(…, MALLOC_CAP_SPIRAM)`.

---

## 6. Fix Options (Ordered by Risk / Invasiveness)

The following options are not mutually exclusive. The **recommended approach** is to apply Options A + B
together and then verify empirically. Option C is a safety backstop if the threshold is confirmed too
conservative after the memory savings.

---

### Option A — Move `g_rx_slots` to PSRAM (Highest Impact, Moderate Effort)

**Saves: ~16,464 bytes of internal DRAM**

Change `g_rx_slots` from a static `.bss` array to a heap-allocated pointer initialised once at
program start using `heap_caps_malloc` targeting PSRAM.

**Step 1:** In `sdkconfig.defaults`, ensure PSRAM malloc is enabled:

```ini
# sdkconfig.defaults (add if not present)
CONFIG_SPIRAM_USE_CAPS_ALLOC=y
# or for global malloc fallback (higher risk, wider change):
# CONFIG_SPIRAM_USE_MALLOC=y
```

`CONFIG_SPIRAM_USE_CAPS_ALLOC` routes only explicit `heap_caps_malloc(…, MALLOC_CAP_SPIRAM)` calls
to PSRAM — it does not affect normal `malloc()` calls, making it safer.

**Step 2:** In `mqtt_client.cpp`, replace the static array with a pointer:

```cpp
// Before:
static ChunkSlot g_rx_slots[kRxMaxSlots]{};

// After:
static ChunkSlot* g_rx_slots = nullptr;

// Add an init function called once from MqttClient constructor or init():
static void init_rx_slots() {
    if (g_rx_slots != nullptr) return;
    g_rx_slots = static_cast<ChunkSlot*>(
        heap_caps_malloc(sizeof(ChunkSlot) * kRxMaxSlots, MALLOC_CAP_SPIRAM)
    );
    if (g_rx_slots) {
        memset(g_rx_slots, 0, sizeof(ChunkSlot) * kRxMaxSlots);
    } else {
        LOG_ERROR("MQTT", "Failed to allocate chunk slots in PSRAM — falling back to internal");
        // fallback: allocate from internal heap (still better than .bss at link time
        // because it happens after heap has been measured, not before)
        g_rx_slots = new ChunkSlot[kRxMaxSlots]{};
    }
}
```

All existing code that reads/writes `g_rx_slots[i]` continues to work via pointer access.

**Risk:** Low. PSRAM access is slightly slower than DRAM (~80 ns vs ~2 ns cache hit), but chunk
reassembly is not on a real-time path — it processes infrequent MQTT messages. The fallback ensures
the system still works if PSRAM allocation fails.

---

### Option B — Reduce Chunk Buffer Size (Lowest Risk, Immediate Partial Relief)

**Saves: up to ~8,232 bytes of internal DRAM (if kRxMaxParts halved)**

The current configuration allocates `kRxMaxParts = 8` parts × 1024 bytes = 8 KB per slot. Per the
MQTT feasibility study (Section 17.4), the design permits up to 8 parts. However, practical cell_data
and event_log payloads may not require all 8. Reducing to 4 parts halves the buffer:

```cpp
// Current:
constexpr int kRxMaxParts       = 8;
constexpr int kRxAssemblyBufLen = kRxMaxParts * kRxChunkPartLen + 1;  // 8193

// After (Option B only):
constexpr int kRxMaxParts       = 4;
constexpr int kRxAssemblyBufLen = kRxMaxParts * kRxChunkPartLen + 1;  // 4097
```

This reduces `g_rx_slots` from ~16.4 KB to ~8.4 KB — saving ~8 KB.

**Caveat:** If any transmitter payload currently requires > 4 chunks (i.e., > 4 KB of assembled data),
reassembly would silently fail. Before applying this, verify the maximum actual chunk count by checking
the transmitter's `ESPnowtransmitter2/espnowtransmitter2/src/mqtt/` chunking code.

**Recommendation:** Combine with Option A. If buffers are in PSRAM (Option A), kRxMaxParts can
remain at 8 with no internal RAM cost.

---

### Option C — Lower the Heap Admission Threshold (Fastest, Lowest Effort)

**No RAM savings — adjusts the guard rail only**

The 32 KB threshold was set when ESP-NOW was still running (adding significant heap pressure). With
ESP-NOW removed, 32 KB may be over-conservative.

```cpp
// webserver.cpp line 57 — current:
constexpr uint32_t kMinHeapForWebserverStartBytes = 32U * 1024U;

// Option C: lower to 20 KB
constexpr uint32_t kMinHeapForWebserverStartBytes = 20U * 1024U;
```

**Risk:** `httpd_start()` itself and the first HTTP session allocate several KB from internal heap.
If the threshold is set too low and `httpd_start()` fails internally, the webserver will be in a
half-started state with no clear error. Empirical testing with `esp_get_free_heap_size()` **before
and after** `httpd_start()` is needed to validate any revised threshold.

**Recommendation:** Do NOT apply this as the sole fix. Use it as a follow-up validation step after
Option A to determine the true minimum safe threshold empirically.

---

### Option D — Move Task Stacks to PSRAM (Highest Impact, Highest Effort)

**Saves: up to ~21.5 KB of internal DRAM (all FreeRTOS task stacks)**

`task_config.h` already contains the comment: *"Stack sizes are sized for PSRAM allocation (see Phase B
notes)"* — meaning this was a planned but never-implemented option.

On ESP32-S3, FreeRTOS task stacks can be placed in PSRAM by:

1. Setting `CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y` in `sdkconfig.defaults`
2. Using `xTaskCreatePinnedToCoreWithCaps` (IDF 5.x) or `heap_caps_malloc` for stack memory +
   `xTaskCreateStaticPinnedToCore` with externally-allocated TCB and stack

This is a significant refactor of `runtime_task_startup.cpp` and is **not recommended as the first
step** — but is documented here because `task_config.h` indicates it was planned.

---

### Option E — Reduce WiFi TX Buffer Count (Large Savings, Low Risk)

**Saves: ~50–80 KB of internal DRAM**

`sdkconfig.defaults` currently sets `CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=64`. The default is 32.
Each buffer is approximately 1.6 KB, so:

- Current: 64 × 1.6 KB = **~102 KB**
- Reduced to 32: **~51 KB** (saves ~50 KB)
- Reduced to 16: **~26 KB** (saves ~76 KB, may reduce throughput for burst traffic)

```ini
# sdkconfig.defaults — change:
CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=32
```

**Risk:** Reducing TX buffers can reduce WiFi throughput under burst conditions (e.g., large OTA
updates or rapid HTTP responses). For normal MQTT + webserver + LVGL operation at ~1 Mbps, 32 buffers
is typically sufficient. 16 may cause occasional retransmits. 32 is a safe conservative reduction.

**This is likely the highest single-change impact on the heap budget** and requires no code changes
— only a sdkconfig.defaults line.

---

## 7. Recommended Fix Sequence

Apply in this order, building and measuring free heap after each step:

### Step 1 — Apply Option E (sdkconfig.defaults WiFi buffer reduction)

Change `CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM` from `64` → `32` in `sdkconfig.defaults`.

Expected: ~50 KB saved in internal heap. This alone may be sufficient to get free heap above
threshold, but does not fix the underlying root cause.

### Step 2 — Apply Option A (Move `g_rx_slots` to PSRAM)

Add `CONFIG_SPIRAM_USE_CAPS_ALLOC=y` to `sdkconfig.defaults`.  
Change `g_rx_slots` from static array to PSRAM-allocated pointer in `mqtt_client.cpp`.

Expected: ~16.5 KB freed from internal DRAM permanently.

### Step 3 — Verify and calibrate Option C (Threshold review)

After Steps 1 and 2, add a log statement immediately before and after the webserver `httpd_start()`
call to record the actual heap consumption of the HTTP server itself. Use this to set a data-driven
threshold — likely 16–20 KB rather than 32 KB given the new memory profile.

### Step 4 — Optional: Apply Option D (Task stacks to PSRAM)

If further internal DRAM headroom is needed (e.g., for future features), implement PSRAM task stack
allocation as planned in `task_config.h` Phase B notes.

---

## 8. What Was NOT the Cause

For completeness:

- **ESP-NOW runtime** — confirmed NOT running. No `esp_now_init()` in the build. No ESP-NOW worker
  or TX scheduler tasks are created. `build_src_filter = -<espnow/espnow_runtime*.cpp>` is effective.
  Some ESP-NOW header includes remain in `webserver.cpp` (lines 11–12) but contribute zero runtime cost.

- **PSRAM hardware fault** — PSRAM is physically present and correctly configured in the board JSON
  (`qio_opi` interface, 8 MB). The issue is that the application never uses it.

- **MQTT chunk buffer count** — reducing from 2 slots to 1 saves only ~8.2 KB and is not sufficient
  alone to cross the threshold.

---

## 9. Verification Checklist (Post-Fix)

Before closing this issue, confirm all of the following:

- [ ] `esp_get_free_heap_size()` >= 32 KB (or new calibrated threshold) at webserver init time
- [ ] `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)` after `init_rx_slots()` shows expected reduction
- [ ] Webserver starts on first attempt after boot (no backoff retries in serial log)
- [ ] MQTT chunk reassembly for `cell_data` and `event_logs` still works correctly
- [ ] WebSocket / SSE sessions function normally under 30-minute mixed soak
- [ ] OTA update completes successfully (WiFi TX buffer reduction has no adverse effect)
- [ ] No crash or assertion in PSRAM allocation path

---

## 10. Open Questions

1. **What is the actual maximum assembled payload size from the transmitter for `cell_data` and
   `event_logs`?** This determines whether `kRxMaxParts=8` (8 KB max) is truly needed or can be
   reduced to 4. Check `ESPnowtransmitter2/espnowtransmitter2/src/mqtt/` for the chunking send code.

2. **Was PSRAM allocation ever working in a prior build?** The `task_config.h` Phase B note implies
   it was planned but serial logs and code confirm it was never implemented. If a prior build had
   a different `mqtt_client.cpp` with `ps_malloc`, that is not present in the current branch.

3. **Is `CONFIG_SPIRAM_USE_CAPS_ALLOC` already set in any IDF/Arduino component default for this
   board?** The board package (`waveshare_esp32s3_n16r8`) may include a default sdkconfig that enables
   PSRAM caps — check the PlatformIO cache for this board's default sdkconfig before adding it
   manually to avoid conflicts.
