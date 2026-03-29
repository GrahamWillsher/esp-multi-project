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

    <style>
        .hardware-config-grid label[for] {
            cursor: pointer;
        }

        .hardware-config-grid input[type='checkbox']:not(:disabled),
        .hardware-config-grid select:not(:disabled),
        .hardware-config-grid button:not(:disabled) {
            cursor: pointer;
        }
    </style>

    <div class='settings-card'>
        <h3>Hardware config</h3>
        <p style='color: #888; font-size: 13px; margin-top: 0;'>Ported layout/mechanics from the original Battery Emulator Hardware Config section.</p>

        <div class='hardware-config-grid' style='display: grid; grid-template-columns: 1fr 1.5fr; gap: 10px; align-items: center;'>
            <!-- 1 -->
            <label for='canFdAsClassic'>Use CanFD as classic CAN:</label>
            <input type='checkbox' id='canFdAsClassic' class='editable-field' />

            <!-- 2 -->
            <label for='canFreq'>CAN addon crystal (MHz):</label>
            <input type='number' id='canFreq' class='editable-field' min='1' max='1000' step='1' />

            <!-- 3 -->
            <label for='canFdFreq'>CAN-FD-addon crystal (MHz):</label>
            <input type='number' id='canFdFreq' class='editable-field' min='1' max='100' step='1' />

            <!-- 4 -->
            <label for='eqStop'>Equipment stop button:</label>
            <select id='eqStop' class='editable-field'>
                <option value='0'>Not connected</option>
                <option value='1'>Latching</option>
                <option value='2'>Momentary</option>
            </select>

            <!-- 5 -->
            <label for='cntCtrl'>Contactor control via GPIO:</label>
            <input type='checkbox' id='cntCtrl' class='editable-field' />

            <!-- 7 -->
            <label for='prechgMs'>Precharge time ms:</label>
            <input type='number' id='prechgMs' class='editable-field' min='10' max='30000' step='1' />

            <!-- 8 -->
            <label for='ncContactor'>Use Normally Closed logic:</label>
            <input type='checkbox' id='ncContactor' class='editable-field' />

            <!-- 9 -->
            <label for='pwmCntCtrl'>PWM contactor control:</label>
            <input type='checkbox' id='pwmCntCtrl' class='editable-field' />

            <!-- 9a -->
            <label for='pwmFreqHz'>PWM Frequency Hz:</label>
            <input type='number' id='pwmFreqHz' class='editable-field' min='100' max='50000' step='1' />

            <!-- 9b -->
            <label for='pwmHoldDuty'>PWM Hold 1-1023:</label>
            <input type='number' id='pwmHoldDuty' class='editable-field' min='1' max='1023' step='1' />

            <!-- 10 -->
            <label for='perBmsReset'>Periodic BMS reset every 24h:</label>
            <input type='checkbox' id='perBmsReset' class='editable-field' />

            <!-- 10a (internal first-align control hidden; only target time is user-visible) -->
            <label for='bmsFirstAlignTime'>Reset target time (HH:MM):</label>
            <input type='time' id='bmsFirstAlignTime' class='editable-field' step='60' />

            <!-- 11 -->
            <label for='extPrecharge'>External precharge via HIA4V1:</label>
            <input type='checkbox' id='extPrecharge' class='editable-field' />

            <!-- 12 -->
            <label for='maxPreTime'>Precharge, maximum ms before fault:</label>
            <input type='number' id='maxPreTime' class='editable-field' min='100' max='60000' step='1' />

            <!-- 12a -->
            <label for='noInvDisc'>Normally Open (NO) inverter disconnect contactor:</label>
            <input type='checkbox' id='noInvDisc' class='editable-field' />

            <!-- 13 -->
            <label for='ledMode'>Status LED pattern:</label>
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
    </div>
    )rawliteral";

    return content;
}
