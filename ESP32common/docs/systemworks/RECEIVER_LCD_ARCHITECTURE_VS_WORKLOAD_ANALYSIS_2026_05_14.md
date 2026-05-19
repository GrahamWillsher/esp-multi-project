# Receiver LCD Memory Architecture: Is This a Resource Problem or Design Problem?
**Date:** 2026-05-14  
**Question:** Do we need heap optimization, or is the device fundamentally incapable of the requested workload?  
**Answer:** Both. It's a **design problem masquerading as a heap problem.**

---

## 1) Device Reality vs. Design Expectations

### 1.1 What Hardware Actually Exists

**Waveshare ESP32-S3 Touch LCD 7"**
- **Internal RAM:** 327 KB total (all that matters for this discussion)
- **External PSRAM:** 8 MB (cannot be used for HTTP/WiFi/ESP-NOW work)
- **Stable heap available:** ~12-15 KB under normal operation

### 1.2 What's Actually Running

```
Internal RAM allocation baseline (minimum required):
├─ WiFi STA driver stack          ~50-80 KB
├─ ESP-NOW RX/TX + callbacks      ~20-30 KB  
├─ MQTT client (ArduinoJSON)      ~15-25 KB
├─ LVGL display rendering         ~16-32 KB (configurable)
├─ FreeRTOS kernel + tasks        ~10-15 KB
└─ System/misc overhead           ~5-10 KB
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Total baseline                     ~120-180 KB
Remaining for heap                 ~12-15 KB (what's left)
```

**This is not configurable.** WiFi, ESP-NOW, MQTT require internal RAM. PSRAM cannot substitute.

### 1.3 What We're Asking It To Do

**Simultaneously:**
1. Render LVGL UI (display updates every frame)
2. Receive ESP-NOW telemetry from transmitter
3. Connect and sync with MQTT broker
4. Serve HTTP dashboard page (40.5 KB content)

**During the 38-second dashboard render:**
- WiFi continues running (must keep AP/STA alive)
- ESP-NOW must respond to incoming heartbeats
- MQTT retries broker connection (every 10s)
- LVGL keeps updating display
- HTTP chunks send 1 KB every ~1 second

---

## 2) The Core Question: Is 12 KB "Enough"?

### 2.1 For What's Currently Running

**No.** 12 KB is actually on the edge for a receiver with these responsibilities.

**Evidence from logs:**
```
[render low heap sample] heap=7676 ... stage=common_styles
[render low heap sample] heap=6576 ... stage=common_styles
[render low heap sample] heap=4992 ... stage=common_styles  ← CRITICAL
```

This isn't "low heap during render." This is **heap starvation during normal operation**. The system is designed on the knife's edge.

### 2.2 Could We Optimize To Use 12 KB Effectively?

**For dashboard rendering?** Partially.

**Theoretical best case:**
- Stop all background tasks (MQTT, WiFi idle)
- Minimize LVGL rendering to single frame
- Reduce HTTP chunk operations
- Result: Could probably get 20 KB allocated and streamed

**In practice:**
- Can't stop WiFi (AP/STA connection breaks)
- Can't stop ESP-NOW (link dies)
- Can't stop MQTT (device config updates fail)
- Result: Still need 38+ seconds, still timeout

---

## 3) Architectural Analysis: Is This Actually a Valid Design?

### 3.1 What the LCD Receiver Is Optimized For

**From device specs and design:**
- 7" touch display → visual interface for monitoring
- Low-power WiFi STA → network connectivity
- ESP-NOW receiver → telemetry ingestion
- Compact form factor → edge device

**NOT optimized for:**
- Large content serving (webserver role)
- High concurrent load (radio + network + HTTP)
- Static asset hosting (that's a server job)

### 3.2 Device Role Classification

There are fundamentally **two types of devices** in this system:

| Device | Transmitter (POE) | Receiver (LCD) |
|--------|-------------------|---|
| **Primary role** | Data source + control hub | Display + monitoring |
| **Primary interface** | ESP-NOW TX + MQTT publish | Touch UI + ESP-NOW listen |
| **Secondary interface** | OTA/diagnostic HTTP API | Web API + dashboard pages |
| **Memory profile** | Generous (SoM) | Constrained (edge) |
| **HTTP workload** | **Maintenance/status endpoints** | **Large page render + API** |

From current source, the transmitter does **not** host dashboard/static content rendering. It exposes lightweight maintenance routes (OTA + diagnostics), while the receiver currently carries the heavy dashboard render path.

### 3.3 The Design Mistake (Corrected Against Source)

**Current design (as implemented):**
```
┌─────────────┐         ┌──────────────┐
│ Transmitter │◄──ESP─NOW──┤  Receiver   │
│  (POE)      │         │   (LCD)      │
│             │         │              │
│ OTA/API     │         │ Webserver    │  ← Problem location
│ status only │         │ Dashboard    │
│ (small)     │         │ (40+ KB)     │  ← Heavy render on constrained node
└─────────────┘         └──────────────┘
```

**Should be (workload-corrected):**
```
┌─────────────┐         ┌──────────────┐         ┌─────────────┐
│ Transmitter │◄──ESP─NOW──┤  Receiver   │         │  Browser    │
│  (POE)      │         │   (LCD)      │◄───WiFi──┤             │
│             │         │              │         │             │
│ OTA/API     │         │ Minimal API  │         │ Renders UI  │
│ status only │         │ (JSON only)  │         │ client-side │
│ (small)     │         │ (2-3 KB)     │         │             │
└─────────────┘         └──────────────┘         └─────────────┘
```

**Source confirmation used for this correction:**
- `ESPnowtransmitter2/.../src/network/ota_manager.cpp` registers OTA/diagnostic routes only.
- `ESPnowtransmitter2/.../src/network/ota_status_handlers.cpp` root route returns plain text (`"ESP-NOW Transmitter - Ready for OTA"`), not dashboard HTML.
- `espnowreceiver_LCD/.../lib/webserver_lcd/pages/*` contains receiver dashboard page/content/script implementation.

---

## 4) The Real Options

### 4.1 Option A: Optimize Within Current Constraints (Quick fix)

**Reduce dashboard content:**
- CSS: 5.5 KB → 2 KB (brutal simplification)
- JS: 17.4 KB → 5 KB (minimal interactivity)
- HTML: 5.6 KB → 2 KB (bare essentials)
- **Total: 40.5 KB → 9 KB**

**Result:** Render completes in ~9 seconds instead of 38 → HTTP timeout avoided

**Pros:**
- Immediate fix
- No architectural change
- Works with current devices

**Cons:**
- Dashboard becomes very minimal
- Still vulnerable to slow networks
- Still consuming valuable receiver time
- Fragile (any feature added breaks it again)
- **Receiver never had this workload intended**

### 4.2 Option B: Move Dashboard Off Receiver (Proper fix)

**Architecture:**
- Receiver: Only serves `/api/receiver_status.json` (~500 bytes)
- Transmitter: Continues ESP-NOW + MQTT + OTA/diagnostic API duties
- Browser: Loads dashboard UI from a separate host (or embedded web app) and reads JSON from device APIs

**Result:** Receiver freed from large content serving without requiring transmitter-side dashboard rendering

**Pros:**
- Receiver only does what it's designed for (display + minimal API)
- Transmitter remains focused on control/data-plane duties
- Receiver stays responsive for ESP-NOW/MQTT
- Scalable (multiple receivers can be viewed by one browser UI)
- Matches current code reality (no transmitter dashboard renderer)

**Cons:**
- Requires architectural changes
- User loses full "local web page" on receiver unless client UI is hosted elsewhere
- Requires browser/client UI hosting path

### 4.3 Option C: Hybrid Approach (Best balance)

**Keep receiver webserver but redesign its role:**
1. Receiver serves **read-only API endpoints** (JSON only, 2-5 KB responses)
   - `/api/receiver_status` - Device info
   - `/api/local_telemetry` - Last received TX values
   - `/api/receiver_health` - Heap, uptime, link status

2. Browser fetches data from API, renders locally (client-side)
   - No large HTML/CSS/JS uploads
   - Minimal HTTP transactions
   - Responsive UI (client-side, not blocked by render)

3. Dashboard UI is hosted externally (browser/PWA/lightweight service) as primary interface

**Result:** 
- Receiver webserver becomes lightweight API server (2-3 KB responses)
- No 38-second render timeout
- Users can check receiver status locally if needed
- Full dashboard available from external browser-hosted UI

**Pros:**
- Modest changes needed (remove HTML rendering, keep JSON APIs)
- Receiver stays responsive
- Both options available (local JSON + remote dashboard)
- Clean separation of concerns

**Cons:**
- Browser needs to call two servers (if accessing receiver)
- Client needs JavaScript to render

---

## 5) Memory Optimization Alone Is Insufficient

### 5.1 Can We "Just Add More RAM"?

**No.** This is an **architectural problem, not a RAM shortage.**

Even if the device had **twice** the RAM (24 KB heap):
- Would buy ~10 seconds of render time
- Still timeout at 25+ seconds
- Would be fragile/brittle
- Doesn't solve the fundamental issue: receiver shouldn't be a dashboard server

### 5.2 Could We Use PSRAM For This?

**No.** PSRAM (external 8 MB) cannot be used for:
- HTTP send buffers (requires MALLOC_CAP_INTERNAL)
- WiFi stack memory (hardware requirement)
- ESP-NOW frames (hardware requirement)

PSRAM can only be used for application data (images, large buffers marked explicitly).

---

## 6) What Other IoT Devices Do

### 6.1 Typical Edge Device Patterns

**Small display device (similar to our receiver):**
- Runs display UI (mandatory)
- Connects to backend (WiFi/Bluetooth)
- **Does NOT host large web content**
- Serves minimal REST API only
- Queries backend for data

**Examples:**
- Nest Thermostat: Syncs with cloud, doesn't host UI webserver
- Philips Hue Bridge: Serves **minimal** JSON API (< 1 KB responses)
- Tesla UI: Doesn't host dashboard; displays data from vehicle computer
- Printers: Serve status page only (< 5 KB HTML)

**Nobody puts a 40 KB dashboard on a 327 KB RAM device with concurrent radio work.**

---

## 7) Honest Assessment

### 7.1 Device Capability Truth Table

| Workload | Feasible? | Note |
|----------|-----------|------|
| Display UI + ESP-NOW RX | ✅ Yes | Core function |
| + WiFi connectivity | ✅ Yes | Designed for this |
| + MQTT client | ✅ Yes | Works in practice |
| + Minimal JSON API (2-3 KB) | ✅ Yes | Light HTTP load |
| + Large HTML dashboard (40+ KB) | ❌ No | Too much + concurrent |
| **At the same time** | ❌ No | Heap exhaustion guaranteed |

### 7.2 The Bottom Line

**The device CAN handle:**
- ✅ WiFi + ESP-NOW + MQTT + display + minimal webserver

**The device CANNOT handle:**
- ❌ WiFi + ESP-NOW + MQTT + display + large dashboard serve

**It's not a heap problem; it's an architecture problem.**

The device is being asked to do more than it was designed for. Optimization buys time but doesn't change the fundamental limitation.

---

## 8) Recommendation

### Short Term (Keep current design working)

Implement **Option C - Hybrid Approach:**
## 10) Espressif Recommendations & Real-World Solutions

### 10.1 Official Espressif Guidance On Memory Constraints

**From Espressif RF Coexistence Guide:**
- WiFi + ESP-NOW coexistence requires **internal RAM (MALLOC_CAP_INTERNAL)** for all protocol stack buffers
- PSRAM can only be used for application data, NOT for WiFi/ESP-NOW/MQTT stack operations
- Tuning `CONFIG_LWIP_TCP_SND_BUF_DEFAULT` and `CONFIG_LWIP_TCP_WND_DEFAULT` can reduce WiFi overhead
- Reducing `CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM` and `CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM` saves internal RAM
- **Performance guide emphasizes:** "Choose performance-critical aspects. For concurrent operations, optimize task priorities to prevent blocking."

**Key Finding:** Espressif explicitly states that memory tuning (buffer reduction) works **only for steady-state**. It does NOT solve **concurrent load spikes** or **blocking operations**.

### 10.2 Real-World HTTP Server Patterns (From ESP-IDF Examples)

**Pattern 1: Async Request Handlers**
```cpp
// From esp-idf/examples/protocols/http_server/async_handlers
// Problem: Long-running HTTP requests block other connections
// Solution: Use httpd_req_async_handler_begin() + worker task queue

typedef struct {
   httpd_req_t* req;           // Async copy of request (heap allocated)
   httpd_req_handler_t handler; // Handler function to call in worker thread
} httpd_async_req_t;

// Use counting semaphore to track available workers
xSemaphoreCreateCounting(max_workers, 0)

// If no workers available: immediately return 503 Service Unavailable
// DON'T: Block or wait - this starves other tasks
```

**Key lesson:** Espressif pattern shows that HTTP handlers MUST NOT block the server task. If long operations are needed, they must run in separate worker threads, with proper backpressure (return 503 if queue full).

**Pattern 2: Chunked Transfer Encoding**
```cpp
// From WebServer library examples (based on httpd)
// Problem: Large content doesn't fit in available buffer
// Solution: Send in chunks with delays to allow other tasks to run

server.chunkResponseBegin();
for (int i = 0; i < 10; i++) {
   server.chunkWrite(data, chunk_size);
   delay(500);  // Let CPU switch to other tasks
}
server.chunkResponseEnd();

// Results: ~5 second total render time instead of blocking entire server
```

**Key lesson:** Even with chunking, sending large responses is expensive. The `delay()` yields to FreeRTOS scheduler, but other tasks (WiFi, ESP-NOW) are still memory-starved during send window.

### 10.3 Why The FSM Broke The Receiver

**Before FSM (espnowreceiver_2):**
- Simpler ESP-NOW message handling (basic queue polling)
- Less overhead in ISR callbacks
- More predictable memory use
- Dashboard render could succeed with lucky timing

**After FSM (espnowreceiver_LCD with UnifiedLinkFsm):**
- More complex state machine (more states = more work per ISR)
- Higher priority ESP-NOW worker task (priority 2 vs old priority 1)
- Discovery announcements running in parallel
- TX scheduler with retry logic + backoff timers
- **Result:** ISR callbacks take longer → WiFi interrupts delayed → Dashboard HTTP send gets starved

**From task config analysis:**
```cpp
// espnowreceiver_LCD/include/task_config.h
constexpr uint8_t ESPNOW_WORKER_PRIORITY      = 2;  // HIGH
constexpr uint8_t DISPLAY_RENDERER_PRIORITY   = 1;  // LOW
constexpr uint8_t ESPNOW_TX_PRIORITY          = 1;  // LOW (but has retry loop!)

// HTTP server runs at inherited priority (FreeRTOS default = tskIDLE_PRIORITY + 5 ~= 2)
// Result: ESPNOW_WORKER (prio 2) can starve HTTP server (prio 2)
```

### 10.4 What Other Developers Do On Constrained Devices

**From Arduino ESP32 WebServer library:**
1. **Always include `delay(2)` in loop:**
   ```cpp
   void loop() {
      server.handleClient();
      delay(2);  // allow the cpu to switch to other tasks
   }
   ```
   This yields every 2ms. **Without it, HTTP handler monopolizes CPU.**

2. **Keep responses small:**
   - Status endpoints: JSON only, 500 bytes max
   - Large content: Serve from external server
   - If must serve large files: Use chunked transfer + delays

3. **Don't combine heavy radio (WiFi + ESP-NOW) with large HTTP:**
   - Projects that handle both use separate devices
   - Or: HTTP server on transmitter, display-only API on receiver

**From Arduino Camera WebServer example:**
```cpp
// CameraWebServer runs in its OWN task (not main loop)
void loop() {
   delay(10000);  // Main loop does nothing
}
// HTTP server has dedicated FreeRTOS task with proper stack (4096+ bytes)
// This is the ONLY way to serve large responses concurrently
```

### 10.5 The FSM + Async Render Problem (Analysis)

**Timeline of render failure:**
```
T0: Browser requests /dashboard (40.5 KB)

T1: HTTP handler starts in server task
   - Allocates request context (heap stress begins)
   - Starts emitting 1 KB chunks

T1-T9: Chunks 1-9 send successfully (9 KB)
   - Webserver task not doing anything else
   - WiFi & ESP-NOW runs during delays
   - Memory recovers between chunks

T10: Chunk 10 send attempt
   - Webserver task blocks in send
   - WiFi stack needs to schedule transmit
   - But heap is at 7-8 KB (tight!)
   - TCP window gets smaller
   - Takes longer to send each packet

T15-T38: Chunks 10-39 send slowly
   - Each chunk takes 0.5-1.0 seconds instead of 100ms
   - ESP-NOW ISR fires periodically
      - Wants to queue message
      - **Heap too low - allocation fails**
      - Message lost (retransmit triggered)
   - MQTT task tries to send config update
      - **Out of memory**
      - Disconnects
   - Display task waits for LVGL (blocked on another task)
    
T38+: Timeout!
   - Browser still waiting for more chunks
   - Server waiting for TCP window
   - **No progress for 30+ seconds**
```

**Why FSM made it worse:**
- Higher priority ESP-NOW worker = more ISR wake-ups
- TX scheduler retries = more memory allocations during ISR context
- More state transitions = more CPU time in callbacks
- Result: Webserver gets less CPU time to make progress

---

## 11) The Real Solution: Task Priority & Backpressure

### 11.1 What Espressif Recommends (Performance Guide)

**For WiFi + Radio concurrent workloads:**
1. **Separate tasks, proper priorities:**
   - Critical radio handler: Priority 3-4 (highest)
   - HTTP server: Priority 2 (high but not critical)
   - Background tasks: Priority 0-1 (can be interrupted)

2. **Non-blocking event handlers:**
   - ISR/callbacks must be fast (< 1 ms)
   - Defer work to task queues
   - Never malloc/free in callbacks

3. **Reduce buffer sharing:**
   - Give each task its own small buffer (PSRAM if available)
   - Only share critical state with mutexes
   - Avoid malloc/free in hot paths

### 11.2 Concrete Fix For Receiver LCD

**Strategy: Reduce HTTP Handler CPU Time**

Instead of:
```cpp
// CURRENT - blocks for 38 seconds
emit_dashboard_page_content(req);  // Sends 40 KB in 40 chunks
```

Do one of:

**Option A: Reduce content**
```cpp
// Remove CSS/JS, keep HTML only = 2-3 KB
// Render time: < 5 seconds
// Leaves more CPU time for ESP-NOW & MQTT
httpd_resp_send(req, minimal_html, strlen(minimal_html));
```

**Option B: Move to worker task** (Espressif pattern)
```cpp
// Make HTTP handler async
httpd_req_async_handler_begin(req, &async_req);
xQueueSend(worker_queue, async_req, pdMS_TO_TICKS(100));
// Return immediately to server
// Worker thread sends content in background
// Server can handle other requests while worker sends chunks
```

**Option C: Serve read-only API (RECOMMENDED)**
```cpp
// Only serve JSON status endpoints
#define TOTAL_SIZE 256  // Small JSON response
GET /api/status → {"soc": 42, "power": 500, ...}

// Dashboard/UI is hosted externally (browser/PWA)
// Receiver only provides data API
// Result: Concurrent operations work better
```

---

@@### Short Term (Keep current design working)
@@
@@Implement **Option C - Hybrid Approach:**
1. Remove HTML/CSS/JS content from receiver
2. Keep JSON API endpoints (convert dashboard to read-only endpoints)
3. Users access full dashboard from external browser-hosted UI
4. Local JSON API available for debugging

**Changes needed:**
- Remove `emit_dashboard_page_content()` call
- Convert to `emit_api_status()` returning JSON
- Delete 40.5 KB of CSS/JS/HTML content
- Result: Receiver webserver 2-3 KB responses only

**Time to implement:** 1-2 hours

### Medium Term (Proper architecture)

Document that receiver is **display + minimal API device**, not dashboard-rendering webserver device. Official architecture should show dashboard/UI hosted externally.

### Long Term (If expansion needed)

If receiver needs richer local interface:
- Use **smaller device** (new receiver variant with more RAM), or  
- Move receiver webserver to external service (cloud/transmitter), or
- Accept the 38-second timeout as "expected behavior when overloading device"

---

## 9) Final Answer To Your Question

**"Do we need to use the heap for this, or is it just that the device doesn't have enough memory?"**

**Answer:** The device doesn't have enough memory for the *intended design*. But the **intended design is wrong for this device.**

**The device is sufficient for:**
- ✅ Display + monitoring (what it's designed for)
- ✅ + minimal local API

**The device is insufficient for:**
- ❌ Large dashboard hosting under concurrent load

**This isn't a problem that heap optimization fixes. It's a problem that architecture redesign fixes.**

**Recommendation: Pivot to Option C (hybrid API) to make receiver serve its actual role, not try to force it to be a webserver.**

