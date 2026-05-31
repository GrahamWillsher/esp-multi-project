#include "cellmonitor_page_content.h"

const char* get_cellmonitor_page_content() {
    static const char kContent[] = R"rawliteral(
    <h1>Cell Monitor</h1>
    <div class='info-box' style='margin-bottom: 20px;'>
        <div style='display: flex; justify-content: space-between; align-items: center;'>
            <div>
                <strong>Data Source:</strong>
                <span id='cellMode' style='color: #FFD700; font-weight: bold;'>Loading...</span>
            </div>
        </div>
        <p id='cellStatus' style='color: #888; margin-top: 8px;'>Fetching cell data...</p>
        <div style='display: flex; justify-content: center; gap: 30px; margin-top: 8px; color: #ddd;'>
            <span>Deviation: <span id='deviation' style='color: #FFD700;'>-- mV</span></span>
            <span>Balancing Cells: <span id='balancingCount' style='color: #00FFFF;'>--</span></span>
        </div>
    </div>

    <div class='info-box'>
        <h2 style='margin-top: 0; color: #00FFFF;'>Cell Voltages</h2>
        <div id='cellGrid' style='display: grid; grid-template-columns: repeat(9, 1fr); gap: 6px;'></div>
    </div>

    <div class='info-box' style='margin-top: 30px;'>
        <h2 style='margin-top: 0; color: #00FFFF;'>Voltage Distribution</h2>
        <div style='margin-bottom: 10px; color: #888; font-size: 12px;'>
            <span>Min: <strong id='barMin' style='color: #FF6B6B;'>-- mV</strong></span>
            <span style='margin-left: 20px;'>Max: <strong id='barMax' style='color: #4CAF50;'>-- mV</strong></span>
        </div>
        <div id='voltageBar' style='display: flex; flex-direction: row; gap: 2px; height: 120px; width: 100%; background: #111; padding: 4px; align-items: flex-end; box-sizing: border-box;'></div>
    </div>
    )rawliteral";
    return kContent;
}
