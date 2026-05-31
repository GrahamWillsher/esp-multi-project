# Theme Cascade Architecture & Usage Contract

## Overview
The theme cascade provides deterministic blue (`#2196F3`) coloring for `/transmitter/*` pages and green (`#4CAF50`) coloring for `/receiver/*` pages across both receiver projects (`espnowreceiver_2` and `espnowreceiver_LCD`).

The cascade is implemented through:
1. **URI-based theme injection**: Body class set by `page_generator.cpp`
2. **CSS variables**: `:root` defines `--primary-color`, theme classes override it
3. **Semantic classes**: `.theme-panel`, `.theme-primary-btn`, etc., backed by CSS variables
4. **Script-level integration**: JavaScript reads CSS variables via `getPrimaryColor()`

---

## Theme Contract

### For Transmitter Pages (`/transmitter/*`)

**✅ DO:**
- Use `.theme-panel` for section containers
- Use `.theme-panel-title` for section headings
- Use `.theme-primary-btn` for action buttons (save, submit, etc.)
- Use `.theme-nav-btn` for navigation controls
- Use `.theme-key-text` for emphasized text
- Use `.theme-status-indicator` for status badges
- Read CSS variable: `getPrimaryColor()` for dynamic color assignments
- Use `var(--primary-color)` in CSS for all theme-dependent styles

**❌ DON'T:**
- Hardcode `#2196F3` (transmitter blue) in HTML, CSS, or JavaScript
- Hardcode `#4CAF50` (receiver green) — it has no place here
- Use inline `style="color: #2196F3"` or similar
- Set button/text colors without going through CSS variables
- Create page-specific color classes or styles

**Why:** Hardcoded colors break the cascade and create maintenance nightmares. The body class handles theme selection once; semantic classes and variables do the rest.

---

### For Receiver Pages (`/receiver/*`)

**✅ DO:**
- Use the same semantic classes as transmitter pages
- Semantics don't change — only the CSS variable value changes (`body.receiver-theme` overrides `--primary-color` to `#4CAF50`)
- Read `getPrimaryColor()` for script-level color assignments
- Trust the cascade: your pages inherit green automatically

**❌ DON'T:**
- Hardcode `#4CAF50` (receiver green) in page code
- Hardcode `#2196F3` (transmitter blue) — it has no place here
- Create receiver-specific color classes
- Override the theme class or redefine color variables per page

**Why:** The whole point is "write once, render with correct colors in both contexts." Hardcoding breaks this.

---

## CSS Variables

### Available Variables
```css
:root {
  --primary-color: #2196F3;      /* Default: transmitter blue */
  --primary-bg: rgba(33, 150, 243, 0.1);
  --primary-border: rgba(33, 150, 243, 0.3);
}

body.receiver-theme {
  --primary-color: #4CAF50;      /* Receiver green */
  --primary-bg: rgba(76, 175, 80, 0.1);
  --primary-border: rgba(76, 175, 80, 0.3);
}
```

### Usage in CSS
```css
.my-button {
  background-color: var(--primary-color);
  border-color: var(--primary-border);
}
```

### Usage in JavaScript
```javascript
function getPrimaryColor() {
  return getComputedStyle(document.documentElement)
    .getPropertyValue('--primary-color')
    .trim();
}

// Use it:
button.style.backgroundColor = getPrimaryColor();
```

---

## Semantic Classes Reference

| Class | Purpose | Styling |
|-------|---------|---------|
| `.theme-panel` | Section container | Left border in theme color, padding, border-radius |
| `.theme-panel-title` | Section heading | Text in theme color, bottom border, bold |
| `.theme-nav-btn` | Navigation button | Theme color background, text decoration on hover |
| `.theme-primary-btn` | Primary action button | Theme color background, border, disabled state handling |
| `.theme-key-text` | Important text | Text in theme color, bold |
| `.theme-status-indicator` | Status badge | Circle with theme color background |

---

## Implementation Pattern

### HTML Example
```html
<!-- ✅ CORRECT: Use semantic classes -->
<div class="theme-panel">
  <h3 class="theme-panel-title">Settings</h3>
  <input type="text" id="setting1" />
  <button class="theme-primary-btn" onclick="saveSetting()">Save</button>
</div>

<!-- ❌ WRONG: Hardcoded color -->
<div style="border-left: 4px solid #2196F3;">
  <h3 style="color: #2196F3;">Settings</h3>
  <button style="background-color: #4CAF50;">Save</button>
</div>
```

### JavaScript Example
```javascript
// ✅ CORRECT: Read CSS variable
function getPrimaryColor() {
  return getComputedStyle(document.documentElement)
    .getPropertyValue('--primary-color')
    .trim();
}

function saveSetting() {
  const primaryColor = getPrimaryColor();
  button.style.backgroundColor = primaryColor;  // Inherits theme automatically
}

// ❌ WRONG: Hardcoded color
function saveSetting() {
  button.style.backgroundColor = '#4CAF50';  // Only works in receiver pages!
}
```

---

## Theme Flow Diagram

```
Request arrives: GET /transmitter/config
           ↓
page_generator.cpp inspects URI
           ↓
Detects /transmitter → injects <body class="transmitter-theme">
           ↓
CSS applies: :root { --primary-color: #2196F3; }
           ↓
Page uses .theme-primary-btn { background-color: var(--primary-color); }
           ↓
Result: Blue button ✅

---

Request arrives: GET /receiver/config
           ↓
page_generator.cpp inspects URI
           ↓
Detects /receiver → injects <body class="receiver-theme">
           ↓
CSS applies: body.receiver-theme { --primary-color: #4CAF50; }
           ↓
Page uses .theme-primary-btn { background-color: var(--primary-color); }
           ↓
Result: Green button ✅

---

NO PER-PAGE LOGIC NEEDED. The cascade handles everything.
```

---

## Compliance Validation

### Static Checks
Use `theme_compliance_checker.py` to validate:
```bash
python scripts/theme_compliance_checker.py <project_path>
```

The checker ensures:
- Transmitter pages contain no `#4CAF50`
- Receiver pages contain no `#2196F3`
- All theme colors come from CSS variables or semantic classes

### Pre-Commit Hook (Optional)
Add to `.git/hooks/pre-commit`:
```bash
#!/bin/bash
python scripts/theme_compliance_checker.py . || exit 1
```

---

## Files Implementing the Contract

### Core Infrastructure
- `lib/webserver/common/common_styles.h` — CSS variables and semantic classes
- `lib/webserver/common/page_generator.cpp` — URI-based theme class injection
- `lib/webserver/pages/settings_page.cpp` — Example of proper CSS variable usage

### Equivalent for espnowreceiver_LCD
- `lib/webserver_lcd/common/common_styles.h`
- `lib/webserver_lcd/common/page_generator.cpp`
- `lib/webserver_lcd/pages/settings_page.cpp`

---

## Migration Path for Existing Pages

If adding a new page or updating an existing one:

1. **Use semantic classes for structure:**
   ```html
   <div class="theme-panel">
     <h3 class="theme-panel-title">My Section</h3>
     ...
   </div>
   ```

2. **Avoid inline styles or hardcoded colors:**
   ```javascript
   // Bad:
   element.style.color = '#2196F3';
   
   // Good:
   element.style.color = getPrimaryColor();
   ```

3. **Verify with compliance checker:**
   ```bash
   python scripts/theme_compliance_checker.py <project_path>
   ```

4. **Build and test both transmitter and receiver pages:**
   - Transmitter pages should be blue
   - Receiver pages should be green
   - No per-page logic or overrides needed

---

## Rollback & Testing

If a change breaks theme compliance:
1. Run `theme_compliance_checker.py` to identify violations
2. Replace hardcoded colors with CSS variables
3. Rebuild and test both theme variants
4. Re-run checker before committing

---

## Future Enhancements

- **CI Integration**: Add checker to pre-commit hooks or CI pipeline
- **Additional Themes**: Current structure supports easy addition of more themes (dark mode, high contrast, etc.)
- **Documentation**: Keep this contract updated as new semantic classes are added
- **Testing**: Add visual regression tests for theme variants

---

## Contact & Questions

For theme-related issues:
1. Check this contract document first
2. Run `theme_compliance_checker.py` to identify violations
3. Refer to the semantic class definitions in `common_styles.h`
4. Update the analysis document if the implementation contract changes
