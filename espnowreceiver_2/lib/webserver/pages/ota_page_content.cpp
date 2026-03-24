#include "ota_page_content.h"

String get_ota_page_content() {
    String content = R"rawliteral(
    <h1>OTA Firmware Update</h1>
    <div style='margin-bottom: 20px;'>
        <a href='/' style='display: inline-block; padding: 10px 16px; background: #4CAF50; color: white; text-decoration: none; border-radius: 6px; font-weight: bold;'>
            ← Dashboard
        </a>
    </div>
    )rawliteral";

    content += R"rawliteral(
    
    <div class='info-box' style='margin-bottom: 20px;'>
        <h3>📊 Current Firmware Versions</h3>
        <table style='width: 100%; border-collapse: collapse; margin-top: 15px;'>
            <tr style='border-bottom: 1px solid #444;'>
                <td style='padding: 10px; font-weight: bold;'>Device</td>
                <td style='padding: 10px; font-weight: bold;'>Version</td>
                <td style='padding: 10px; font-weight: bold;'>Build</td>
            </tr>
            <tr style='border-bottom: 1px solid #444;'>
                <td style='padding: 10px;'>Receiver (This Device)</td>
                <td id='receiverVersion' style='padding: 10px; color: #4CAF50;'>Loading...</td>
                <td id='receiverBuild' style='padding: 10px; color: #888; font-size: 12px;'>Loading...</td>
            </tr>
            <tr>
                <td style='padding: 10px;'>Transmitter</td>
                <td id='transmitterVersion' style='padding: 10px; color: #4CAF50;'>Loading...</td>
                <td id='transmitterBuild' style='padding: 10px; color: #888; font-size: 12px;'></td>
            </tr>
        </table>
        <div id='compatibilityStatus' style='margin-top: 15px; padding: 10px; background-color: rgba(0,0,0,0.3); border-radius: 5px; color: #FFD700;'>
            ⚠️ Waiting for version info
        </div>
    </div>

    )rawliteral";

    content += R"rawliteral(
    <!-- Receiver OTA Section -->
    <div class='info-box' style='text-align: center; margin-bottom: 20px;'>
        <h3>📲 Update Receiver (This Device)</h3>
        <div id='statusReceiver' style='margin: 20px 0; font-size: 18px;'>
            📁 Select firmware file (.bin)
        </div>
        
        <input type='file' id='firmwareFileReceiver' accept='.bin' style='display:none;'>

        <button id='uploadBtnReceiver' class='button'
            style='background-color: #666; font-size: 20px; font-weight: bold; width: 340px; height: 72px; padding: 0 14px; line-height: 1.2; box-sizing: border-box; white-space: pre-line; word-break: break-word; text-align: center; display: inline-flex; align-items: center; justify-content: center;'>Select File First</button>
        
        <div id='progressBarReceiver' style='display:none; width:340px; height:72px; background:#333; border-radius:4px; position:relative; overflow:hidden; margin:0 auto;'>
            <div id='progressFillReceiver' style='height:100%; width:0%; background:#4CAF50; transition:width 0.15s ease;'></div>
            <div id='progressTextReceiver' style='position:absolute; top:50%; left:50%; transform:translate(-50%,-50%); color:#fff; font-size:20px; font-weight:bold;'>0%</div>
        </div>
        
        <div style='margin-top: 15px; color: #FFD700; font-size: 14px;'>
            ⚠️ Device will reboot automatically after update
        </div>
        
    </div>
    
    <!-- Transmitter OTA Section -->
    <div class='info-box' style='text-align: center;'>
        <h3>📡 Update Transmitter</h3>
        <div id='statusTransmitter' style='margin: 20px 0; font-size: 18px;'>
            📁 Select firmware file (.bin)
        </div>
        
        <input type='file' id='firmwareFileTransmitter' accept='.bin' style='display:none;'>

        <button id='uploadBtnTransmitter' class='button'
            style='background-color: #666; font-size: 20px; font-weight: bold; width: 340px; height: 72px; padding: 0 14px; line-height: 1.2; box-sizing: border-box; white-space: pre-line; word-break: break-word; text-align: center; display: inline-flex; align-items: center; justify-content: center;'>Select File First</button>
        
        <div id='progressBarTransmitter' style='display:none; width:340px; height:72px; background:#333; border-radius:4px; position:relative; overflow:hidden; margin:0 auto;'>
            <div id='progressFillTransmitter' style='height:100%; width:0%; background:#4CAF50; transition:width 0.15s ease;'></div>
            <div id='progressTextTransmitter' style='position:absolute; top:50%; left:50%; transform:translate(-50%,-50%); color:#fff; font-size:20px; font-weight:bold;'>0%</div>
        </div>
        
        <div style='margin-top: 15px; color: #FFD700; font-size: 14px;'>
            The transmitter will receive and install the firmware via HTTP
        </div>
        
    </div>
)rawliteral";

    return content;
}