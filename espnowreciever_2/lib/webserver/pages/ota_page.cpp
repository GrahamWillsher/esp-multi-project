#include "ota_page.h"
#include "../common/page_generator.h"
#include "../common/nav_buttons.h"
#include "../utils/transmitter_manager.h"
#include <Arduino.h>
#include <firmware_version.h>

/**
 * @brief Handler for the OTA firmware upload page
 * 
 * This page allows users to upload firmware files to the transmitter via HTTP.
 * Features file selection, upload progress tracking, and automatic redirect on success.
 */
static esp_err_t ota_handler(httpd_req_t *req) {
    // Get version information
    String receiver_version = formatVersion(FW_VERSION_NUMBER);
    
    String transmitter_version = "Unknown";
    String compatibility_status = "⚠️ Waiting for version info";
    String compatibility_color = "#FFD700";
    
    // V2: Only use metadata (legacy hasVersionInfo() removed)
    if (TransmitterManager::hasMetadata()) {
        uint8_t major, minor, patch;
        TransmitterManager::getMetadataVersion(major, minor, patch);
        uint32_t tx_version = major * 10000 + minor * 100 + patch;
        transmitter_version = formatVersion(tx_version);
        
        if (isVersionCompatible(tx_version)) {
            compatibility_status = "✅ Compatible";
            compatibility_color = "#4CAF50";
        } else {
            compatibility_status = "⚠️ Version mismatch";
            compatibility_color = "#ff6b35";
        }
    }
    
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
                <td id='transmitterVersion' style='padding: 10px; color: #4CAF50;'>)rawliteral";
    content += transmitter_version;
    content += R"rawliteral(</td>
                <td id='transmitterBuild' style='padding: 10px; color: #888; font-size: 12px;'></td>
            </tr>
        </table>
        <div id='compatibilityStatus' style='margin-top: 15px; padding: 10px; background-color: rgba(0,0,0,0.3); border-radius: 5px; color: )rawliteral";
    content += compatibility_color;
    content += R"rawliteral(;'>
            )rawliteral";
    content += compatibility_status;
    content += R"rawliteral(
        </div>
    </div>
    
    <!-- Receiver OTA Section -->
    <div class='info-box' style='text-align: center; margin-bottom: 20px;'>
        <h3>📲 Update Receiver (This Device)</h3>
        <div id='statusReceiver' style='margin: 20px 0; font-size: 18px;'>
            📁 Select firmware file (.bin)
        </div>
        
        <div style='margin: 20px auto; max-width: 500px;'>
            <input type='file' id='firmwareFileReceiver' accept='.bin' 
                   style='display: block; margin: 20px auto; padding: 10px; font-size: 16px; cursor: pointer;'>
         </div>
        
        <button id='uploadBtnReceiver' class='button' disabled 
                style='background-color: #666; font-size: 18px; padding: 15px 30px;'>
            Select File First
        </button>
        
        <div style='margin-top: 15px; color: #FFD700; font-size: 14px;'>
            ⚠️ Device will reboot automatically after update
        </div>
        
        <div id='progressReceiver' style='margin-top: 20px; display: none;'>
            <div style='background-color: #333; border-radius: 5px; overflow: hidden;'>
                <div id='progressBarReceiver' style='background-color: #4CAF50; height: 30px; width: 0%; transition: width 0.3s;'></div>
            </div>
            <div id='progressTextReceiver' style='margin-top: 10px; color: #FFD700;'>0%</div>
        </div>
    </div>
    
    <!-- Transmitter OTA Section -->
    <div class='info-box' style='text-align: center;'>
        <h3>📡 Update Transmitter</h3>
        <div id='statusTransmitter' style='margin: 20px 0; font-size: 18px;'>
            📁 Select firmware file (.bin)
        </div>
        
        <div style='margin: 20px auto; max-width: 500px;'>
            <input type='file' id='firmwareFileTransmitter' accept='.bin' 
                   style='display: block; margin: 20px auto; padding: 10px; font-size: 16px; cursor: pointer;'>
         </div>
        
        <button id='uploadBtnTransmitter' class='button' disabled 
                style='background-color: #666; font-size: 18px; padding: 15px 30px;'>
            Select File First
        </button>
        
        <div style='margin-top: 15px; color: #FFD700; font-size: 14px;'>
            The transmitter will receive and install the firmware via HTTP
        </div>
        
        <div id='progressTransmitter' style='margin-top: 20px; display: none;'>
            <div style='background-color: #333; border-radius: 5px; overflow: hidden;'>
                <div id='progressBarTransmitter' style='background-color: #4CAF50; height: 30px; width: 0%; transition: width 0.3s;'></div>
            </div>
            <div id='progressTextTransmitter' style='margin-top: 10px; color: #FFD700;'>0%</div>
        </div>
    </div>
)rawliteral";

    String script = R"rawliteral(
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
            const progressDiv = document.getElementById('progress' + device);
            const progressBar = document.getElementById('progressBar' + device);
            const progressText = document.getElementById('progressText' + device);
            
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
                                                  '<span style="color: #4CAF50;">● ' + metadata.device + ' v' + 
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
                    uploadBtn.innerText = 'Upload and Update ' + device;
                } else {
                    selectedFile = null;
                    statusDiv.innerHTML = '📁 Select firmware file (.bin)';
                    uploadBtn.disabled = true;
                    uploadBtn.style.backgroundColor = '#666';
                    uploadBtn.innerText = 'Select File First';
                }
            });
            
            // Handle upload button
            uploadBtn.addEventListener('click', function() {
                if (!selectedFile) {
                    alert('Please select a firmware file first');
                    return;
                }
                
                console.log('Starting ' + device + ' OTA upload...');
                uploadBtn.disabled = true;
                uploadBtn.style.backgroundColor = '#666';
                uploadBtn.innerText = 'Uploading...';
                
                // Show progress bar
                progressDiv.style.display = 'block';
                
                // Create FormData and upload
                const formData = new FormData();
                formData.append('firmware', selectedFile);
                
                const xhr = new XMLHttpRequest();
                
                // Upload progress
                xhr.upload.addEventListener('progress', function(e) {
                    if (e.lengthComputable) {
                        const percent = Math.round((e.loaded / e.total) * 100);
                        progressBar.style.width = percent + '%';
                        progressText.innerText = percent + '% uploaded';
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
                                    progressBar.style.backgroundColor = '#4CAF50';
                                    // Don't redirect - receiver is rebooting
                                } else {
                                    statusDiv.innerHTML = '✅ Firmware uploaded! ESP-NOW command sent to transmitter.<br><br>Redirecting in <span id=\"countdown' + device + '\">10</span> seconds...';
                                    progressBar.style.backgroundColor = '#4CAF50';
                                    
                                    // Countdown and redirect
                                    let seconds = 10;
                                    const countdownInterval = setInterval(function() {
                                        seconds--;
                                        const countdownEl = document.getElementById('countdown' + device);
                                        if (countdownEl) {
                                            countdownEl.innerText = seconds;
                                        }
                                        if (seconds <= 0) {
                                            clearInterval(countdownInterval);
                                            window.location.href = '/';
                                        }
                                    }, 1000);
                                }
                            } else {
                                statusDiv.innerHTML = '❌ Failed: ' + response.message;
                                progressBar.style.backgroundColor = '#ff6b35';
                                uploadBtn.disabled = false;
                                uploadBtn.style.backgroundColor = '#ff6b35';
                                uploadBtn.innerText = 'Retry Upload';
                            }
                        } catch (e) {
                            statusDiv.innerHTML = '❌ Error parsing response';
                            uploadBtn.disabled = false;
                            uploadBtn.style.backgroundColor = '#ff6b35';
                            uploadBtn.innerText = 'Retry Upload';
                        }
                    } else {
                        statusDiv.innerHTML = '❌ Upload failed: HTTP ' + xhr.status;
                        progressBar.style.backgroundColor = '#ff6b35';
                        uploadBtn.disabled = false;
                        uploadBtn.style.backgroundColor = '#ff6b35';
                        uploadBtn.innerText = 'Retry Upload';
                    }
                });
                
                // Upload error
                xhr.addEventListener('error', function() {
                    statusDiv.innerHTML = '❌ Network error during upload';
                    progressBar.style.backgroundColor = '#ff6b35';
                    uploadBtn.disabled = false;
                    uploadBtn.style.backgroundColor = '#ff6b35';
                    uploadBtn.innerText = 'Retry Upload';
                });
                
                // Choose endpoint based on device
                const endpoint = (device === 'Receiver') ? '/api/ota_upload_receiver' : '/api/ota_upload';
                xhr.open('POST', endpoint);
                xhr.send(formData);
            });
        }
        
        window.onload = function() {
            console.log('OTA page loaded');
            
            // Load current receiver firmware info
            fetch('/api/firmware_info')
                .then(response => response.json())
                .then(data => {
                    const versionEl = document.getElementById('receiverVersion');
                    const buildEl = document.getElementById('receiverBuild');
                    
                    if (data.valid) {
                        versionEl.innerHTML = data.device + ' v' + data.version + ' <span style="color: #4CAF50;">●</span>';
                        buildEl.innerText = data.build_date;
                    } else {
                        versionEl.innerHTML = 'v' + data.version + ' <span style="color: #FFD700;">*</span>';
                        buildEl.innerText = data.build_date;
                    }
                })
                .catch(err => {
                    console.error('Failed to fetch firmware info:', err);
                    document.getElementById('receiverVersion').innerText = 'Error';
                    document.getElementById('receiverBuild').innerText = 'Failed to load';
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
                            versionEl.innerHTML = data.device + ' v' + data.version + ' ' + indicator;
                            buildEl.innerText = data.build_date || 'Unknown';
                        } else {
                            // Gray circle: Waiting for metadata from transmitter
                            versionEl.innerHTML = 'Waiting... <span style="color: #888;">○</span>';
                            buildEl.innerText = 'No metadata received yet';
                        }
                    })
                    .catch(err => {
                        console.error('Failed to fetch transmitter metadata:', err);
                        document.getElementById('transmitterVersion').innerHTML = 'Error';
                        document.getElementById('transmitterBuild').innerText = 'Failed to load';
                    });
            }
            
            // Initial fetch
            fetchTransmitterMetadata();
            
            // Periodically refresh transmitter metadata
            setInterval(fetchTransmitterMetadata, 5000);
        };
    )rawliteral";

    String html = generatePage("ESP-NOW Receiver - OTA Update", content, "", script);
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
