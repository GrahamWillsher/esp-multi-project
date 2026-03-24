#include "hardware_config_page_content.h"

String get_hardware_config_page_content() {
    String content = R"rawliteral(
    <div style='margin-bottom: 20px;'>
        <a href='/' style='display: inline-block; padding: 10px 16px; background: #4CAF50; color: white; text-decoration: none; border-radius: 6px; font-weight: bold;'>
            ← Dashboard
        </a>
    </div>

    <h1>Hardware Config</h1>
    )rawliteral";

    content += R"rawliteral(

    <div class='settings-card'>
        <h3>Status LED Pattern</h3>
        <p style='color: #888; font-size: 13px; margin-top: 0;'>Choose the receiver-side simulated LED effect policy synchronized from the transmitter.</p>

        <div class='settings-row'>
            <label for='ledMode'>Status LED Pattern:</label>
            <select id='ledMode' class='editable-field'>
                <option value='0'>Classic</option>
                <option value='1'>Energy Flow</option>
                <option value='2'>Heartbeat</option>
            </select>
        </div>
    </div>

    <div class='settings-card'>
        <h3>Live LED Runtime Status</h3>
        <div class='settings-row'>
            <label>Current Color:</label>
            <span id='liveLedColor' style='font-weight: bold;'>--</span>
        </div>
        <div class='settings-row'>
            <label>Current Effect:</label>
            <span id='liveLedEffect' style='font-weight: bold;'>--</span>
        </div>
        <div class='settings-row'>
            <label>Expected Effect:</label>
            <span id='expectedLedEffect' style='font-weight: bold;'>--</span>
        </div>
        <div class='settings-row'>
            <label>Sync Status:</label>
            <span id='ledSyncStatus' style='font-weight: bold;'>--</span>
        </div>
        <div class='settings-row'>
            <label>Manual Resync:</label>
            <button id='resyncLedBtn' onclick='resyncLedState()' style='padding: 8px 16px; font-size: 13px; background-color: #2196F3; color: white; border: none; border-radius: 4px; cursor: pointer;'>
                Resync LED Now
            </button>
        </div>
        <div id='resyncStatus' style='color: #888; font-size: 12px; margin-top: 8px; min-height: 18px;'></div>
    </div>

    <div style='text-align: center; margin-top: 30px;'>
        <button id='saveHardwareBtn' onclick='saveHardwareSettings()' disabled style='padding: 12px 40px; font-size: 16px; background-color: #6c757d; color: white; border: none; border-radius: 4px; cursor: not-allowed;'>
            Nothing to Save
        </button>
        <div id='saveStatus' style='color: #888; font-size: 12px; margin-top: 12px; min-height: 18px;'></div>
    </div>
    )rawliteral";

    return content;
}
