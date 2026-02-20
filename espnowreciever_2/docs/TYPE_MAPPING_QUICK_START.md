# Type Mapping Quick Start

## Quick Summary

Battery and inverter types are now **automatically sorted alphabetically** in the web UI dropdowns. When you add a new type, it appears in alphabetical order automatically.

## Files Changed

### `lib/webserver/api/api_handlers.cpp`
- Added `TypeEntry` struct with comparison operator
- Added `battery_types[]` array (47 types)
- Added `inverter_types[]` array (22 types)  
- Added `generateSortedTypeJson()` helper function
- Updated API handlers to use dynamic sorting

### New Documentation
- `docs/TYPE_MAPPINGS.md` - Full reference guide
- `BATTERY_INVERTER_TYPE_SYNC_SOLUTION.md` - Architecture overview
- `scripts/sync_type_mappings.py` - Verification tool

## Adding a New Battery Type

### 1. Add to Battery Emulator
File: `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/battery/BATTERIES.cpp`

```cpp
case BatteryType::NewBatteryName:
    return "Display Name Here";
```

### 2. Update Receiver Array
File: `espnowreciever_2/lib/webserver/api/api_handlers.cpp`

Find `battery_types[]` array and add:
```cpp
{ID_NUMBER, "Display Name Here"},
```

**Get ID from Battery Emulator enum** (e.g., if it's `BatteryType::Foo = 47`, use `{47, "Foo"}`)

### 3. Build & Test
```bash
cd espnowreciever_2
pio run -t upload -t monitor
```

The new type will automatically appear in alphabetical order on the web UI.

## Adding a New Inverter Type

Same process, but update `inverter_types[]` array instead.

## Verification

1. Open receiver web UI
2. Go to `/battery_settings.html` or `/inverter_settings.html`
3. Click the type dropdown
4. Verify types are alphabetically sorted

**Example battery order:**
- None
- BMW PHEV
- BMW i3
- BMW iX
- Bolt/Ampera
- BYD Atto 3
- ...

## How It Works

```
battery_types[] array (unsorted)
        ↓
generateSortedTypeJson() 
        ↓
Create temporary copy
        ↓
std::sort() with TypeEntry::operator<
        ↓
Generate JSON (sorted)
        ↓
Web UI dropdown (alphabetical)
```

The API handler automatically sorts at runtime, so the order in the source array doesn't matter.

## Key Features

✅ **Automatic sorting** - No manual ordering needed  
✅ **Easy to add** - Just add array entry  
✅ **Maintains ID-name mapping** - Type IDs stay correct  
✅ **Future-proof** - New types sort automatically  
✅ **Memory efficient** - Temporary sort only during API call  

## Files to Know

| File | Purpose |
|------|---------|
| `api_handlers.cpp` | Type arrays and sorting logic |
| `docs/TYPE_MAPPINGS.md` | Complete reference |
| `scripts/sync_type_mappings.py` | Verify synchronization |
| `BATTERIES.cpp` (Transmitter) | Battery type definitions |

## Troubleshooting

**New type doesn't appear?**
- Verify ID matches Battery Emulator enum
- Check spelling of name
- Rebuild with `pio run -t clean` then `pio run -t upload`

**Wrong order?**
- API should auto-sort - check browser console for errors
- Clear browser cache (Ctrl+Shift+Delete)

**Compilation error?**
- Ensure includes are present: `<algorithm>` and `<cstring>`
- Check array syntax is correct
- Run `pio run -t clean` first

## Next Steps

1. Read `docs/TYPE_MAPPINGS.md` for complete details
2. Review `BATTERY_INVERTER_TYPE_SYNC_SOLUTION.md` for architecture
3. Run `python3 scripts/sync_type_mappings.py` to verify
4. Add new battery/inverter types as needed

---

**Summary**: When adding a new battery or inverter type:
1. Add to Battery Emulator
2. Add ID+name to receiver array
3. Rebuild
4. Type appears alphabetically sorted automatically ✓
