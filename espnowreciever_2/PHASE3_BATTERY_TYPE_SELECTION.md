# Phase 3.1: Battery & Inverter Type Selection Implementation

## Overview
Extends Phase 3 to add user interface for selecting and configuring battery and inverter types on the receiver. Users can now select which battery emulator profile and inverter type the transmitter should use.

## Architecture

### 1. Storage Layer (ReceiverNetworkConfig Extension)
Add new fields to store selected types:
- `battery_type_: uint8_t` - Selected battery type ID
- `inverter_type_: uint8_t` - Selected inverter type ID

NVS Keys:
- `"batt_type"` - Battery type ID
- `"inv_type"` - Inverter type ID

Default values:
- Battery: 29 (PYLON_BATTERY - matching transmitter default)
- Inverter: 0 (NONE - matching transmitter default)

### 2. Web UI Layer

#### Battery Settings Page (`/battery_settings.html`)
- Existing: Parameter adjustment UI (capacity, voltage, current, etc.)
- **NEW**: Battery Type dropdown selector
  - Populated with available battery types
  - Current selection displayed
  - Updates saved to NVS and sent to transmitter

#### Inverter Settings Page (`/inverter_settings.html`)
- Existing: Parameter adjustment UI
- **NEW**: Inverter Type dropdown selector
  - Populated with available inverter types
  - Current selection displayed
  - Updates saved to NVS and sent to transmitter

### 3. API Layer

#### New Endpoints
1. **GET `/api/get_battery_types`**
   - Returns JSON array of available battery types with IDs and names
   - Response: `{"types": [{id: 29, name: "PYLON_BATTERY"}, ...]}`

2. **GET `/api/get_inverter_types`**
   - Returns JSON array of available inverter types with IDs and names
   - Response: `{"types": [{id: 0, name: "NONE"}, {id: 5, name: "SOLAX"}, ...]}`

3. **POST `/api/set_battery_type`**
   - Body: `{"type": 29}`
   - Saves to NVS and triggers transmitter update
   - Response: `{"success": true}`

4. **POST `/api/set_inverter_type`**
   - Body: `{"type": 5}`
   - Saves to NVS and triggers transmitter update
   - Response: `{"success": true}`

5. **GET `/api/get_selected_types`**
   - Returns currently selected battery and inverter types
   - Response: `{"battery_type": 29, "inverter_type": 0}`

### 4. ESP-NOW Communication

#### New Message Type: COMPONENT_TYPE_SELECT
Transmitter receives battery/inverter type selection from receiver via ESP-NOW.

Message Structure:
```cpp
struct ComponentTypeMessage {
    uint8_t message_type;  // COMPONENT_TYPE_SELECT = 50
    uint8_t battery_type;
    uint8_t inverter_type;
    uint16_t reserved;
}
```

Receiver Action:
- Update component types on transmitter
- Switch to selected battery profile
- Reconfigure CAN protocol for inverter type
- Acknowledge back to receiver

### 5. Data Flow

```
User selects battery type in web UI
    ↓
POST /api/set_battery_type
    ↓
Save to ReceiverNetworkConfig NVS
    ↓
Build ESP-NOW ComponentTypeMessage
    ↓
Send via ESP-NOW to transmitter
    ↓
Transmitter updates system_settings
    ↓
Transmitter loads new battery profile
    ↓
New specs published to MQTT
    ↓
Receiver fetches updated specs
    ↓
Spec display pages show new values
```

## Implementation Tasks

- [ ] Extend ReceiverNetworkConfig class
  - [ ] Add battery_type_ and inverter_type_ fields
  - [ ] Update NVS keys
  - [ ] Add getters/setters

- [ ] Create API handlers
  - [ ] GET /api/get_battery_types
  - [ ] GET /api/get_inverter_types
  - [ ] POST /api/set_battery_type
  - [ ] POST /api/set_inverter_type
  - [ ] GET /api/get_selected_types

- [ ] Update web pages
  - [ ] Battery settings page - add type selector
  - [ ] Inverter settings page - add type selector
  - [ ] Add JavaScript to load types and handle selection

- [ ] Add ESP-NOW handler
  - [ ] Create ComponentTypeMessage handler
  - [ ] Send message on type change

- [ ] Testing
  - [ ] Verify types persist in NVS
  - [ ] Verify ESP-NOW transmission works
  - [ ] Verify transmitter switches battery profiles
  - [ ] Verify spec pages update with new values

## Files to Modify

1. **lib/receiver_config/receiver_config_manager.h** - Add fields
2. **lib/receiver_config/receiver_config_manager.cpp** - Implement logic
3. **lib/webserver/pages/battery_settings_page.cpp** - Add UI
4. **lib/webserver/pages/inverter_settings_page.cpp** - Add UI
5. **lib/webserver/api/api_handlers.cpp** - Add endpoints
6. **src/espnow/espnow_callbacks.cpp** - Add handler
7. **docs/PHASE3_PROGRESS.md** - Update status

## Benefits

- ✅ Users can select which battery emulator profile to use
- ✅ Users can select which inverter protocol to emulate
- ✅ Settings persist across power cycles
- ✅ Changes apply automatically via ESP-NOW
- ✅ Enables testing multiple battery/inverter combinations
- ✅ Supports future battery profile expansion

## Testing Checklist

- [ ] Battery type selector appears on web page
- [ ] Inverter type selector appears on web page
- [ ] Dropdown lists populate with available types
- [ ] Selection saves to NVS
- [ ] ESP-NOW message transmits to transmitter
- [ ] Transmitter acknowledges change
- [ ] Selected type persists after receiver reboot
- [ ] Battery specs update when type changes
- [ ] Multiple type switches work correctly

## Status

**Current**: Planning & Requirements
**Next**: Implementation
