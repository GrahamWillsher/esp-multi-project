#include "cellmonitor_page.h"
#include "../common/page_generator.h"

static esp_err_t cellmonitor_handler(httpd_req_t *req) {
    String content = R"rawliteral(
    <h1>Cell Monitor</h1>
    <div class='info-box' style='margin-bottom: 20px;'>
        <div style='display: flex; justify-content: space-between; align-items: center;'>
            <div>
                <strong>Data Source:</strong>
                <span id='cellMode' style='color: #FFD700; font-weight: bold;'>Loading...</span>
            </div>
            <a href='/' style='color: #4CAF50; text-decoration: none; font-weight: bold;'>← Back to Dashboard</a>
        </div>
        <p id='cellStatus' style='color: #888; margin-top: 8px;'>Fetching cell data...</p>
    </div>

    <div class='info-box' style='margin-bottom: 20px;'>
        <h2 style='margin-top: 0; color: #00FFFF;'>Statistics</h2>
        <div style='display: grid; grid-template-columns: 1fr 1fr; gap: 15px;'>
            <div>
                <strong>Max Voltage:</strong> <span id='maxVoltage' style='color: #4CAF50;'>-- mV</span>
            </div>
            <div>
                <strong>Min Voltage:</strong> <span id='minVoltage' style='color: #FF6B6B;'>-- mV</span>
            </div>
            <div>
                <strong>Deviation:</strong> <span id='deviation' style='color: #FFD700;'>-- mV</span>
            </div>
            <div>
                <strong>Balancing Cells:</strong> <span id='balancingCount' style='color: #00FFFF;'>--</span>
            </div>
        </div>
    </div>

    <div class='info-box'>
        <h2 style='margin-top: 0; color: #00FFFF;'>Cell Voltages</h2>
        <div id='cellGrid' style='display: grid; grid-template-columns: repeat(9, 1fr); gap: 6px;'></div>
    </div>

    <script>
        function renderCells(cells, balancing, minV, maxV) {
            const grid = document.getElementById('cellGrid');
            grid.innerHTML = '';

            if (!cells || cells.length === 0) {
                grid.innerHTML = '<div style="color:#888">No cell data available</div>';
                return;
            }

            const min = Math.min(...cells);
            const max = Math.max(...cells);
            const deviation = max - min;

            // Update statistics
            document.getElementById('maxVoltage').textContent = max + ' mV';
            document.getElementById('minVoltage').textContent = min + ' mV';
            document.getElementById('deviation').textContent = deviation + ' mV';
            
            const balancingCount = balancing ? balancing.filter(b => b).length : 0;
            document.getElementById('balancingCount').textContent = balancingCount;

            // Render cells in grid
            cells.forEach((mv, idx) => {
                const cell = document.createElement('div');
                cell.style.cssText = 'padding: 6px; border-radius: 6px; text-align: center; cursor: pointer; transition: transform 0.2s;';
                
                // Background color
                if (balancing && balancing[idx]) {
                    cell.style.backgroundColor = 'rgba(0, 255, 255, 0.2)';
                    cell.style.border = '2px solid #00FFFF';
                } else {
                    cell.style.backgroundColor = 'rgba(76, 175, 80, 0.1)';
                    cell.style.border = '1px solid #333';
                }
                
                // Highlight min/max cells
                if (mv === min || mv === max) {
                    cell.style.borderColor = '#FF6B6B';
                    cell.style.borderWidth = '2px';
                }
                
                // Red text for low voltage
                const textColor = mv < 3000 ? '#FF6B6B' : (balancing && balancing[idx] ? '#00FFFF' : '#4CAF50');
                
                cell.innerHTML = `
                    <div style='font-size: 10px; color: #888; margin-bottom: 2px;'>Cell ${idx + 1}</div>
                    <div style='font-size: 14px; font-weight: bold; color: ${textColor};'>${mv}</div>
                    <div style='font-size: 9px; color: #888;'>mV</div>
                `;
                
                cell.title = `Cell ${idx + 1}: ${mv} mV${balancing && balancing[idx] ? ' (BALANCING)' : ''}${mv === max ? ' [MAX]' : ''}${mv === min ? ' [MIN]' : ''}`;
                
                cell.onmouseover = () => cell.style.transform = 'scale(1.05)';
                cell.onmouseout = () => cell.style.transform = 'scale(1)';
                
                grid.appendChild(cell);
            });
        }

        async function loadCellData() {
            try {
                const res = await fetch('/api/cell_data');
                const data = await res.json();

                const modeEl = document.getElementById('cellMode');
                const statusEl = document.getElementById('cellStatus');

                if (!data.success) {
                    modeEl.textContent = data.mode || 'live';
                    modeEl.style.color = '#ff6b35';
                    statusEl.textContent = data.message || 'Cell data not available';
                    renderCells([], []);
                    return;
                }

                modeEl.textContent = data.mode || 'simulated';
                modeEl.style.color = data.mode === 'simulated' ? '#FFD700' : '#4CAF50';
                statusEl.textContent = `Cells: ${data.cells.length} | Min: ${data.cell_min_voltage_mV}mV | Max: ${data.cell_max_voltage_mV}mV`;

                renderCells(data.cells, data.balancing, data.cell_min_voltage_mV, data.cell_max_voltage_mV);
            } catch (e) {
                document.getElementById('cellStatus').textContent = 'Failed to load cell data';
            }
        }

        loadCellData();
        setInterval(loadCellData, 5000);
    </script>
    )rawliteral";

    String page = generatePage("Cell Monitor", content, "/cellmonitor");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, page.c_str(), page.length());
    return ESP_OK;
}

esp_err_t register_cellmonitor_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/cellmonitor",
        .method    = HTTP_GET,
        .handler   = cellmonitor_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}