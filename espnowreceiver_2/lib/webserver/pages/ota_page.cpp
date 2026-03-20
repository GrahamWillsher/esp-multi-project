#include "ota_page.h"
#include "../common/page_generator.h"
#include "../common/nav_buttons.h"
#include "../utils/transmitter_manager.h"
#include <Arduino.h>

/**
 * @brief Handler for the OTA firmware upload page
 * 
 * This page allows users to upload firmware files to the transmitter via HTTP.
 * Features file selection, upload progress tracking, and automatic redirect on success.
 */
static esp_err_t ota_handler(httpd_req_t *req) {
    String content = R"rawliteral(
    <h1>ESP-NOW Receiver</h1>
    <h2>OTA Firmware Update</h2>
    )rawliteral";
    
    // Add navigation buttons from central registry
    content += "    " + generate_nav_buttons("/ota");
    
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

    String script = R"rawliteral(
        function formatEnvName(env) {
            if (!env) return '';
            const spaced = String(env).replace(/[-_]+/g, ' ').trim();
            return spaced.replace(/\b\w/g, c => c.toUpperCase());
        }

        function displayDeviceName(meta) {
            const env = formatEnvName(meta.env || '');
            const type = meta.device || '';
            if (env) return env;
            if (type) return type;
            return 'Unknown Device';
        }

        function parseVersion(v) {
            const m = /^(\d+)\.(\d+)\.(\d+)$/.exec(v || '');
            if (!m) return null;
            return { major: Number(m[1]), minor: Number(m[2]), patch: Number(m[3]) };
        }

        let receiverVersionForCompat = null;
        let transmitterVersionForCompat = null;

        function updateCompatibilityStatus() {
            const el = document.getElementById('compatibilityStatus');
            const rx = parseVersion(receiverVersionForCompat);
            const tx = parseVersion(transmitterVersionForCompat);

            if (!rx || !tx) {
                el.style.color = '#FFD700';
                el.innerText = '⚠️ Waiting for version info';
                return;
            }

            if (rx.major === tx.major) {
                el.style.color = '#4CAF50';
                el.innerText = '✅ Compatible';
            } else {
                el.style.color = '#ff6b35';
                el.innerText = '⚠️ Version mismatch';
            }
        }

        // Extract firmware metadata from binary file
        async function extractMetadataFromFile(file) {
            return new Promise((resolve, reject) => {
                const reader = new FileReader();
                reader.onload = function(e) {
                    const arrayBuffer = e.target.result;
                    const bytes = new Uint8Array(arrayBuffer);
                    
                    // Search for magic start marker (0x464D5441 = "FMTA" in little-endian)
                    const MAGIC_START = [0x41, 0x54, 0x4D, 0x46];  // Little-endian
                    const MAGIC_END = [0x46, 0x44, 0x4E, 0x45];    // Little-endian
                    
                    let metadataOffset = -1;
                    for (let i = 0; i < bytes.length - 128; i++) {
                        if (bytes[i] === MAGIC_START[0] && bytes[i+1] === MAGIC_START[1] &&
                            bytes[i+2] === MAGIC_START[2] && bytes[i+3] === MAGIC_START[3]) {
                            metadataOffset = i;
                            break;
                        }
                    }
                    
                    if (metadataOffset === -1) {
                        resolve({ valid: false, reason: 'No metadata found' });
                        return;
                    }
                    
                    // Verify end marker
                    if (bytes[metadataOffset + 124] !== MAGIC_END[0] || 
                        bytes[metadataOffset + 125] !== MAGIC_END[1] ||
                        bytes[metadataOffset + 126] !== MAGIC_END[2] || 
                        bytes[metadataOffset + 127] !== MAGIC_END[3]) {
                        resolve({ valid: false, reason: 'Invalid metadata structure' });
                        return;
                    }
                    
                    // Extract fields
                    const decoder = new TextDecoder('utf-8');
                    const env_name = decoder.decode(bytes.slice(metadataOffset + 4, metadataOffset + 36)).replace(/\0/g, '');
                    const device_type = decoder.decode(bytes.slice(metadataOffset + 36, metadataOffset + 52)).replace(/\0/g, '');
                    const version_major = bytes[metadataOffset + 52];
                    const version_minor = bytes[metadataOffset + 53];
                    const version_patch = bytes[metadataOffset + 54];
                    const build_date = decoder.decode(bytes.slice(metadataOffset + 56, metadataOffset + 104)).replace(/\0/g, '');
                    
                    resolve({
                        valid: true,
                        env: env_name,
                        device: device_type,
                        version: `${version_major}.${version_minor}.${version_patch}`,
                        build_date: build_date
                    });
                };
                reader.onerror = reject;
                reader.readAsArrayBuffer(file);
            });
        }
        
        function setupDeviceUpload(device) {
            const fileInput = document.getElementById('firmwareFile' + device);
            const uploadBtn = document.getElementById('uploadBtn' + device);
            const statusDiv = document.getElementById('status' + device);
            const progressBarDiv = document.getElementById('progressBar' + device);
            const progressFill = document.getElementById('progressFill' + device);
            const progressBarText = document.getElementById('progressText' + device);
            
            let selectedFile = null;
            
            // Handle file selection
            fileInput.addEventListener('change', async function(e) {
                if (e.target.files.length > 0) {
                    selectedFile = e.target.files[0];
                    const sizeMB = (selectedFile.size / 1024 / 1024).toFixed(2);
                    
                    // Extract metadata from file
                    try {
                        const metadata = await extractMetadataFromFile(selectedFile);
                        if (metadata.valid) {
                            statusDiv.innerHTML = '📄 ' + selectedFile.name + ' (' + sizeMB + ' MB) → ' +
                                                  '<span style="color: #4CAF50;">● v' + 
                                                  metadata.version + '</span> ' +
                                                  '<span style="color: #888; font-size: 12px;">(Built: ' + 
                                                  metadata.build_date + ')</span>';
                        } else {
                            statusDiv.innerHTML = '📄 ' + selectedFile.name + ' (' + sizeMB + ' MB) → ' +
                                                  '<span style="color: #FFD700;">* No metadata (legacy firmware)</span>';
                        }
                    } catch (err) {
                        statusDiv.innerHTML = '📄 ' + selectedFile.name + ' (' + sizeMB + ' MB)';
                    }
                    
                    uploadBtn.disabled = false;
                    uploadBtn.style.backgroundColor = '#ff6b35';
                    uploadBtn.innerText = 'Upload Firmware';
                } else {
                    selectedFile = null;
                    statusDiv.innerHTML = '📁 Select firmware file (.bin)';
                    uploadBtn.disabled = false;
                    uploadBtn.style.backgroundColor = '#666';
                    uploadBtn.innerText = 'Select File First';
                }
            });
            
            // Handle upload button
            uploadBtn.addEventListener('click', function() {
                if (!selectedFile) {
                    fileInput.click();
                    return;
                }
                
                console.log('Starting ' + device + ' OTA upload...');
                uploadBtn.style.display = 'none';
                progressBarDiv.style.display = 'block';
                progressFill.style.width = '0%';
                progressBarText.innerText = '0%';
                
                // Create FormData and upload
                const formData = new FormData();
                formData.append('firmware', selectedFile);

                const xhr = new XMLHttpRequest();
                
                // Upload progress
                xhr.upload.addEventListener('progress', function(e) {
                    if (e.lengthComputable) {
                        const percent = Math.round((e.loaded / e.total) * 100);
                        progressFill.style.width = percent + '%';
                        progressBarText.innerText = percent + '%';
                    }
                });
                
                // Upload complete
                xhr.addEventListener('load', function() {
                    if (xhr.status === 200) {
                        try {
                            const response = JSON.parse(xhr.responseText);
                            if (response.success) {
                                if (device === 'Receiver') {
                                    statusDiv.innerHTML = '✅ Firmware uploaded! Device is rebooting...<br><br>Please wait 30 seconds and reload the page.';
                                    progressBarDiv.style.display = 'none';
                                    uploadBtn.style.display = '';
                                    uploadBtn.innerText = 'Upload complete\nRebooting...';
                                    uploadBtn.style.backgroundColor = '#4CAF50';
                                    // Don't redirect - receiver is rebooting
                                } else {
                                    statusDiv.innerHTML = '✅ Firmware uploaded to transmitter.<br><br>Verifying OTA readiness...';
                                    progressBarDiv.style.display = 'none';
                                    uploadBtn.style.display = '';
                                    uploadBtn.innerText = 'Upload complete\nVerifying OTA...';
                                    uploadBtn.style.backgroundColor = '#4CAF50';

                                    let statusPollAttempts = 0;
                                    const maxStatusPollAttempts = 40; // 40s

                                    const statusPollInterval = setInterval(function() {
                                        statusPollAttempts++;

                                        fetch('/api/transmitter_ota_status')
                                            .then(resp => resp.json())
                                            .then(status => {
                                                if (status.success && status.ready_for_reboot) {
                                                    clearInterval(statusPollInterval);

                                                    TransmitterReboot.run({
                                                        countdownSeconds: TransmitterReboot.COUNTDOWN_SECONDS,
                                                        updateCountdown: (seconds) => {
                                                            statusDiv.innerHTML = '✅ OTA verified on transmitter.';
                                                            uploadBtn.disabled = true;
                                                            uploadBtn.style.cursor = 'not-allowed';
                                                            uploadBtn.style.backgroundColor = '#ff9800';
                                                            uploadBtn.innerText = 'Reboot in ' + seconds + 's...';
                                                        },
                                                        onCommandStart: () => {
                                                            statusDiv.innerHTML = '✅ Sending reboot command to transmitter...';
                                                            uploadBtn.disabled = true;
                                                            uploadBtn.style.cursor = 'not-allowed';
                                                            uploadBtn.style.backgroundColor = '#ff9800';
                                                            uploadBtn.innerText = 'Sending reboot command...';
                                                        },
                                                        onSuccess: () => {
                                                            statusDiv.innerHTML = '';
                                                            uploadBtn.disabled = true;
                                                            uploadBtn.style.cursor = 'not-allowed';
                                                            uploadBtn.style.backgroundColor = '#28a745';
                                                            uploadBtn.innerText = '✓ Reboot command sent';
                                                        },
                                                        onFailure: (message) => {
                                                            statusDiv.innerHTML = '❌ OTA uploaded, but reboot command failed: ' + (message || 'Unknown error');
                                                            uploadBtn.disabled = false;
                                                            uploadBtn.style.cursor = 'pointer';
                                                            uploadBtn.style.backgroundColor = '#ff6b35';
                                                            uploadBtn.innerText = 'Retry Upload';
                                                        },
                                                        onError: (err) => {
                                                            statusDiv.innerHTML = '❌ OTA uploaded, but reboot request error: ' + err.message;
                                                            uploadBtn.disabled = false;
                                                            uploadBtn.style.cursor = 'pointer';
                                                            uploadBtn.style.backgroundColor = '#ff6b35';
                                                            uploadBtn.innerText = 'Retry Upload';
                                                        }
                                                    });
                                                } else if (status.success && status.in_progress) {
                                                    statusDiv.innerHTML = '✅ Firmware uploaded to transmitter.<br><br>Applying OTA... please wait';
                                                } else if (status.success && !status.in_progress && !status.ready_for_reboot && status.last_success === false) {
                                                    clearInterval(statusPollInterval);
                                                    statusDiv.innerHTML = '❌ Transmitter OTA failed: ' + (status.last_error || 'Unknown error');
                                                    uploadBtn.disabled = false;
                                                    uploadBtn.style.backgroundColor = '#ff6b35';
                                                    uploadBtn.innerText = 'Retry Upload';
                                                } else if (!status.success) {
                                                    clearInterval(statusPollInterval);
                                                    statusDiv.innerHTML = '❌ Unable to verify transmitter OTA: ' + (status.message || status.detail || 'Unknown status error');
                                                    uploadBtn.disabled = false;
                                                    uploadBtn.style.backgroundColor = '#ff6b35';
                                                    uploadBtn.innerText = 'Retry Upload';
                                                }
                                            })
                                            .catch(() => {
                                                // Keep polling until timeout
                                            });

                                        if (statusPollAttempts >= maxStatusPollAttempts) {
                                            clearInterval(statusPollInterval);
                                            statusDiv.innerHTML = '❌ Timed out waiting for transmitter OTA readiness';
                                            uploadBtn.disabled = false;
                                            uploadBtn.style.backgroundColor = '#ff6b35';
                                            uploadBtn.innerText = 'Retry Upload';
                                        }
                                    }, 1000);
                                }
                            } else {
                                statusDiv.innerHTML = '❌ Failed: ' + response.message;
                                progressBarDiv.style.display = 'none';
                                uploadBtn.style.display = '';
                                uploadBtn.disabled = false;
                                uploadBtn.style.backgroundColor = '#ff6b35';
                                uploadBtn.innerText = 'Retry Upload';
                            }
                        } catch (e) {
                            statusDiv.innerHTML = '❌ Error parsing response';
                            progressBarDiv.style.display = 'none';
                            uploadBtn.style.display = '';
                            uploadBtn.disabled = false;
                            uploadBtn.style.backgroundColor = '#ff6b35';
                            uploadBtn.innerText = 'Retry Upload';
                        }
                    } else {
                        statusDiv.innerHTML = '❌ Upload failed: HTTP ' + xhr.status;
                        progressBarDiv.style.display = 'none';
                        uploadBtn.style.display = '';
                        uploadBtn.disabled = false;
                        uploadBtn.style.backgroundColor = '#ff6b35';
                        uploadBtn.innerText = 'Retry Upload';
                    }
                });
                
                // Upload error
                xhr.addEventListener('error', function() {
                    statusDiv.innerHTML = '❌ Network error during upload';
                    progressBarDiv.style.display = 'none';
                    uploadBtn.style.display = '';
                    uploadBtn.disabled = false;
                    uploadBtn.style.backgroundColor = '#ff6b35';
                    uploadBtn.innerText = 'Retry Upload';
                });

                // Choose endpoint based on device
                const endpoint = (device === 'Receiver') ? '/api/ota_upload_receiver' : '/api/ota_upload';
                xhr.open('POST', endpoint);
                if (device === 'Receiver') {
                    // Receiver self-OTA path expects multipart form upload
                    xhr.send(formData);
                } else {
                    // Transmitter OTA forwarding path uses raw binary upload
                    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
                    xhr.send(selectedFile);
                }
            });
        }
        
        window.onload = function() {
            console.log('OTA page loaded');

            function fallbackReceiverFromVersionApi() {
                return fetch('/api/version')
                    .then(response => response.json())
                    .then(data => {
                        const versionEl = document.getElementById('receiverVersion');
                        const buildEl = document.getElementById('receiverBuild');
                        const v = data.version || 'Unknown';
                        const d = data.build_date || 'Build info unavailable';
                        versionEl.innerHTML = 'Receiver v' + v + ' <span style="color: #FFD700;">*</span>';
                        buildEl.innerText = d;
                        receiverVersionForCompat = v;
                        updateCompatibilityStatus();
                    });
            }

            function fallbackTransmitterFromVersionApi() {
                return fetch('/api/version')
                    .then(response => response.json())
                    .then(data => {
                        const versionEl = document.getElementById('transmitterVersion');
                        const buildEl = document.getElementById('transmitterBuild');
                        const v = data.transmitter_version || 'Unknown';
                        const d = data.transmitter_build_date || 'Build info unavailable';
                        versionEl.innerHTML = 'Transmitter v' + v + ' <span style="color: #FFD700;">*</span>';
                        buildEl.innerText = d;
                        transmitterVersionForCompat = (v && v !== 'Unknown') ? v : null;
                        updateCompatibilityStatus();
                    });
            }
            
            // Load current receiver firmware info
            fetch('/api/firmware_info')
                .then(response => response.json())
                .then(data => {
                    const versionEl = document.getElementById('receiverVersion');
                    const buildEl = document.getElementById('receiverBuild');
                    
                    if (data.valid) {
                        const name = displayDeviceName(data);
                        versionEl.innerHTML = name + ' v' + data.version + ' <span style="color: #4CAF50;">●</span>';
                        buildEl.innerText = data.build_date;
                        receiverVersionForCompat = data.version;
                    } else {
                        versionEl.innerHTML = 'Metadata unavailable <span style="color: #FFD700;">*</span>';
                        buildEl.innerText = data.message || 'No embedded metadata';
                        receiverVersionForCompat = null;
                    }
                    updateCompatibilityStatus();
                })
                .catch(err => {
                    console.error('Failed to fetch firmware info:', err);
                    fallbackReceiverFromVersionApi().catch(() => {
                        document.getElementById('receiverVersion').innerText = 'Unavailable';
                        document.getElementById('receiverBuild').innerText = 'Failed to load';
                        receiverVersionForCompat = null;
                        updateCompatibilityStatus();
                    });
                });
            
            // Setup upload handlers for both devices
            setupDeviceUpload('Receiver');
            setupDeviceUpload('Transmitter');
            
            // Fetch transmitter metadata from ESP-NOW data (simpler approach)
            function fetchTransmitterMetadata() {
                fetch('/api/transmitter_metadata')
                    .then(response => response.json())
                    .then(data => {
                        const versionEl = document.getElementById('transmitterVersion');
                        const buildEl = document.getElementById('transmitterBuild');
                        
                        if (data.status === 'received' && data.version) {
                            // Metadata received from transmitter
                            let indicator;
                            if (data.valid) {
                                // Green circle: Valid metadata from .rodata
                                indicator = '<span style="color: #4CAF50;">●</span>';
                            } else {
                                // Yellow star: Fallback to build flags (metadata not valid)
                                indicator = '<span style="color: #FFD700;">*</span>';
                            }
                            const name = displayDeviceName(data);
                            versionEl.innerHTML = name + ' v' + data.version + ' ' + indicator;
                            const buildDate = data.build_date || data.buildDate || data.build || '';
                            if (buildDate) {
                                buildEl.innerText = buildDate;
                            } else {
                                fallbackTransmitterFromVersionApi().catch(() => {
                                    buildEl.innerText = 'Build info unavailable';
                                });
                            }
                            transmitterVersionForCompat = data.version;
                        } else if (data.status === 'received' && (!data.version || !data.build_date)) {
                            fallbackTransmitterFromVersionApi().catch(() => {
                                versionEl.innerHTML = 'Transmitter metadata incomplete <span style="color: #FFD700;">*</span>';
                                buildEl.innerText = 'Build info unavailable';
                                transmitterVersionForCompat = null;
                                updateCompatibilityStatus();
                            });
                        } else {
                            fallbackTransmitterFromVersionApi().catch(() => {
                                // Gray circle: Waiting for metadata from transmitter
                                versionEl.innerHTML = 'Waiting... <span style="color: #888;">○</span>';
                                buildEl.innerText = 'No metadata received yet';
                                transmitterVersionForCompat = null;
                                updateCompatibilityStatus();
                            });
                            return;
                        }
                        updateCompatibilityStatus();
                    })
                    .catch(err => {
                        console.error('Failed to fetch transmitter metadata:', err);
                        fallbackTransmitterFromVersionApi().catch(() => {
                            document.getElementById('transmitterVersion').innerHTML = 'Unavailable';
                            document.getElementById('transmitterBuild').innerText = 'Failed to load';
                            transmitterVersionForCompat = null;
                            updateCompatibilityStatus();
                        });
                    });
            }
            
            // Initial fetch
            fetchTransmitterMetadata();
            
            // Periodically refresh transmitter metadata
            setInterval(fetchTransmitterMetadata, 5000);
        };
    )rawliteral";

    String html = renderPage("ESP-NOW Receiver - OTA Update", content, PageRenderOptions("", script));
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html.c_str(), html.length());
    return ESP_OK;
}

/**
 * @brief Register the OTA page handler with the HTTP server
 */
esp_err_t register_ota_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/ota",
        .method    = HTTP_GET,
        .handler   = ota_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
