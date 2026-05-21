#include "cellmonitor_page_script.h"

const char* get_cellmonitor_page_script() {
    return R"rawliteral(
        let selectedCellIdx = -1;
        let selectedBarIdx = -1;
        let pollTimer = null;
        let cellKeepaliveTimer = null;
        const CELL_POLL_BASE_MS = 5000;
        const CELL_POLL_MAX_MS = 30000;
        const CELL_STREAM_KEEPALIVE_MS = 45000;
        let cellPollDelayMs = CELL_POLL_BASE_MS;

        function subscribeCellDataStream() {
            fetch('/api/cell_data/subscribe', { method: 'POST' }).catch(() => {});
        }

        function unsubscribeCellDataStream() {
            if (navigator.sendBeacon) {
                const blob = new Blob(['{}'], { type: 'application/json' });
                navigator.sendBeacon('/api/cell_data/unsubscribe', blob);
            } else {
                fetch('/api/cell_data/unsubscribe', { method: 'POST', keepalive: true }).catch(() => {});
            }
        }

        function startCellDataKeepalive() {
            if (cellKeepaliveTimer) {
                clearInterval(cellKeepaliveTimer);
            }
            cellKeepaliveTimer = setInterval(() => {
                fetch('/api/cell_data/keepalive', { method: 'POST' }).catch(() => {});
            }, CELL_STREAM_KEEPALIVE_MS);
        }

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
                
                // Highlight min/max cells with distinct colors
                if (mv === max || mv === min) {
                    cell.style.borderColor = '#FFD700';
                    cell.style.borderWidth = '2px';
                    cell.style.boxShadow = '0 0 8px rgba(255, 215, 0, 0.5)';
                }
                
                // Red text for low voltage
                const textColor = mv < 3000 ? '#FF6B6B' : (balancing && balancing[idx] ? '#00FFFF' : '#4CAF50');
                
                cell.innerHTML = `
                    <div style='font-size: 10px; color: #888; margin-bottom: 2px;'>Cell ${idx + 1}</div>
                    <div style='font-size: 14px; font-weight: bold; color: ${textColor};'>${mv}</div>
                    <div style='font-size: 9px; color: #888;'>mV</div>
                `;
                
                cell.title = `Cell ${idx + 1}: ${mv} mV${balancing && balancing[idx] ? ' (BALANCING)' : ''}${mv === max ? ' [MAX]' : ''}${mv === min ? ' [MIN]' : ''}`;
                
                // Enhanced hover - 15% enlargement + shadow
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
                let color = '#4CAF50';
                if (mv === max || mv === min) {
                    color = '#FFD700';
                } else if (mv < min + range * 0.33) {
                    color = '#FF6B6B';
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
                
                // Bi-directional highlighting (bar <-> cell)
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

        function jitterDelay(ms) {
            const jitter = 0.10;
            const delta = Math.floor(ms * jitter);
            const min = Math.max(1000, ms - delta);
            const max = ms + delta;
            return Math.floor(Math.random() * (max - min + 1)) + min;
        }

        function retryAfterToMs(response) {
            const retryAfter = response.headers.get('Retry-After');
            const retrySeconds = Number(retryAfter);
            if (!Number.isFinite(retrySeconds) || retrySeconds <= 0) {
                return null;
            }
            return Math.max(CELL_POLL_BASE_MS, Math.floor(retrySeconds * 1000));
        }

        function scheduleNextPoll(delayMs) {
            if (pollTimer) {
                clearTimeout(pollTimer);
            }
            pollTimer = setTimeout(pollCellData, jitterDelay(delayMs));
        }

        async function pollCellData() {
            const modeEl = document.getElementById('cellMode');
            const statusEl = document.getElementById('cellStatus');

            try {
                const response = await fetch('/api/cell_data', { cache: 'no-store' });

                if (!response.ok) {
                    const retryAfterMs = retryAfterToMs(response);
                    if (retryAfterMs !== null) {
                        cellPollDelayMs = Math.max(CELL_POLL_BASE_MS, retryAfterMs);
                    } else {
                        cellPollDelayMs = Math.min(cellPollDelayMs * 2, CELL_POLL_MAX_MS);
                    }

                    statusEl.textContent = `Cell data unavailable (${response.status}) - retrying...`;
                    statusEl.style.color = '#FFD700';
                    scheduleNextPoll(cellPollDelayMs);
                    return;
                }

                const data = await response.json();
                if (data.success) {
                    modeEl.textContent = data.mode || 'live';
                    modeEl.style.color = '#4CAF50';
                    statusEl.innerHTML = `Cells: ${data.cells.length} | Min Voltage: <span style="color:#4CAF50;">${data.cell_min_voltage_mV} mV</span> | Max Voltage: <span style="color:#FF6B6B;">${data.cell_max_voltage_mV} mV</span>`;
                    statusEl.style.color = '#ddd';
                    renderCells(data.cells, data.balancing, data.cell_min_voltage_mV, data.cell_max_voltage_mV);
                } else {
                    modeEl.textContent = data.mode || 'unavailable';
                    modeEl.style.color = '#FFD700';
                    statusEl.textContent = data.message || 'Waiting for data from transmitter...';
                    statusEl.style.color = '#FFD700';
                }

                // Successful HTTP response resets polling delay to the design cadence.
                cellPollDelayMs = CELL_POLL_BASE_MS;
                scheduleNextPoll(cellPollDelayMs);
            } catch (error) {
                console.error('Cell polling error:', error);
                cellPollDelayMs = Math.min(cellPollDelayMs * 2, CELL_POLL_MAX_MS);
                statusEl.textContent = 'Connection lost - retrying...';
                statusEl.style.color = '#FFD700';
                scheduleNextPoll(cellPollDelayMs);
            }
        }

        document.addEventListener('DOMContentLoaded', function() {
            subscribeCellDataStream();
            startCellDataKeepalive();
            pollCellData();
        });

        window.addEventListener('beforeunload', function() {
            if (pollTimer) {
                clearTimeout(pollTimer);
            }
            if (cellKeepaliveTimer) {
                clearInterval(cellKeepaliveTimer);
            }
            unsubscribeCellDataStream();
        });
    )rawliteral";
}
