#include "receiver_hub_page_content.h"

String get_receiver_hub_page_content(const String& device_subtitle,
                                     const String& status_color,
                                     const String& status_text,
                                     const String& ip_text,
                                     const String& version_text,
                                     const String& build_date) {
    String content = R"rawliteral(
    <h1>📱 Receiver <span style='font-size: 0.5em; font-weight: 600;'>()rawliteral";
    content += device_subtitle;
    content += R"rawliteral()</span></h1>

    <div class='info-box' style='margin: 20px 0; border-left: 5px solid )rawliteral";
    content += status_color;
    content += R"rawliteral(;'>
        <h3 style='margin: 0 0 15px 0;'>📊 Status Summary</h3>
        <div style='display: grid; grid-template-columns: repeat(2, 1fr); gap: 15px;'>
            <div>
                <div style='color: #888; font-size: 13px;'>Connection</div>
                <div style='font-size: 18px; font-weight: bold; color: )rawliteral";
    content += status_color;
    content += R"rawliteral(; margin-top: 5px;'>)rawliteral";
    content += status_text;
    content += R"rawliteral(</div>
            </div>
            <div>
                <div style='color: #888; font-size: 13px;'>IP Address</div>
                <div style='font-size: 16px; font-weight: bold; margin-top: 5px; font-family: monospace;'>)rawliteral";
    content += ip_text;
    content += R"rawliteral(</div>
            </div>
            <div>
                <div style='color: #888; font-size: 13px;'>Firmware</div>
                <div style='font-size: 16px; font-weight: bold; margin-top: 5px;'>)rawliteral";
    content += version_text;
    content += R"rawliteral(</div>
            </div>
            <div>
                <div style='color: #888; font-size: 13px;'>Build Date</div>
                <div style='font-size: 13px; margin-top: 5px; color: #888;'>)rawliteral";
    content += build_date;
    content += R"rawliteral(</div>
            </div>
        </div>
    </div>

    <h3 style='margin: 30px 0 15px 0;'>⚙️ Functions</h3>
    <div style='display: grid; grid-template-columns: repeat(2, 1fr); gap: 15px;'>
        <a href='/receiver/config' style='text-decoration: none;'>
            <div class='info-box' style='cursor: pointer; text-align: center; transition: transform 0.2s, border-color 0.2s; border: 2px solid #4CAF50;'
                 onmouseover='this.style.transform="translateY(-3px)"; this.style.borderColor="#6CCF84"'
                 onmouseout='this.style.transform="translateY(0)"; this.style.borderColor="#4CAF50"'>
                <div style='font-size: 36px; margin: 10px 0;'>⚙️</div>
                <div style='font-weight: bold; color: #4CAF50; font-size: 16px;'>Configuration</div>
                <div style='font-size: 12px; color: #888; margin-top: 8px;'>WiFi, IP, MQTT, Display</div>
            </div>
        </a>

        <a href='/receiver/memoryhealth' style='text-decoration: none;'>
            <div class='info-box' style='cursor: pointer; text-align: center; transition: transform 0.2s, border-color 0.2s; border: 2px solid #4CAF50;'
                 onmouseover='this.style.transform="translateY(-3px)"; this.style.borderColor="#6CCF84"'
                 onmouseout='this.style.transform="translateY(0)"; this.style.borderColor="#4CAF50"'>
                <div style='font-size: 36px; margin: 10px 0;'>🧠</div>
                <div style='font-weight: bold; color: #4CAF50; font-size: 16px;'>Memory Health</div>
                <div style='font-size: 12px; color: #888; margin-top: 8px;'>Pressure gate + heap</div>
            </div>
        </a>
    </div>
    )rawliteral";

    return content;
}
