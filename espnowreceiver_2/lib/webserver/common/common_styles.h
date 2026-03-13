#ifndef COMMON_STYLES_H
#define COMMON_STYLES_H

// Common CSS styles for all pages
const char* COMMON_STYLES = R"rawliteral(
    html { font-family: Arial, Helvetica, sans-serif; display: inline-block; text-align: center; }
    body { max-width: 800px; margin: 0px auto; padding: 20px; background-color: #303841; color: white; }
    h1 { color: white; }
    h2 { color: #FFD700; margin-top: 5px; }
    h3 { color: white; margin-top: 20px; }
    .button {
        background-color: #505E67;
        border: none;
        color: white;
        padding: 12px 24px;
        text-decoration: none;
        font-size: 16px;
        margin: 10px;
        cursor: pointer;
        border-radius: 10px;
        display: inline-block;
    }
    .button:hover { background-color: #3A4A52; }
    .info-box {
        background-color: #3a4b54;
        padding: 20px;
        border-radius: 20px;
        margin: 15px 0;
        box-shadow: 0 2px 5px rgba(0, 0, 0, 0.2);
    }
    .info-box h3 {
        color: #fff;
        margin-top: 0;
        margin-bottom: 15px;
        padding-bottom: 8px;
        border-bottom: 1px solid #4d5f69;
    }
    .info-row {
        display: flex;
        justify-content: space-between;
        padding: 8px 0;
        border-bottom: 1px solid #505E67;
    }
    .info-row:last-child { border-bottom: none; }
    .info-label { font-weight: bold; color: #FFD700; }
    .info-value { color: white; }
    .settings-card {
        background-color: #3a4b54;
        padding: 15px 20px;
        margin-bottom: 20px;
        border-radius: 20px;
        box-shadow: 0 2px 5px rgba(0, 0, 0, 0.2);
        text-align: left;
    }
    .settings-card h3 {
        color: #fff;
        margin-top: 0;
        margin-bottom: 15px;
        padding-bottom: 8px;
        border-bottom: 1px solid #4d5f69;
    }
    .settings-row {
        display: grid;
        grid-template-columns: 1fr 1.5fr;
        gap: 10px;
        align-items: center;
        padding: 8px 0;
    }
    label { font-weight: bold; color: #FFD700; }
    input, select {
        max-width: 250px;
        padding: 8px;
        border-radius: 5px;
        border: none;
    }
    .ip-row {
        display: flex;
        align-items: center;
        gap: 6px;
    }
    .octet {
        width: 44px;
        text-align: right;
        margin: 0;
    }
    .dot {
        display: inline-block;
        width: 8px;
        text-align: center;
    }
    .note {
        background-color: #ff9800;
        color: #000;
        padding: 15px;
        border-radius: 10px;
        margin: 20px 0;
        font-weight: bold;
    }
    .settings-note {
        background-color: #ff9800;
        color: #000;
        padding: 15px;
        border-radius: 10px;
        margin: 20px 0;
        font-weight: bold;
    }
    
    /* ========== Material Design Field Styles ========== */
    /* Read-only fields (DHCP mode, informational data) */
    .readonly-field,
    input[type="text"]:disabled,
    input[type="password"]:disabled,
    input.octet:disabled {
        background-color: #FFFFFF !important;
        color: #757575 !important;           /* Material Grey 600 */
        border: 1px solid #E0E0E0 !important; /* Material Grey 300 */
        cursor: not-allowed !important;
    }
    
    /* Editable fields (Static IP mode, user input) */
    .editable-field,
    input[type="text"]:not(:disabled),
    input[type="password"]:not(:disabled),
    input.octet:not(:disabled) {
        background-color: #FFFFFF !important;
        color: #212121 !important;           /* Material Grey 900 */
        border: 1px solid #BDBDBD !important; /* Material Grey 400 */
        cursor: text !important;
    }
    
    /* Focus state for editable fields */
    .editable-field:focus,
    input[type="text"]:not(:disabled):focus,
    input[type="password"]:not(:disabled):focus,
    input.octet:not(:disabled):focus {
        border: 2px solid #4CAF50 !important; /* Material Green 500 - matches save button */
        outline: none !important;
    }
    
    /* Mode badges */
    .network-mode-badge {
        display: inline-block;
        padding: 4px 12px;
        border-radius: 12px;
        font-size: 12px;
        font-weight: 600;
        margin-left: 8px;
        vertical-align: middle;
    }
    
    .badge-dhcp {
        background-color: #E3F2FD; /* Light Blue 50 */
        color: #1976D2;            /* Blue 700 */
    }
    
    .badge-static {
        background-color: #E8F5E9; /* Light Green 50 */
        color: #388E3E;            /* Green 700 */
    }
    
    /* MQTT Status Dot Indicator */
    .status-dot {
        display: inline-block;
        width: 12px;
        height: 12px;
        border-radius: 50%;
        margin-left: 8px;
        vertical-align: middle;
    }
    .status-dot.connected {
        background-color: #28a745;  /* Green */
        box-shadow: 0 0 8px #28a745;
    }
    .status-dot.connecting {
        background-color: #FF9800;  /* Orange */
        animation: pulse 1.5s ease-in-out infinite;
    }
    .status-dot.disconnected {
        background-color: #dc3545;  /* Red */
    }
    @keyframes pulse {
        0%, 100% { opacity: 1; }
        50% { opacity: 0.5; }
    }
)rawliteral";

#endif
