from pathlib import Path

def replace_once(path, old, new):
    p = Path(path)
    t = p.read_text(encoding='utf-8')
    if old not in t:
        raise SystemExit(f'Pattern not found in {path}: {old[:100]!r}')
    t = t.replace(old, new, 1)
    p.write_text(t, encoding='utf-8')

comp = Path(r"C:\Users\GrahamWillsher\ESP32Projects\esp32common\docs\systemworks\COMPREHENSIVE_SETTINGS_FINDINGS_AND_PLAN.md")
text = comp.read_text(encoding='utf-8')

# Add Phase 3 implementation status block
old_phase3_tail = "**Rationale:**\n- Versions > 0 indicate transmitter has published\n- Retry logic now waits for actual version update, not just data presence\n- Prevents page from loading default values and stopping\n"
new_phase3_tail = "**Rationale:**\n- Versions > 0 indicate transmitter has published\n- Retry logic now waits for actual version update, not just data presence\n- Prevents page from loading default values and stopping\n\n**Implementation Status: COMPLETE ✓**\n\n**What was actually implemented:**\n\n1. **Updated hardware page retry logic in both receivers**:\n   - `espnowreceiver_2/lib/webserver/pages/hardware_config_page_script.cpp`\n   - `espnowreceiver_LCD/lib/webserver_lcd/pages/hardware_config_page_script.cpp`\n   - `settingsReady` now derives from explicit version checks (`battery_version`, `power_version`, `can_version`, `contactor_version`)\n\n2. **Completed LCD parity for version-aware API/cache flow**:\n   - Added `version` fields to LCD receiver `PowerSettings`, `CanSettings`, `ContactorSettings`\n   - Added version getters to LCD `TransmitterSettingsCache` and `TransmitterManager`\n   - Updated LCD `api_get_battery_settings_handler()` to require all versions > 0 before `settings_ready=true`\n   - Added version fields to LCD API response payload\n\n3. **Completed version hydration from transmitter retained payload**:\n   - Both receiver MQTT clients now store `version` for `power`, `can`, and `contactor` sections in `handleStaticSettings()`\n\n**Build Status: SUCCESS ✓**\n- `espnowreceiver_2`: both environments build successfully\n- `espnowreceiver_LCD`: waveshare environment builds successfully\n"
if old_phase3_tail in text:
    text = text.replace(old_phase3_tail, new_phase3_tail, 1)
else:
    raise SystemExit('Phase 3 rationale block not found')

# Fix success criteria statuses for pending phases
text = text.replace("**Phase 4 (MEDIUM):**\n- ✓ `/transmitter/config` uses same category/field model as `/transmitter/hardware`\n- ✓ Both pages use same refresh/save patterns\n- ✓ Network and MQTT config versions are checked before serving\n\n**Phase 5 (MEDIUM):**\n- ✓ Version numbers increment when settings are updated\n- ✓ Receiver detects version changes and refreshes cache\n- ✓ Audit trail shows when settings changed\n",
"**Phase 4 (MEDIUM) — Pending:**\n- [ ] `/transmitter/config` uses same category/field model as `/transmitter/hardware`\n- [ ] Both pages use same refresh/save patterns\n- [ ] Network and MQTT config versions are checked before serving\n\n**Phase 5 (MEDIUM) — Pending verification/closeout:**\n- [ ] Version numbers increment when settings are updated\n- [ ] Receiver detects version changes and refreshes cache\n- [ ] Audit trail shows when settings changed\n")

# Update next steps to current state
text = text.replace("## Next Steps\n\n1. **Approve Phase 1 Fix:** Add `SettingsManager::instance().init()` call to transmitter startup\n2. **Test Phase 1:** Verify MQTT retained message shows correct values\n3. **Implement Phase 2:** Version-aware API checking\n4. **Implement Phase 3:** Version-based retry logic in pages\n5. **Plan Phase 4:** Architectural alignment for `/transmitter/config`\n6. **Document:** Update relevant README files and architecture docs\n",
"## Next Steps\n\n1. **Hardware validation (required):**\n   - Confirm transmitter startup logs show initialized non-zero settings versions\n   - Verify retained `tx/state/static/settings` contains expected non-default values\n   - Validate `/transmitter/hardware` on both receivers shows persisted transmitter values after reboot\n2. **Close Phase 4:** Plan and implement `/transmitter/config` migration to the generic typed-settings model\n3. **Close Phase 5 verification:** Confirm and document version increment behavior for all settings domains during updates\n4. **Final documentation pass:** Update architecture/README docs once hardware validation is complete\n")

comp.write_text(text, encoding='utf-8')

# Update investigation doc with implementation addendum
inv = Path(r"C:\Users\GrahamWillsher\ESP32Projects\esp32common\docs\systemworks\TRANSMITTER_HARDWARE_SETTINGS_END_TO_END_INVESTIGATION_2026_05_24.md")
it = inv.read_text(encoding='utf-8')
needle = "`SettingsManager::init()` is the code path that loads persisted NVS settings into the in-memory authority object. Without that initialization, the refresh and retained publish path can still function correctly at the transport level, but they will publish default or uninitialized values instead of the saved hardware settings. That exactly matches the observed symptom: `/transmitter/hardware` loads, but values shown on receivers do not reflect the saved transmitter hardware configuration.\n"
insert = needle + "\n### Implementation Update (2026-05-25)\n\nThe primary transmitter bootstrap fix identified in this investigation has now been implemented:\n- transmitter startup now calls `SettingsManager::instance().init()` before static settings publication\n- startup logs now include battery/power/CAN/contactor settings versions\n\nReceiver-side version-aware readiness and retry handling has also been implemented in both receiver projects:\n- API readiness now requires non-zero settings versions\n- hardware page retry logic now uses explicit version checks\n\nResult: the code path now aligns with the investigation findings. Remaining closeout work is hardware validation plus planned `/transmitter/config` architectural alignment.\n"
if needle not in it:
    raise SystemExit('Needle not found in investigation doc')
it = it.replace(needle, insert, 1)
inv.write_text(it, encoding='utf-8')

print('Documentation updated.')
