# Transmitter Hardware Settings End-to-End Investigation

Date: 2026-05-24
Scope: Full trace of the `/transmitter/hardware` settings lifecycle from receiver page load, through MQTT refresh and save commands, into transmitter persistence/application, and back out to receivers.

## Executive Summary

The receiver-side page flow for `/transmitter/hardware` is structurally correct:
- the page requests `/api/get_battery_settings`
- the receiver API requests a transmitter MQTT refresh on `rx/cmd/refresh/settings`
- the transmitter responds by publishing retained `tx/state/static/settings`
- the receiver MQTT client parses that retained payload and hydrates typed settings caches used by the page
- settings updates from the page are sent back to the transmitter via MQTT command topic `rx/cmd/update/battery`
- the transmitter applies, persists, ACKs, and republishes retained settings on successful update

The strongest root-cause finding (at time of investigation) was on the transmitter side:

**`MqttManager::publish_static_settings()` publishes from `SettingsManager`, but transmitter startup does not appear to call `SettingsManager::instance().init()`.**

`SettingsManager::init()` is the code path that loads persisted NVS settings into the in-memory authority object. Without that initialization, the refresh and retained publish path can still function correctly at the transport level, but they will publish default or uninitialized values instead of the saved hardware settings. That exactly matches the observed symptom: `/transmitter/hardware` loads, but values shown on receivers do not reflect the saved transmitter hardware configuration.

### Implementation Update (2026-05-25)

The primary transmitter bootstrap fix identified in this investigation has now been implemented:
- transmitter startup now calls `SettingsManager::instance().init()` before static settings publication
- startup logs now include battery/power/CAN/contactor settings versions

Receiver-side version-aware readiness and retry handling has also been implemented in both receiver projects:
- API readiness now requires non-zero settings versions
- hardware page retry logic now uses explicit version checks

Result: the code path now aligns with the investigation findings. Remaining closeout work is hardware validation plus planned `/transmitter/config` architectural alignment.

## Investigation Goal

Investigate the complete cycle requested by the user:
1. initial display on the receiver in `/transmitter/hardware`
2. how the settings are refreshed/pulled from the transmitter
3. how changed values are sent back to the transmitter
4. whether the transmitter persists and republishes the updated values
5. identify where the chain is broken

## End-to-End Flow

### 1. Receiver page display flow

The receiver hardware page is registered and rendered correctly.

Relevant files:
- `espnowreceiver_2/lib/webserver/pages/hardware_config_page.cpp`
- `espnowreceiver_2/lib/webserver/pages/hardware_config_page_content.cpp`
- `espnowreceiver_2/lib/webserver/pages/hardware_config_page_script.cpp`

Observed behavior:
- the page script calls `loadHardwareSettings()` on page load
- `loadHardwareSettings()` fetches `/api/get_battery_settings`
- the returned JSON is mapped into UI controls for:
  - CAN frequency / CAN-FD frequency
  - equipment stop type
  - external precharge
  - precharge timings
  - contactor enable / NC mode / PWM settings
  - periodic BMS reset / first-align target time
  - LED mode
- if `settings_ready` is false but `requested` is true, the page retries a small number of follow-up polls

Conclusion:
- the browser/UI side is not the primary fault
- the page is already designed to cope with a short delay between request and typed-cache hydration

### 2. Receiver API refresh flow

Relevant file:
- `espnowreceiver_2/lib/webserver/api/api_settings_handlers.cpp`

Observed behavior in `api_get_battery_settings_handler()`:
- when MQTT is enabled and connected, the receiver publishes a refresh command to:
  - `batt-emu/mqtt-v1/rx/cmd/refresh/settings`
- it then checks whether typed settings caches are available through `TransmitterManager`
- it returns cached values from:
  - battery settings
  - power settings
  - CAN settings
  - contactor settings
  - battery emulator settings (for `led_mode`)
- it reports readiness via:
  - `battery_known`
  - `power_known`
  - `can_known`
  - `contactor_known`
  - `settings_ready`

Conclusion:
- the receiver API does request a refresh correctly
- the page is not reading raw MQTT JSON directly; it depends on the receiver’s typed caches

### 3. Receiver MQTT ingest and cache hydration

Relevant files:
- `espnowreceiver_2/src/mqtt/mqtt_client.cpp`
- `espnowreceiver_LCD/src/mqtt/mqtt_client.cpp`

Observed behavior in `handleStaticSettings()`:
- the receiver parses retained topic `tx/state/static/settings`
- it accepts either:
  - a root object containing settings sections directly, or
  - a wrapper object containing a `settings` object
- it hydrates typed caches for:
  - `battery`
  - `battery_emulator`
  - `power`
  - `can`
  - `contactor`
- these typed caches then feed `TransmitterManager::get*Settings()` used by the web API

Additional hardening already applied during this session:
- dynamic JSON capacity for larger payloads
- alias fallback handling for some field names (`can_frequency_khz`, `can_fd_frequency_mhz`, `nc_mode`, `pwm_enabled`, `bms_first_align_target_mins`)
- warning if the payload contains no recognized sections

Conclusion:
- the receiver parse/hydration path is viable and was further hardened
- if the transmitter publishes correct retained settings, the receiver should be able to display them

### 4. Receiver save flow back to the transmitter

Relevant file:
- `espnowreceiver_2/lib/webserver/api/api_settings_handlers.cpp`

Observed behavior in `api_save_setting_handler()`:
- the hardware page posts individual changes to `/api/save_setting`
- the API maps each field to a `category`, `field`, and typed value
- the receiver sends a settings command through `MqttCommandClient::sendSettingsUpdate(...)`
- on success, it waits for an ACK and then updates the local cache optimistically

Important detail:
- the command topic name remains `update/battery`, but the payload category selects battery/power/inverter/CAN/contactor behavior on the transmitter side
- despite the legacy topic name, this is not limited to battery-only fields

Conclusion:
- the save path is coherent and not the likely cause of the initial load problem

### 5. Transmitter command handling and refresh handling

Relevant file:
- `ESPnowtransmitter2/espnowtransmitter2/src/network/mqtt_manager.cpp`

Observed behavior:
- transmitter subscribes to refresh and command topics including:
  - `rx/cmd/refresh/settings`
  - `rx/cmd/update/battery`
- `handle_refresh_command()` routes a settings refresh request directly to `publish_static_settings()`
- `handle_settings_command()` routes the update payload into `SettingsManager::apply_settings_update(...)`

Conclusion:
- refresh requests and settings updates do arrive at the right transmitter subsystem

### 6. Transmitter retained publish source of truth

Relevant files:
- `ESPnowtransmitter2/espnowtransmitter2/src/network/mqtt_manager.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/network/mqtt_task.cpp`

Observed behavior:
- `MqttManager::publish_static_settings()` builds the retained settings payload using values from `SettingsManager::instance()`
- `mqtt_task.cpp` also publishes the same retained settings as part of the MQTT connect snapshot bundle

This means two important things:
1. both the initial retained publish and explicit refresh publish depend on `SettingsManager`
2. if `SettingsManager` has not loaded persisted state, both publishes will emit defaults/stale in-memory values

Conclusion:
- the correctness of `static/settings` is only as good as transmitter `SettingsManager` initialization

### 7. Transmitter persistence/application path

Relevant files:
- `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_manager.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_field_setters.cpp`
- `ESPnowtransmitter2/espnowtransmitter2/src/settings/settings_espnow.cpp`

Observed behavior:
- `SettingsManager::init()` exists and performs initialization including loading persisted settings
- category-specific save functions call persistence routines such as:
  - `save_battery_settings()`
  - `save_power_settings()`
  - `save_inverter_settings()`
  - `save_can_settings()`
  - `save_contactor_settings()`
- after successful persistence, the field setters call `send_settings_changed_notification(...)`
- `send_settings_changed_notification(...)` republishes retained state including `publish_static_settings()` and metadata/schema versions

This is a crucial finding:
- **successful saves do republish retained settings**
- therefore, the update/republish design is present and working in code

Conclusion:
- the missing behavior is not “save does not republish”
- the higher-probability failure is “the authoritative settings object was never initialized with persisted values before publishing”

## Primary Root Cause (Historical)

Status: fixed in current codebase (2026-05-25).

### Finding (at investigation time)

The transmitter startup sequence included the `SettingsManager` header, but no clear call to `SettingsManager::instance().init()` was found in transmitter bootstrap.

Relevant file examined:
- `ESPnowtransmitter2/espnowtransmitter2/src/main.cpp`

Startup phases observed there include:
- hardware bootstrap
- system settings bootstrap
- WiFi neutralization for Ethernet routing
- battery initialization
- connectivity / Ethernet / MQTT bootstrap
- data-layer initialization

However, the transmitter bootstrap was not observed to initialize the transmitter `SettingsManager` before MQTT retained settings are published.

### Why this matters

`publish_static_settings()` reads from `SettingsManager::instance()`.

`SettingsManager::init()` is what loads persisted settings from NVS into that authority object.

If `init()` is never called:
- retained `tx/state/static/settings` can still be published
- refresh commands can still be handled
- receiver parsing can still succeed
- receiver caches can still populate
- **but the values being published can be defaults instead of the saved transmitter hardware settings**

That directly explains the user-visible symptom:
- `/transmitter/hardware` shows values
- but they are not the correct values from the transmitter’s saved hardware configuration

## Secondary Findings

### Receiver-side robustness was worth keeping

Even though it does not appear to be the primary fault, the receiver-side hardening performed during this investigation is still valid and useful:
- it tolerates slightly different retained payload shapes
- it tolerates several field-name aliases
- it keeps short follow-up polling alive when caches are still being hydrated

These changes make the receivers less fragile, but they do not fix a transmitter source-of-truth initialization failure.

### Receiver cache persistence is not the authoritative source

The receivers persist transmitter caches locally, but the `/transmitter/hardware` page is intended to reflect transmitter state, not just old receiver-side cache state. Therefore, even if receiver NVS cache persistence exists and works, it cannot replace proper transmitter initialization and retained publish correctness.

## Most Likely Failure Scenario

1. transmitter boots
2. `SettingsManager` remains at defaults because `SettingsManager::instance().init()` was not called
3. transmitter connects to MQTT and publishes retained `tx/state/static/settings` from default in-memory values
4. receiver loads `/transmitter/hardware`
5. receiver requests refresh on `rx/cmd/refresh/settings`
6. transmitter republishes the same default-based retained payload
7. receiver parses it correctly and hydrates typed caches
8. page displays those incorrect/default values

This matches the reported symptom much better than a receiver parser bug.

## Confidence Assessment

Confidence in the primary finding: **high**

Reason:
- the receiver display path is coherent
- the receiver refresh path is coherent
- the receiver static/settings parser is coherent and has been hardened
- the transmitter update path does republish on successful save
- the remaining high-impact gap is authoritative transmitter settings initialization before publish

The only missing element is on-device runtime confirmation after adding explicit `SettingsManager::instance().init()` to transmitter startup.

## Recommended Fix

### Required code fix

Add explicit transmitter settings initialization during transmitter bootstrap, before any retained settings publish can occur.

Recommended placement:
- in transmitter startup/persistence bootstrap, before MQTT services begin publishing retained state
- likely in `bootstrap_persistence()` or immediately after it, depending on the desired lifecycle ownership

Recommended action:
- call `SettingsManager::instance().init()` once during startup
- log success/failure explicitly
- ensure this occurs before any call chain that can lead to:
  - `publish_static_settings()`
  - MQTT connect snapshot publish
  - refresh/settings handling

### Validation after fix

1. build transmitter project
2. boot transmitter with known non-default hardware settings already stored in NVS
3. confirm startup log shows `SettingsManager` initialization
4. inspect retained `tx/state/static/settings`
5. load `/transmitter/hardware` on both receivers
6. confirm values now match saved transmitter settings without requiring a save round-trip
7. change one hardware field from the receiver page
8. confirm ACK, retained republish, and receiver UI consistency after reload

## Recommended Follow-up Checks

After applying the transmitter init fix, verify:
- retained payload shape still matches receiver expectations
- battery emulator section still contains `led_mode`
- version counters increment after saves
- no duplicate initialization of settings objects occurs
- runtime `apply_runtime_static_settings()` side effects still execute in the correct order for power/CAN/contactor changes

## Final Conclusion

The end-to-end control path from receiver page to transmitter and back is mostly intact.

The strongest explanation for the broken `/transmitter/hardware` values is **not** that the receiver is failing to request, parse, or cache settings. The stronger explanation is that the transmitter is publishing `static/settings` from a `SettingsManager` instance that was never initialized from NVS during startup.

In short:
- transport path: present
- refresh path: present
- save + ACK path: present
- republish-on-save path: present
- authoritative startup load of settings into transmitter `SettingsManager`: **appears missing and is the likely root cause**

## Appendix: `/transmitter/hardware` vs `/transmitter/config`

The user correctly observed that these two pages feel very similar from a UI perspective: both are receiver-hosted configuration pages for the transmitter, both load cached transmitter state, both track local edits, and both save changes back over MQTT. However, they are **not backed by the same configuration architecture**.

### High-level purpose split

`/transmitter/config`
- transmitter connectivity and identity page
- covers:
  - transmitter metadata/status (read-only)
  - transmitter network configuration (static IP/DHCP + DNS)
  - transmitter MQTT broker/client configuration
- implemented as one older combined page

`/transmitter/hardware`
- transmitter runtime/static hardware-behavior page
- covers:
  - CAN timing/mode
  - power/precharge/contactors
  - equipment stop behavior
  - periodic BMS reset scheduling
  - LED mode plus live LED runtime status
- implemented as a newer focused page over the generic typed settings model

### Structural similarity

Both pages share the same broad interaction pattern:
- page is served by the receiver webserver
- page JavaScript loads transmitter-related data via receiver API endpoints
- receiver APIs read/write receiver-side transmitter caches (`TransmitterManager`)
- saves are proxied to the transmitter over MQTT command topics
- a save button is driven by change tracking against an initial snapshot

This is why the pages feel like they “basically do the same thing”. From the browser’s point of view, they do.

### Structural difference: backing data model

The major difference is **what data model each page is sitting on top of**.

#### `/transmitter/config` uses dedicated transport/config models

It is built on transmitter cache domains that are separate from the generic settings system:
- metadata cache
- network config cache
- MQTT config cache

Its APIs are specialized:
- `GET /api/transmitter_metadata`
- `GET /api/get_network_config`
- `POST /api/save_network_config`
- `GET /api/get_mqtt_config`
- `POST /api/save_mqtt_config`

Its MQTT command topics are specialized too:
- `rx/cmd/update/network`
- `rx/cmd/update/mqtt`

The cached data comes from dedicated retained/static topics such as:
- `tx/state/static/network`
- `tx/state/static/mqtt`
- `tx/meta/version`

So `/transmitter/config` is really a **transport/infrastructure configuration page**.

#### `/transmitter/hardware` uses the generic typed settings model

It is built on the unified settings snapshot and typed settings categories:
- battery
- battery_emulator
- power
- inverter
- can
- contactor

Its receiver API is more generic:
- `GET /api/get_battery_settings`
- `POST /api/save_setting`

Its save path uses one generic command topic with category/field routing:
- `rx/cmd/update/battery`

Its load path depends on the unified retained settings snapshot:
- `tx/state/static/settings`

So `/transmitter/hardware` is really a **model/settings page** rather than a transport config page.

### Functional difference: load behavior

`/transmitter/config`
- loads whatever transmitter metadata/network/MQTT data is already cached on the receiver
- does **not** actively trigger a `refresh/settings`-style fetch for its domains during page load
- is therefore more cache-dependent

`/transmitter/hardware`
- actively asks the transmitter to republish settings on page load
- uses `rx/cmd/refresh/settings`
- then waits for typed settings caches to become ready
- is therefore more refresh-driven

This is an important architectural difference.

### Functional difference: save granularity

`/transmitter/config`
- batches two domain updates when the user saves:
  - network payload
  - MQTT payload
- submits both even though the button is driven by a single combined “changed settings” count
- is domain-level and coarse-grained

`/transmitter/hardware`
- computes only the changed fields
- submits them one at a time using category/field/value tuples
- is fine-grained and field-level

This means `/transmitter/hardware` is structurally closer to the newer settings architecture, while `/transmitter/config` remains a legacy-style combined configuration form.

### Functional difference: runtime impact

`/transmitter/config`
- changes connectivity behavior
- can affect how the transmitter reaches the network and MQTT broker
- may have reconnect/restart implications depending on transmitter handling

`/transmitter/hardware`
- changes hardware-adjacent runtime behavior and static operating parameters
- many changes are intended to apply immediately and be republished as retained settings
- also exposes live LED runtime status, which makes it partly a configuration page and partly a diagnostic page

### Naming and UX mismatch

There is a mild UX mismatch in the current transmitter hub:
- the `Configuration` card says “Network, MQTT, Settings”, but it actually covers metadata + network + MQTT only
- the `Hardware Config` card subtitle says “Status LED Pattern”, but the page now covers a much wider set of settings including CAN, precharge, contactor, equipment stop, and BMS reset timing

So the two pages are no longer just “config” vs “LED config”; the split has evolved into:
- infrastructure configuration (`/transmitter/config`)
- generic static/runnable hardware settings (`/transmitter/hardware`)

### Consistency across both receiver projects

This split exists in both receiver codebases:
- `espnowreceiver_2`
- `espnowreceiver_LCD`

The logical behavior is the same in both.

The main difference is implementation style:
- the LCD project has some more memory-conscious streaming/static-page optimizations
- the non-LCD project uses more `String`-assembled page content in a few places

That does not materially change the architectural conclusion.

### Recommendation

The two pages should currently be treated as **separate architectural layers**, not duplicates:

- keep `/transmitter/config` for transmitter transport/infrastructure configuration
- keep `/transmitter/hardware` for unified typed operating settings

However, the current UI and naming make them look more similar than they really are. To reduce confusion, the recommended medium-term cleanup is:

1. rename the hub card descriptions to reflect actual scope
2. stop describing `/transmitter/config` as generic “Settings”
3. consider renaming `/transmitter/hardware` to something closer to `Operating Settings` or `Hardware & Control`
4. eventually decide whether network/MQTT should remain on a specialized page or be migrated into a broader schema-driven settings system

### Bottom line

`/transmitter/config` and `/transmitter/hardware` are similar in presentation, but they are **different generations of configuration architecture**:

- `/transmitter/config` = older specialized config page for network + MQTT + metadata
- `/transmitter/hardware` = newer generic typed-settings page for CAN/power/contactor/LED behavior

So they overlap in user intent, but not in data source, save model, or architectural layer.
