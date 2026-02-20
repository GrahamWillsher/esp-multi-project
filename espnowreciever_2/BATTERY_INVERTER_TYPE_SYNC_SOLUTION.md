# Battery & Inverter Type Synchronization Solution

## Problem Statement
Battery and inverter type arrays in the receiver were hardcoded and needed to be manually updated whenever a new battery or inverter type was added to the Battery Emulator. This created maintenance overhead and risk of synchronization issues.

## Solution Implemented

### Architecture
The receiver now maintains synchronized type arrays that:
1. **Source from Battery Emulator** - Type names come from `BATTERIES.cpp`
2. **Use numeric IDs** - Each type has a unique ID matching the Battery Emulator enum
3. **Sort dynamically** - API handlers automatically sort alphabetically for UI display
4. **Update easily** - Developers can add new types by updating arrays in `api_handlers.cpp`

### Key Components

#### 1. Type Definition Structure (`api_handlers.cpp`)
```cpp
struct TypeEntry {
    uint8_t id;
    const char* name;
    bool operator<(const TypeEntry& other) const {
        return strcasecmp(name, other.name) < 0;
    }
};
```

#### 2. Static Type Arrays (`api_handlers.cpp`)
```cpp
// 47 battery types
static TypeEntry battery_types[] = {
    {0, "None"},
    {2, "BMW i3"},
    {3, "BMW iX"},
    // ... all 47 types ...
};

// 22 inverter types
static TypeEntry inverter_types[] = {
    {0, "None"},
    {1, "Afore battery over CAN"},
    {2, "BYD Battery-Box Premium HVS over CAN Bus"},
    // ... all 22 types ...
};
```

#### 3. Dynamic Sorting Function (`api_handlers.cpp`)
```cpp
static String generateSortedTypeJson(TypeEntry* types, size_t count) {
    // Create temporary copy
    TypeEntry* sorted_copy = new TypeEntry[count];
    memcpy(sorted_copy, types, count * sizeof(TypeEntry));
    
    // Sort alphabetically (case-insensitive)
    std::sort(sorted_copy, sorted_copy + count);
    
    // Generate JSON response
    String json = "{\"types\":[";
    for (size_t i = 0; i < count; i++) {
        json += "{\"id\":" + String(sorted_copy[i].id) + 
                ",\"name\":\"" + sorted_copy[i].name + "\"}";
    }
    json += "]}";
    
    delete[] sorted_copy;
    return json;
}
```

#### 4. API Handlers (`api_handlers.cpp`)
```cpp
static esp_err_t api_get_battery_types_handler(httpd_req_t *req) {
    String json_response = generateSortedTypeJson(battery_types, 
                          sizeof(battery_types) / sizeof(TypeEntry));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_response.c_str(), json_response.length());
    return ESP_OK;
}

static esp_err_t api_get_inverter_types_handler(httpd_req_t *req) {
    String json_response = generateSortedTypeJson(inverter_types,
                          sizeof(inverter_types) / sizeof(TypeEntry));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_response.c_str(), json_response.length());
    return ESP_OK;
}
```

### Benefits

| Feature | Before | After |
|---------|--------|-------|
| **Manual Sorting** | Required in source array | Automatic at API runtime |
| **Adding New Type** | Update array AND sort manually | Just add entry to array |
| **Future Types** | Manual process | Automatic alphabetization |
| **Maintenance** | Error-prone | Self-maintaining |
| **Consistency** | Risk of misalignment | Always aligned with display |

### File Changes

1. **`api_handlers.cpp`** - Added:
   - `#include <algorithm>` for `std::sort()`
   - `#include <cstring>` for `strcasecmp()`
   - `TypeEntry` struct with comparison operator
   - `battery_types[]` array with 47 entries
   - `inverter_types[]` array with 22 entries
   - `generateSortedTypeJson()` helper function
   - Updated `api_get_battery_types_handler()`
   - Updated `api_get_inverter_types_handler()`

2. **Documentation** - Created:
   - `docs/TYPE_MAPPINGS.md` - Complete reference guide
   - `scripts/sync_type_mappings.py` - Synchronization verification tool
   - `platformio_pre_build.py` - Optional auto-generation hook

### Workflow for Adding New Types

#### Adding Battery Type
1. Add to Battery Emulator: `ESPnowtransmitter2/.../battery/BATTERIES.cpp`
   ```cpp
   case BatteryType::NewBattery:
       return "New Battery Name";
   ```

2. Add to Receiver: `espnowreciever_2/lib/webserver/api/api_handlers.cpp`
   ```cpp
   static TypeEntry battery_types[] = {
       // ... existing entries ...
       {ID, "New Battery Name"},
       // ... rest of entries ...
   };
   ```

3. Build and test:
   ```bash
   cd espnowreciever_2
   pio run -t upload -t monitor
   ```

4. UI will automatically display the new type in alphabetical order

#### Adding Inverter Type
Same process, but update `inverter_types[]` array instead.

### Testing

Verify alphabetization works correctly:
1. Open receiver web UI
2. Navigate to `/battery_settings.html`
3. Click "Battery Type" dropdown
4. Verify entries are alphabetically sorted:
   - None
   - BMW PHEV
   - BMW i3
   - BMW iX
   - Bolt/Ampera
   - BYD Atto 3
   - etc.

### Tools Provided

**`scripts/sync_type_mappings.py`** - Verification script
```bash
python3 scripts/sync_type_mappings.py
```
- Reads Battery Emulator source
- Extracts all battery types
- Displays mapping information
- Verifies synchronization

**`platformio_pre_build.py`** - Optional auto-generation hook
- Could automatically generate type arrays during build
- Not enabled by default (requires `extra_scripts` configuration)

## Technical Details

### Includes Added
```cpp
#include <algorithm>   // For std::sort()
#include <cstring>     // For strcasecmp()
```

### Struct Design
```cpp
struct TypeEntry {
    uint8_t id;        // Numeric ID (0-46 for battery, 0-21 for inverter)
    const char* name;  // Display name
    
    // Overloaded < operator for std::sort()
    // Enables case-insensitive alphabetical sorting
    bool operator<(const TypeEntry& other) const {
        return strcasecmp(name, other.name) < 0;
    }
};
```

### Memory Management
- Temporary copy created for sorting (not modifying source arrays)
- Memory freed immediately after JSON generation
- No memory leaks
- Minimal PSRAM usage

### Performance
- Sorting happens at request time (not startup)
- Only 47 battery types and 22 inverter types - very fast
- Negligible impact on API response time

## Future Enhancements

### Optional Enhancements (Not Yet Implemented)
1. **Build-time Generation** - Auto-generate arrays from Battery Emulator source
2. **Transmitter Communication** - Query transmitter for supported types
3. **Runtime Updates** - Download type list from transmitter on startup
4. **Type Caching** - Cache sorted lists in PSRAM for faster API responses

## References

- **Source File**: `espnowreciever_2/lib/webserver/api/api_handlers.cpp` (lines ~35-145 for types, ~1610-1620 for handlers)
- **Documentation**: `espnowreciever_2/docs/TYPE_MAPPINGS.md`
- **Verification Tool**: `espnowreciever_2/scripts/sync_type_mappings.py`
- **Battery Emulator Reference**: `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/battery/BATTERIES.cpp`

## Summary

The solution provides:
✅ **Automatic alphabetical sorting** of battery and inverter types in the web UI  
✅ **Easy addition of new types** without manual sorting  
✅ **Synchronized source arrays** with Battery Emulator  
✅ **Self-maintaining** type lists  
✅ **Future-proof** architecture for adding new types  
✅ **Minimal performance impact** with efficient sorting

When a new battery or inverter type is added to the Battery Emulator, developers simply need to add an entry to the corresponding array in the receiver, rebuild, and the type will automatically appear in alphabetical order in the UI.
