# DST Transition Array on /debug — Implementation Plan (2026-03-31)

## 1) Goal

Add a **new section** to the receiver `/debug` page showing the **next 4 timezone/DST time changes** (if available), sourced from transmitter time logic and delivered over ESP-NOW.

User requirement interpreted as:
- show upcoming time changes (DST boundaries and any offset transitions)
- no dependency on transmitter HTTP proxy path
- carry data over ESP-NOW messages
- keep traffic low and avoid unnecessary recalculation

---

## 2) Current State (What Exists Today)

### Receiver `/debug` page
Current debug page is receiver-hosted and currently shows:
1. Transmitter debug level control (`/api/debugLevel`, `/api/setDebugLevel`)
2. Memory health (`/api/memory_samples`)

Files:
- `espnowreceiver_2/lib/webserver/pages/debug_page_content.cpp`
- `espnowreceiver_2/lib/webserver/pages/debug_page_script.cpp`

### ESP-NOW data path for status metadata
Receiver already receives periodic `version_beacon_t` messages from transmitter (forced every 30s and on state/version changes).

Important for this feature request: DST transition data is **very low-churn** and should **not** be re-sent periodically.

Files:
- `esp32common/espnow_transmitter/espnow_common.h` (`version_beacon_t`)
- `ESPnowtransmitter2/.../src/espnow/version_beacon_manager.cpp`
- `espnowreceiver_2/src/espnow/espnow_tasks.cpp` (handler for `msg_version_beacon`)

### Time pipeline baseline
- Heartbeat now carries `unix_time`, `utc_offset_min`, `time_source`.
- No future transition array is currently carried.
- Transmitter timezone logic lives in `ethernet_utilities.cpp`.

---

## 3) Proposed Transport Strategy (ESP-NOW)

## Decision
Use a **new dedicated ESP-NOW message type** for timezone transition snapshots, sent only:
1. once after boot/timezone initialization
2. when transition data changes

This avoids sending low-churn DST data inside periodic beacons.

### Why this is the right message
1. **Meets requirement exactly**: boot + change only (no periodic retransmit).
2. **Minimizes traffic**: no repeated transition payload every 30s.
3. **Cleaner separation**: runtime heartbeat/version signals stay periodic; DST snapshot remains event-driven.
4. **Deterministic UI semantics**: receiver cache updates only when transmitter publishes new snapshot.

### New wire payload (planned)
In `esp32common/espnow_transmitter/espnow_common.h`:

```cpp
constexpr uint8_t TIME_TRANSITION_MAX = 4;

// New message type in msg_type enum
// msg_time_transitions_snapshot,

typedef struct __attribute__((packed)) {
    uint32_t transition_unix_utc;   // UTC epoch seconds of the offset change
    int16_t  offset_before_min;     // offset minutes before transition
    int16_t  offset_after_min;      // offset minutes after transition
} time_transition_t;

typedef struct __attribute__((packed)) {
   uint8_t  type;                                    // msg_time_transitions_snapshot
   uint8_t  transition_count;                        // 0..TIME_TRANSITION_MAX
   uint16_t revision;                                // increments when snapshot changes
   uint32_t generated_unix_utc;                      // when snapshot was generated
   char     timezone_name[40];                       // e.g. Europe/London
   char     timezone_abbrev[8];                      // e.g. BST/GMT
   int16_t  current_utc_offset_min;                  // current offset minutes
   time_transition_t transitions[TIME_TRANSITION_MAX];
   uint32_t checksum;                                // CRC32 (same approach as heartbeat)
} time_transitions_snapshot_t;
```

This provides everything needed for display and debugging without extra API calls to transmitter.

### Wire-size safety
Snapshot message remains well below ESP-NOW 250-byte payload limit. Final build will include:

```cpp
static_assert(sizeof(time_transitions_snapshot_t) <= 250,
              "time_transitions_snapshot_t exceeds ESP-NOW payload");
```

---

## 4) Transition Computation Strategy (Transmitter)

## Decision
Compute transitions on transmitter in `ethernet_utilities`, **cache them**, and send a snapshot message only on boot/change.

### Why
- Transition schedule changes very rarely (timezone change, DST boundary crossing, reboot).
- Recomputing per beacon or heartbeat is unnecessary.
- Cached approach is deterministic and low CPU.

### Planned algorithm
1. Start from current UTC time `now`.
2. Determine current offset in minutes.
3. Scan forward in coarse day steps (up to a horizon, e.g., 3 years) until `max=4` offset changes found.
4. For each day where offset differs, binary-search within that day to locate transition second.
5. Record:
   - transition UTC epoch
   - offset before
   - offset after
6. Cache the resulting array.

### Recompute triggers
Rebuild cached transition array when:
- timezone is set/changed via `setenv("TZ",...) + tzset()`
- successful geolocation timezone update
- optional periodic integrity refresh in background task (disabled by default)

After recompute, compare against prior snapshot (`timezone_name`, `current_utc_offset_min`, `transition_count`, transitions[]). If changed, increment `revision` and publish one new snapshot message.

This aligns with existing cached-offset strategy and avoids per-message recalculation.

### Send triggers (final)
`msg_time_transitions_snapshot` is sent:
1. once after boot when time/timezone become valid
2. after any detected transition-data change

No periodic re-send for this payload.

---

## 5) Receiver Data Model + API Plan

### Receiver cache additions
Extend receiver-side transmitter cache (`transmitter_state`) with:
- timezone name
- timezone abbreviation
- current offset
- transition count
- 4 transition entries
- generation timestamp

### ESP-NOW ingestion
In `espnow_tasks.cpp` new snapshot handler:
- parse new fields
- store in `TransmitterManager`/`TransmitterState`

### API endpoint for /debug page
Add new receiver endpoint:
- `/api/transmitter_time_transitions`

Response shape (planned):

```json
{
  "success": true,
  "timezone_name": "Europe/London",
  "timezone_abbrev": "BST",
  "current_utc_offset_min": 60,
  "generated_unix_utc": 1774920000,
  "transition_count": 4,
  "transitions": [
    {"unix_utc": 1795395600, "offset_before_min": 60, "offset_after_min": 0},
    {"unix_utc": 1808622000, "offset_before_min": 0, "offset_after_min": 60}
  ]
}
```

If unavailable, return `success:true` with `transition_count:0` and explanatory status field.

Add optional fields:
- `revision`
- `source` = `"espnow_snapshot"`

---

## 6) /debug Page UI Plan (New Section)

Add a new card under existing debug cards:

### Section title
**🕒 Timezone & Upcoming Time Changes**

### Summary block (top of new section)
- Current timezone: `Europe/London`
- Current offset: `UTC+01:00`
- Current abbrev: `BST`
- Last snapshot generated: full UTC date/time, e.g. `2026-03-31 06:40:12 UTC`
- Snapshot revision: e.g. `r3`

### "Next change" primary row (prominent)
Display a single highlighted row for the nearest future transition:
- **Next change:** `Sunday, 25 October 2026, 01:00:00 UTC`
- **Change:** `UTC+01:00 → UTC+00:00` (`-60 min`)
- **Type:** `DST ends`

If no future transition available: show `No upcoming time change available`.

### Full upcoming changes table (up to 4 rows)
Columns:
1. **#** (1..4)
2. **UTC transition time (full date/time)**
3. **Before** (`UTC+01:00`)
4. **After** (`UTC+00:00`)
5. **Delta** (`-60 min` / `+60 min`)
6. **Type** (`DST ends`, `DST starts`, or `Offset change`)

### No-data states
- `No upcoming offset transitions found in scan horizon`
- `Transmitter time not yet synchronized`
- `Transmitter transition data not received yet`

### Frontend polling
- Fetch on page load.
- Refresh every 30s for UI freshness, but data only changes when a new ESP-NOW snapshot is received.

---

## 7) Backward Compatibility / Rollout

Adding `msg_time_transitions_snapshot` is a wire contract change. Plan:
1. Update `esp32common` message enum/struct and static asserts.
2. Update transmitter snapshot sender.
3. Update receiver route + parser + cache.
4. Reflash both devices together.

Receiver should guard parsing with:
- length checks (`msg->len >= sizeof(time_transitions_snapshot_t)`)
- safe defaults if fields absent (during migration window only if mixed firmware is possible)

---

## 8) Files Planned for Change

### Common protocol
- `esp32common/espnow_transmitter/espnow_common.h`

### Transmitter
- `ESPnowtransmitter2/espnowtransmitter2/lib/ethernet_utilities/ethernet_utilities.h`
- `ESPnowtransmitter2/espnowtransmitter2/lib/ethernet_utilities/ethernet_utilities.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/message_routes.cpp` (register new sender path if needed)
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/*` (new snapshot manager/sender module)

### Receiver
- `espnowreceiver_2/src/espnow/espnow_tasks.cpp`
- `espnowreceiver_2/lib/webserver/utils/transmitter_state.h`
- `espnowreceiver_2/lib/webserver/utils/transmitter_state.cpp`
- `espnowreceiver_2/lib/webserver/utils/transmitter_manager.h`
- `espnowreceiver_2/lib/webserver/utils/transmitter_manager.cpp`
- `espnowreceiver_2/lib/webserver/api/api_debug_handlers.h`
- `espnowreceiver_2/lib/webserver/api/api_debug_handlers.cpp`
- `espnowreceiver_2/lib/webserver/api/api_handlers.cpp` (route registration)
- `espnowreceiver_2/lib/webserver/pages/debug_page_content.cpp`
- `espnowreceiver_2/lib/webserver/pages/debug_page_script.cpp`

---

## 9) Validation Plan

1. **Unit/compile checks**
   - Ensure protocol struct sizes compile on both projects.
   - `static_assert(sizeof(time_transitions_snapshot_t) <= 250)`.

2. **Runtime checks**
   - Transmitter logs show transition cache generation.
   - Receiver logs show transition array parsed from `msg_time_transitions_snapshot` messages.

3. **UI checks**
   - `/debug` displays timezone summary and transition table.
   - If no transitions in horizon, clear no-data message shown.

4. **DST semantics checks**
   - For `Europe/London`, table includes March/October changes in correct order.
   - `Before`/`After` offsets match expected `0 ↔ +60` pattern.

---

## 10) Final Implementation Notes

- This plan intentionally avoids transmitter HTTP dependency.
- Traffic is minimized: transition data is sent only at boot and on change events.
- Computation remains low by caching transition array at timezone/NTP update points.
- `/debug` becomes the operational source for timezone-transition observability.

---

Prepared by: GitHub Copilot  
Date: 2026-03-31
