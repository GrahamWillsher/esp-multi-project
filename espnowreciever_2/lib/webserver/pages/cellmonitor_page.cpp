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

    <div class='info-box' style='margin-top: 30px;'>
        <h2 style='margin-top: 0; color: #00FFFF;'>Voltage Distribution</h2>
        <div style='margin-bottom: 10px; color: #888; font-size: 12px;'>
            <span>Min: <strong id='barMin' style='color: #FF6B6B;'>-- mV</strong></span>
            <span style='margin-left: 20px;'>Max: <strong id='barMax' style='color: #4CAF50;'>-- mV</strong></span>
        </div>
        <div id='voltageBar' style='display: flex; flex-direction: row; gap: 2px; height: 120px; width: 100%; background: #111; padding: 4px; align-items: flex-end; box-sizing: border-box;'></div>
    </div>

    <script>
        let selectedCellIdx = -1;
        let selectedBarIdx = -1;
        let eventSource = null;

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

            // Update voltage bar limits
            document.getElementById('barMin').textContent = min + ' mV';
            document.getElementById('barMax').textContent = max + ' mV';

            // Render cells in grid
            cells.forEach((mv, idx) => {
                const cell = document.createElement('div');
                cell.style.cssText = 'padding: 6px; border-radius: 6px; text-align: center; cursor: pointer; transition: transform 0.2s, box-shadow 0.2s;';
                
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
                
                // Phase 4: Enhanced hover - 15% enlargement + shadow
                cell.onmouseover = () => {
                    cell.style.transform = 'scale(1.15)';
                    cell.style.boxShadow = '0 0 12px rgba(76, 175, 80, 0.6)';
                    selectedCellIdx = idx;
                    updateBarHighlight(idx);
                };
                cell.onmouseout = () => {
                    cell.style.transform = 'scale(1)';
                    cell.style.boxShadow = 'none';
                    selectedCellIdx = -1;
                    updateBarHighlight(-1);
                };
                
                grid.appendChild(cell);
            });

            // Render voltage distribution bar
            renderVoltageBar(cells);
        }

        // Phase 4: Render voltage distribution bar graph
        function renderVoltageBar(cells) {
            const bar = document.getElementById('voltageBar');
            bar.innerHTML = '';

            if (!cells || cells.length === 0) {
                return;
            }

            const min = Math.min(...cells);
            const max = Math.max(...cells);
            const range = max - min || 1;

            cells.forEach((mv, idx) => {
                const barSegment = document.createElement('div');
                
                // Calculate height proportional to voltage
                const normalized = (mv - min) / range;
                const height = Math.max(10, normalized * 100);
                
                // Color based on voltage level
                let color = '#4CAF50';  // Green - normal
                if (mv < min + range * 0.33) {
                    color = '#FF6B6B';  // Red - low
                } else if (mv > max - range * 0.1) {
                    color = '#FFD700';  // Yellow - high
                }
                
                barSegment.style.cssText = `
                    background: linear-gradient(to top, ${color}, ${color});
                    height: ${height}%;
                    flex: 1 1 auto;
                    border-radius: 2px;
                    cursor: pointer;
                    transition: all 0.2s;
                    border: 1px solid rgba(255, 255, 255, 0.2);
                `;
                
                barSegment.title = `Cell ${idx + 1}: ${mv} mV`;
                
                // Phase 4: Bi-directional highlighting (bar ↔ cell)
                barSegment.onmouseover = () => {
                    barSegment.style.opacity = '1';
                    barSegment.style.boxShadow = '0 0 8px ' + color;
                    selectedBarIdx = idx;
                    updateBarHighlight(idx);
                };
                barSegment.onmouseout = () => {
                    barSegment.style.opacity = '0.8';
                    barSegment.style.boxShadow = 'none';
                    selectedBarIdx = -1;
                    updateBarHighlight(-1);
                };
                
                barSegment.style.opacity = '0.8';
                bar.appendChild(barSegment);
            });
        }

        // Phase 4: Update bar highlight when cell is hovered
        function updateBarHighlight(cellIdx) {
            const bar = document.getElementById('voltageBar');
            const segments = bar.querySelectorAll('div');
            segments.forEach((seg, idx) => {
                if (idx === cellIdx) {
                    seg.style.opacity = '1';
                } else if (cellIdx === -1) {
                    seg.style.opacity = '0.8';
                }
            });
        }

        // Phase 4: Highlight cell when bar segment is hovered
        function highlightCell(barIdx) {
            const grid = document.getElementById('cellGrid');
            const cells = grid.querySelectorAll('div');
            cells.forEach((cell, idx) => {
                if (idx === barIdx) {
                    cell.style.opacity = '1';
                } else if (barIdx === -1) {
                    cell.style.opacity = '1';
                } else {
                    cell.style.opacity = '0.6';
                }
            });
        }

        function connectSSE() {
            // Close previous connection if exists
            if (eventSource) {
                eventSource.close();
            }
            
            eventSource = new EventSource('/api/cell_stream');
            
            eventSource.onmessage = function(event) {
                try {
                    const data = JSON.parse(event.data);
                    
                    if (data.success) {
                        const modeEl = document.getElementById('cellMode');
                        const statusEl = document.getElementById('cellStatus');
                        
                        modeEl.textContent = data.mode || 'live';
                        modeEl.style.color = '#4CAF50';
                        statusEl.textContent = `Cells: ${data.cells.length} | Min: ${data.cell_min_voltage_mV}mV | Max: ${data.cell_max_voltage_mV}mV`;
                        
                        renderCells(data.cells, data.balancing, data.cell_min_voltage_mV, data.cell_max_voltage_mV);
                    } else {
                        // No MQTT data available yet
                        const modeEl = document.getElementById('cellMode');
                        const statusEl = document.getElementById('cellStatus');
                        
                        modeEl.textContent = data.mode || 'unavailable';
                        modeEl.style.color = '#FFD700';
                        statusEl.textContent = data.message || 'Waiting for data from transmitter...';
                        statusEl.style.color = '#FFD700';
                    }
                } catch (e) {
                    console.error('Failed to parse SSE data:', e);
                }
            };
            
            eventSource.onerror = function(event) {
                console.error('SSE connection error:', event);
                document.getElementById('cellStatus').textContent = 'Connection lost - reconnecting...';
                // Reconnect after 3 seconds
                setTimeout(connectSSE, 3000);
            };
        }
        
        // Connect to SSE stream on page load
        connectSSE();
        
        // Cleanup on page unload
        window.addEventListener('beforeunload', function() {
            if (eventSource) {
                eventSource.close();
            }
        });
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