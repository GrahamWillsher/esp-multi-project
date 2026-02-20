# Battery & Inverter Type Mappings

This document explains how battery and inverter type selections are managed and kept synchronized between the Battery Emulator and the Receiver.

## Architecture

### Battery Types Source
- **Primary Source**: `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/battery/BATTERIES.cpp`
- **Function**: `name_for_battery_type(BatteryType type)`
- **Enum Definition**: `BatteryType` enum in Battery Emulator

### How It Works

1. **Battery Emulator** defines all battery types in the `BatteryType` enum with numeric IDs (0-46)
2. **Each battery type** has a display name defined in `BATTERIES.cpp`
3. **Receiver** maintains hardcoded arrays of battery and inverter types
4. **API handlers** (`api_handlers.cpp`) dynamically sort these arrays alphabetically when returning to the web UI
5. **User selects** from the sorted dropdown list

### Type Synchronization Flow

```
Battery Emulator (Transmitter)
    ↓
    BATTERIES.cpp (battery types)
    ↓
Receiver API Handler
    ↓
    battery_types[] array (hardcoded, IDs + names)
    ↓
    generateSortedTypeJson() (sorts alphabetically)
    ↓
Web UI Dropdown (sorted)
```

## Adding a New Battery Type

When you add a new battery to the Battery Emulator:

### Step 1: Add to Battery Emulator
```cpp
// In ESPnowtransmitter2/.../battery/BATTERIES.cpp
case BatteryType::NewBatteryName:
    return "New Battery Display Name";
```

### Step 2: Update Receiver Type Array
Edit `espnowreciever_2/lib/webserver/api/api_handlers.cpp`:

```cpp
static TypeEntry battery_types[] = {
    // ... existing entries ...
    {NEW_ID, "New Battery Display Name"},
    // ... rest of entries ...
};
```

**Note:** The ID must match the Battery Emulator enum's numeric value for that type.

### Step 3: Verify Alphabetical Order
The API handler automatically sorts these, so the order in the array doesn't matter. The UI will display them alphabetically.

### Step 4: Test
```bash
cd espnowreciever_2
pio run -t upload -t monitor
```

Navigate to `/battery_settings.html` and verify the new battery appears in the sorted list.

## Adding a New Inverter Type

Similar process for inverter types:

### Step 1: Update Receiver Type Array
Edit `espnowreciever_2/lib/webserver/api/api_handlers.cpp`:

```cpp
static TypeEntry inverter_types[] = {
    // ... existing entries ...
    {NEW_ID, "New Inverter Protocol Name"},
    // ... rest of entries ...
};
```

### Step 2: Test
Navigate to `/inverter_settings.html` and verify the new inverter appears in the sorted list.

## Dynamic Sorting Implementation

The API handlers use the `generateSortedTypeJson()` helper function:

```cpp
// Helper function - generates sorted JSON from type array
static String generateSortedTypeJson(TypeEntry* types, size_t count) {
    TypeEntry* sorted_copy = new TypeEntry[count];
    memcpy(sorted_copy, types, count * sizeof(TypeEntry));
    
    // Sort by name (case-insensitive, alphabetically)
    std::sort(sorted_copy, sorted_copy + count);
    
    // Generate JSON...
    delete[] sorted_copy;
    return json;
}
```

**Benefits:**
- ✅ No manual sorting needed in the arrays
- ✅ Any order in the source arrays works
- ✅ Always consistent UI ordering
- ✅ Handles future additions automatically

## Synchronization Tools

### Manual Sync Script
```bash
cd espnowreciever_2
python3 scripts/sync_type_mappings.py
```

This script:
1. Reads BATTERIES.cpp from Battery Emulator
2. Extracts all battery types
3. Displays mapping information
4. Verifies synchronization

### Automatic Build Hook
A `platformio_pre_build.py` hook can optionally auto-generate type mappings during build (not yet enabled by default).

## Current Type Mappings

### Battery Types (47 total)
- ID 0: None
- ID 2-46: Various vehicle batteries (BMW i3, Tesla Model 3/Y, etc.)

**Order in source array:** Unsorted (will be sorted alphabetically by API)
**Display in UI:** Alphabetically sorted (BMW i3, BMW iX, BMW PHEV, Bolt/Ampera, ...)

### Inverter Types (22 total)
- ID 0: None
- ID 1-21: Various inverter protocols (Afore, BYD, Ferroamp, Growatt, etc.)

**Order in source array:** Unsorted (will be sorted alphabetically by API)
**Display in UI:** Alphabetically sorted

## Troubleshooting

### New battery doesn't appear in UI
1. Verify ID is correct in both `BATTERIES.cpp` and receiver array
2. Check that name matches exactly
3. Clear browser cache
4. Rebuild with `pio run -t upload --verbose`

### Dropdown shows incorrect order
1. API handler should auto-sort - check console for errors
2. Verify `generateSortedTypeJson()` is being called
3. Check browser console for JavaScript errors

### Compilation errors
1. Ensure `<algorithm>` and `<cstring>` are included (they are by default)
2. Verify array syntax is correct
3. Rebuild: `pio run -t clean` then `pio run -t upload`

## References

- Battery Emulator: `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/battery/BATTERIES.cpp`
- API Handlers: `espnowreciever_2/lib/webserver/api/api_handlers.cpp`
- Type Definitions: Search for `TypeEntry` struct in `api_handlers.cpp`
- Type Mapping Script: `espnowreciever_2/scripts/sync_type_mappings.py`
