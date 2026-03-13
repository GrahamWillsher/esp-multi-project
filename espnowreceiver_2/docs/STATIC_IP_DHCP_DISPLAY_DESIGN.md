# Static IP / DHCP Display Design Proposal

## Current Issues

1. **Value Display Logic Problems:**
   - Switching between Static and DHCP doesn't properly update displayed values
   - No clear storage of both DHCP and Static IP configurations
   - When unchecking "Use Static IP", fields don't revert to DHCP values

2. **Visual Design Issues:**
   - Grey background for disabled DHCP fields is unclear
   - No clear visual distinction between "viewing DHCP info" vs "editing Static IP"
   - Color scheme inconsistent with industry standards for read-only vs editable fields

3. **Future Consistency Concerns:**
   - Need to establish pattern for other read-only fields (MAC address, device name, firmware version)
   - Must be consistent across entire UI

---

## **APPROVED DESIGN DECISIONS**

1. ✅ **Visual Style:** Material Design (Option A)
2. ✅ **Mode Indicator:** Badge next to section title (Proposal 1)
3. ✅ **API Strategy:** Transmitter stores static IP config, sends current DHCP values when in DHCP mode
4. ✅ **Color Palette:** Material Design colors approved
5. ✅ **Additional Fields:** None at present (MAC, firmware will follow same pattern when implemented)

---

## Proposed Solution

### 1. Data Storage Strategy

**Store both configurations separately:**

```javascript
// Global variables
let dhcpConfig = {
    ip: null,
    gateway: null,
    subnet: null
};

let staticConfig = {
    ip: null,
    gateway: null,
    subnet: null,
    dns1: null,
    dns2: null
};

let savedStaticConfig = {
    ip: null,
    gateway: null,
    subnet: null,
    dns1: null,
    dns2: null
};
```

**Loading behavior:**
- When page loads, fetch current network config from transmitter
- If transmitter is using DHCP: Store current IP as `dhcpConfig`, use defaults or previous save for `staticConfig`
- If transmitter is using Static: Store current IP as `staticConfig`, query transmitter for actual DHCP values
- Store the last saved static configuration separately in `savedStaticConfig`

**Toggle behavior:**
- **DHCP → Static:** 
  - Load `savedStaticConfig` into fields (or defaults if never saved)
  - Fields become editable (white background, black text)
  - DNS fields appear
  - Warning message appears
  
- **Static → DHCP:**
  - Load `dhcpConfig` into fields
  - Fields become read-only (white background, blue/grey text)
  - DNS fields disappear
  - Warning message disappears

---

### 2. Visual Design - Material Design Style (APPROVED)

**Read-Only Fields (DHCP mode, MAC address, firmware version, etc.):**
- Background: `#FFFFFF` (white)
- Text color: `#757575` (medium grey - clearly readable but visually "muted")
- Border: `1px solid #E0E0E0` (light grey border)
- Cursor: `not-allowed` or `default`
- Optional: Small info icon (ℹ️) or lock icon (🔒) to indicate read-only

**Editable Fields (Static IP mode):**
- Background: `#FFFFFF` (white)
- Text color: `#212121` (dark grey/black - high contrast)
- Border: `1px solid #BDBDBD` (medium grey)
- Border on focus: `2px solid #4CAF50` (green accent - matches save button)
- Cursor: `text`

**Visual Example:**
```
┌─────────────────────────────────────────┐
│ IP Address:  [192].[168].[1  ].[100]   │  ← Read-only (grey text)
│ Gateway:     [192].[168].[1  ].[1  ]   │  ← Read-only (grey text)
│ Subnet:      [255].[255].[255].[0  ]   │  ← Read-only (grey text)
└─────────────────────────────────────────┘

When toggled to Static:

┌─────────────────────────────────────────┐
│ IP Address:  [192].[168].[1  ].[150]   │  ← Editable (black text, green border on focus)
│ Gateway:     [192].[168].[1  ].[1  ]   │  ← Editable
│ Subnet:      [255].[255].[255].[0  ]   │  ← Editable
│ DNS Primary: [8  ].[8  ].[8  ].[8  ]   │  ← Editable (newly visible)
└─────────────────────────────────────────┘
```

---

### 3. Mode Indicator - Badge Style (APPROVED)

**Clear visual indication of current mode using badge next to section title:**

```
┌──────────────────────────────────────────────┐
│ Ethernet IP Configuration    [DHCP Mode]    │  ← Badge shows current mode
│                                               │
│ ☑ Use Static IP                              │
└──────────────────────────────────────────────┘
```

- **DHCP Mode Badge:** Light blue background (`#E3F2FD`), blue text (`#1976D2`)
- **Static Mode Badge:** Light green background (`#E8F5E9`), green text (`#388E3C`)

---

### 4. Complete Visual Specification

#### CSS Color Palette

```css
/* Read-Only Field Style */
.readonly-field {
    background-color: #FFFFFF;
    color: #757575;            /* Material Grey 600 */
    border: 1px solid #E0E0E0; /* Material Grey 300 */
    cursor: not-allowed;
}

/* Editable Field Style */
.editable-field {
    background-color: #FFFFFF;
    color: #212121;            /* Material Grey 900 */
    border: 1px solid #BDBDBD; /* Material Grey 400 */
    cursor: text;
}

.editable-field:focus {
    border: 2px solid #4CAF50; /* Material Green 500 - matches save button */
    outline: none;
}

/* Mode Badges */
.badge-dhcp {
    background-color: #E3F2FD; /* Light Blue 50 */
    color: #1976D2;            /* Blue 700 */
    padding: 4px 12px;
    border-radius: 12px;
    font-size: 12px;
    font-weight: 500;
}

.badge-static {
    background-color: #E8F5E9; /* Light Green 50 */
    color: #388E3C;            /* Green 700 */
    padding: 4px 12px;
    border-radius: 12px;
    font-size: 12px;
    font-weight: 500;
}
```

---

### 5. Implementation Plan

#### Phase 1: Transmitter API Enhancement ⚠️ **REQUIRED**
- [ ] **Transmitter must store static IP configuration in NVS** (separate from current DHCP values)
- [ ] Update transmitter to send **both** configs in network status message:
  - Current IP/Gateway/Subnet (DHCP-assigned or Static, depending on mode)
  - Saved Static IP/Gateway/Subnet/DNS (from NVS)
  - `use_static_ip` flag
- [ ] Receiver API endpoint `/api/get_network_config` returns both configs

#### Phase 2: Receiver Data Management
- [ ] Add `dhcpConfig`, `staticConfig` variables in settings_page.cpp
- [ ] Update `loadNetworkConfig()` to populate both configs from API
- [ ] Store user's static IP entries when they type (before saving)

#### Phase 3: Visual Updates (Receiver)
- [ ] Add CSS classes: `.readonly-field`, `.editable-field`, `.badge-dhcp`, `.badge-static`
- [ ] Add mode badge to section header
- [ ] Update IP/Gateway/Subnet fields to use new CSS classes
- [ ] Apply same style to other read-only fields when implemented (MAC, firmware version)

#### Phase 4: Toggle Behavior (Receiver)
- [ ] When unchecking "Use Static IP":
  - Load DHCP values into fields
  - Apply `.readonly-field` CSS
  - Set `readonly="true"` attribute
  - Hide DNS fields
  - Update badge to "DHCP Mode" (blue)
  
- [ ] When checking "Use Static IP":
  - Load saved static values (from API)
  - Apply `.editable-field` CSS
  - Remove `readonly` attribute
  - Show DNS fields
  - Update badge to "Static Mode" (green)
  - Show warning message

#### Phase 5: Save Behavior (Receiver → Transmitter)
- [ ] When saving static config:
  - Send to transmitter via ESP-NOW
  - Transmitter saves to NVS
  - Transmitter applies configuration
  - Transmitter sends acknowledgment
  - Receiver updates display

#### Phase 6: Consistency Across UI
- [ ] Document style guide for future fields
- [ ] Apply read-only style to MAC address, firmware version, device name when implemented

---

### 6. Example HTML Structure (Proposed)

```html
<div class='settings-card'>
    <div style='display: flex; justify-content: space-between; align-items: center; margin-bottom: 15px;'>
        <h3>Ethernet IP Configuration</h3>
        <span id='networkModeBadge' class='badge-dhcp'>DHCP Mode</span>
    </div>
    
    <div class='settings-row'>
        <label>Use Static IP:</label>
        <input type='checkbox' id='staticIpEnabled' onclick='toggleNetworkFields()' />
    </div>
    
    <div class='settings-row'>
        <label>IP Address:</label>
        <div class='ip-row'>
            <input class='octet readonly-field' id='ip0' type='text' maxlength='3' value='192' readonly />
            <span class='dot'>.</span>
            <input class='octet readonly-field' id='ip1' type='text' maxlength='3' value='168' readonly />
            <span class='dot'>.</span>
            <input class='octet readonly-field' id='ip2' type='text' maxlength='3' value='1' readonly />
            <span class='dot'>.</span>
            <input class='octet readonly-field' id='ip3' type='text' maxlength='3' value='100' readonly />
        </div>
    </div>
    <!-- ... more fields ... -->
</div>
```

---

### 7. Behavior Flow Diagram

```
┌─────────────────────────────────────────────────┐
│ Page Loads                                      │
│ ↓                                               │
│ Fetch /api/get_network_config                  │
│ ↓                                               │
│ Is use_static_ip = true?                       │
│ ├─ YES → Store as staticConfig                 │
│ │         Display static values                │
│ │         Fields: editable, badge: "Static"    │
│ └─ NO  → Store as dhcpConfig                   │
│           Display DHCP values                  │
│           Fields: read-only, badge: "DHCP"     │
└─────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────┐
│ User Clicks "Use Static IP" Checkbox            │
│ ↓                                               │
│ Checkbox checked?                               │
│ ├─ YES → Load savedStaticConfig (or defaults)  │
│ │         Set readonly=false                   │
│ │         Apply editable-field CSS             │
│ │         Show DNS fields                      │
│ │         Badge → "Static Mode" (green)        │
│ │         Show warning message                 │
│ └─ NO  → Load dhcpConfig                       │
│           Set readonly=true                    │
│           Apply readonly-field CSS             │
│           Hide DNS fields                      │
│           Badge → "DHCP Mode" (blue)           │
│           Hide warning message                 │
└─────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────┐
│ User Saves Configuration                        │
│ ↓                                               │
│ Store current field values in savedStaticConfig│
│ Send to transmitter via ESP-NOW                │
│ Update initialTransmitterConfig                │
│ Reset save button                              │
└─────────────────────────────────────────────────┘
```

---

### 8. API Enhancement - REQUIRED IMPLEMENTATION

**Current API Response (Insufficient):**
```json
{
  "success": true,
  "use_static_ip": false,
  "ip": "192.168.1.100",
  "gateway": "192.168.1.1",
  "subnet": "255.255.255.0",
  "config_version": 5
}
```
*Problem: Only returns current IP, no way to get saved static config when in DHCP mode*

**APPROVED Enhanced API Response:**
```json
{
  "success": true,
  "use_static_ip": false,
  "current": {
    "ip": "192.168.1.100",
    "gateway": "192.168.1.1",
    "subnet": "255.255.255.0"
  },
  "static_config": {
    "ip": "192.168.1.150",
    "gateway": "192.168.1.1",
    "subnet": "255.255.255.0",
    "dns_primary": "8.8.8.8",
    "dns_secondary": "8.8.4.4"
  },
  "config_version": 5
}
```

**Transmitter Requirements:**
1. ✅ Store static IP configuration in NVS (already implemented)
2. ⚠️ **NEW:** Send both current IP and saved static config in network status message
3. ⚠️ **NEW:** Populate `static_config` even when `use_static_ip = false`

**Benefits:**
- Receiver can display DHCP values while preserving user's static IP entries
- Smooth toggle between modes without losing configuration
- No need for localStorage workarounds

---

### 9. Style Guide for All UI Fields

| Field Type | Background | Text Color | Border | Cursor | Example |
|------------|------------|------------|--------|--------|---------|
| **Editable** (Static IP fields when enabled) | `#FFFFFF` | `#212121` | `#BDBDBD` | `text` | IP Address (static mode) |
| **Read-Only Info** (DHCP IP, MAC, Firmware) | `#FFFFFF` | `#757575` | `#E0E0E0` | `not-allowed` | IP Address (DHCP mode), MAC Address |
| **Disabled Config** (MQTT disabled fields) | `#F5F5F5` | `#BDBDBD` | `#E0E0E0` | `not-allowed` | MQTT server when MQTT disabled |
| **Focus State** (Active editing) | `#FFFFFF` | `#212121` | `#4CAF50` 2px | `text` | Currently typing in field |

**Consistency Rules:**
1. All read-only informational fields use grey text on white background
2. All editable fields use black text on white background
3. All disabled/inactive fields use light grey background with light grey text
4. Focus always uses green border matching the save button
5. No field should use the old grey background for DHCP mode

---

### 10. Recommendation

**Visual Style:** Option A (Material Design) - Clean, widely recognized, accessible

**Mode Indicator:** Proposal 1 (Badge) - Compact and immediately visible

**Color Scheme:**
- Read-only: White background, grey text (`#757575`)
- Editable: White background, black text (`#212121`), green focus border
- Mode badges: Blue for DHCP, Green for Static

**Implementation Priority:**
1. Fix toggle behavior to swap between DHCP and Static values correctly
2. Add mode badge indicator
3. Update CSS to use white background with grey text for read-only
4. Apply same style to MAC address, firmware version, and other info fields
5. Consider API enhancement for future (but work with current API for now)

---

## FINAL IMPLEMENTATION SUMMARY

### **Approved Design**
- ✅ **Visual Style:** Material Design - white backgrounds, grey text for read-only, black text for editable
- ✅ **Mode Indicator:** Badge next to section title (blue for DHCP, green for Static)
- ✅ **Color Palette:** Material Design colors (#757575 read-only, #212121 editable, #4CAF50 focus)
- ✅ **API Strategy:** Transmitter stores and sends both current IP and saved static config

### **Critical Changes Required**

#### **Transmitter Side:**
1. Continue storing static IP config in NVS (already done)
2. **NEW:** Modify network status message to include both:
   - Current IP (DHCP-assigned or static, depending on mode)
   - Saved static IP configuration (always sent, even in DHCP mode)
3. Update ESP-NOW message structure if needed for additional data

#### **Receiver Side:**
1. Update API handler to parse both configs from transmitter
2. Add CSS for Material Design styling
3. Add mode badge indicator
4. Store both DHCP and static configs in JavaScript
5. Update toggle behavior to swap between configs smoothly
6. Apply read-only vs editable styling based on mode

### **User Experience Flow**
```
DHCP Mode (default):
- Badge shows "DHCP Mode" (blue)
- IP fields show current DHCP-assigned values
- Fields are read-only (white bg, grey text)
- DNS fields hidden
- User can see their actual network configuration

User checks "Use Static IP":
- Badge changes to "Static Mode" (green)
- IP fields load saved static configuration (or defaults if never saved)
- Fields become editable (white bg, black text, green focus)
- DNS fields appear
- Warning message appears
- User can edit and save

User unchecks "Use Static IP":
- Badge changes back to "DHCP Mode" (blue)
- IP fields reload current DHCP values
- Fields become read-only again
- DNS fields hide
- Warning message disappears
- Saved static config preserved for future use
```

### **Benefits**
- ✅ Clear visual distinction between modes
- ✅ No lost configuration when toggling
- ✅ Consistent styling across entire UI
- ✅ Industry-standard, accessible design
- ✅ Smooth, intuitive user experience

---

## Questions for Review

1. **Visual Style:** Do you prefer Material Design (Option A), GitHub (Option B), or Bootstrap (Option C)?
2. **Mode Indicator:** Badge (Proposal 1), Status Bar (Proposal 2), or Section Header (Proposal 3)?
3. **API Enhancement:** Should we request transmitter to store/return both DHCP and Static configs, or use localStorage for static config when in DHCP mode?
4. **Additional Fields:** Are there other fields (beyond MAC, firmware version) that should use the read-only style?
5. **Color Preferences:** Any adjustment to the proposed color palette?

---

## Next Steps

Once approved:
1. Implement data storage for both configs
2. Update CSS with approved styles
3. Add mode indicator
4. Update toggle logic
5. Test thoroughly with both modes
6. Document style guide for future development
