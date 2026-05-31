#include "systemtools_hub_page_content.h"

String get_systemtools_hub_page_content() {
    return R"rawliteral(
    <h1>🛠️ System Tools</h1>

    <div class='info-box' style='margin: 20px 0; border-left: 5px solid #FF9800;'>
        <h3 style='margin: 0 0 15px 0;'>⚙️ Functions</h3>
        <div style='display: grid; grid-template-columns: repeat(2, 1fr); gap: 15px;'>
            <a href='/systemtools/debug' style='text-decoration: none;'>
                <div class='info-box' style='cursor: pointer; text-align: center; transition: transform 0.2s, border-color 0.2s; border: 2px solid #FF9800;'
                     onmouseover='this.style.transform="translateY(-3px)"; this.style.borderColor="#ffb74d"'
                     onmouseout='this.style.transform="translateY(0)"; this.style.borderColor="#FF9800"'>
                    <div style='font-size: 36px; margin: 10px 0;'>🐛</div>
                    <div style='font-weight: bold; color: #FF9800; font-size: 16px;'>Debug Logging</div>
                    <div style='font-size: 12px; color: #888; margin-top: 8px;'>Transmitter debug level control</div>
                </div>
            </a>

            <a href='/systemtools/ota' style='text-decoration: none;'>
                <div class='info-box' style='cursor: pointer; text-align: center; transition: transform 0.2s, border-color 0.2s; border: 2px solid #FF9800;'
                     onmouseover='this.style.transform="translateY(-3px)"; this.style.borderColor="#ffb74d"'
                     onmouseout='this.style.transform="translateY(0)"; this.style.borderColor="#FF9800"'>
                    <div style='font-size: 36px; margin: 10px 0;'>📤</div>
                    <div style='font-weight: bold; color: #FF9800; font-size: 16px;'>OTA Update</div>
                    <div style='font-size: 12px; color: #888; margin-top: 8px;'>Receiver + transmitter firmware update</div>
                </div>
            </a>

            <a href='/systemtools/events' style='text-decoration: none;'>
                <div class='info-box' style='cursor: pointer; text-align: center; transition: transform 0.2s, border-color 0.2s; border: 2px solid #FF9800;'
                     onmouseover='this.style.transform="translateY(-3px)"; this.style.borderColor="#ffb74d"'
                     onmouseout='this.style.transform="translateY(0)"; this.style.borderColor="#FF9800"'>
                    <div style='font-size: 36px; margin: 10px 0;'>📋</div>
                    <div style='font-weight: bold; color: #FF9800; font-size: 16px;'>Event Logs</div>
                    <div style='font-size: 12px; color: #888; margin-top: 8px;'>History, errors and live events</div>
                </div>
            </a>
        </div>
    </div>
    )rawliteral";
}
