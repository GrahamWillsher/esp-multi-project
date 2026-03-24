#include "transmitter_hub_page_script.h"

String get_transmitter_hub_page_script() {
    return R"rawliteral(
        let txHubIntervalsStarted = false;

        // Update test data mode display
        async function updateTransmitterFirmwareInfo() {
            const versionEl = document.getElementById('txFirmwareVersion');
            const buildEl = document.getElementById('txFirmwareBuildDate');

            if (!versionEl || !buildEl) {
                return;
            }

            function formatEnv(env) {
                if (!env) return '';
                const spaced = String(env).replace(/[-_]+/g, ' ').trim();
                return spaced.replace(/\b\w/g, c => c.toUpperCase());
            }

            function displayName(meta) {
                const env = formatEnv(meta.env || '');
                const dev = meta.device || '';
                if (env) return env;
                if (dev) return dev;
                return 'Transmitter';
            }

            try {
                const res = await fetch('/api/transmitter_metadata');
                const data = await res.json();

                if (data.status === 'received' && data.version) {
                    const name = displayName(data);
                    versionEl.textContent = name + ' v' + data.version;
                    const build = data.build_date || data.buildDate || data.build || '';
                    if (build) {
                        buildEl.textContent = build;
                        return;
                    }
                }
            } catch (e) {
                console.error('Error fetching transmitter metadata:', e);
            }

            try {
                const res2 = await fetch('/api/version');
                const versionData = await res2.json();
                const txVersion = versionData.transmitter_version || 'Unknown';
                const txBuild = versionData.transmitter_build_date || 'Build info unavailable';
                versionEl.textContent = 'Transmitter v' + txVersion;
                buildEl.textContent = txBuild;
            } catch (e2) {
                console.error('Error fetching transmitter version fallback:', e2);
                if (buildEl) {
                    buildEl.textContent = 'Build info unavailable';
                }
            }
        }

        async function updateTestDataMode() {
            try {
                const res = await fetch('/api/get_test_data_mode');
                const data = await res.json();
                const modeEl = document.getElementById('txTestDataMode');
                if (!modeEl) return;
                let modeText = 'Unknown';
                let modeColor = '#888';

                if (data.mode === 0 || data.mode === 'OFF') {
                    modeText = 'OFF (Real CAN Data Only)';
                    modeColor = '#f44336';
                } else if (data.mode === 1 || data.mode === 'SOC_POWER_ONLY') {
                    modeText = 'SOC_POWER_ONLY (Test SOC & Power)';
                    modeColor = '#FF9800';
                } else if (data.mode === 2 || data.mode === 'FULL_BATTERY_DATA') {
                    modeText = 'FULL_BATTERY_DATA (All Test Data)';
                    modeColor = '#4CAF50';
                }

                modeEl.textContent = modeText;
                modeEl.style.color = modeColor;
            } catch (e) {
                console.error('Error fetching test data mode:', e);
                const modeEl = document.getElementById('txTestDataMode');
                if (modeEl) {
                    modeEl.textContent = 'Error fetching status';
                }
            }
        }

        // Set test data mode
        async function setTestDataMode(mode) {
            try {
                const statusEl = document.getElementById('modeStatus');
                if (!statusEl) return;
                statusEl.textContent = 'Sending...';
                statusEl.style.color = '#888';

                const res = await fetch('/api/set_test_data_mode', {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json'
                    },
                    body: JSON.stringify({ mode: mode })
                });

                if (res.ok) {
                    statusEl.textContent = '✓ Mode changed successfully';
                    statusEl.style.color = '#4CAF50';
                    // Refresh the display after a short delay
                    setTimeout(updateTestDataMode, 500);
                } else {
                    statusEl.textContent = '✗ Failed to set mode. Transmitter may be disconnected.';
                    statusEl.style.color = '#f44336';
                }
            } catch (e) {
                console.error('Error setting test data mode:', e);
                const statusEl = document.getElementById('modeStatus');
                if (statusEl) {
                    statusEl.textContent = '✗ Error: ' + e.message;
                    statusEl.style.color = '#f44336';
                }
            }
        }

        function startTxHubAutoRefresh() {
            if (txHubIntervalsStarted) return;
            txHubIntervalsStarted = true;
            updateTransmitterFirmwareInfo();
            updateTestDataMode();
            setInterval(updateTransmitterFirmwareInfo, 5000);
            setInterval(updateTestDataMode, 3000);  // Update every 3s
        }

        window.addEventListener('DOMContentLoaded', startTxHubAutoRefresh);
    )rawliteral";
}
