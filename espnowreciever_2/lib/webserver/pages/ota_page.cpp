#include "ota_page.h"
#include "../common/page_generator.h"
#include "../common/nav_buttons.h"
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
    
    <div class='info-box' style='text-align: center;'>
        <h3>Upload Firmware to Transmitter</h3>
        <div id='status' style='margin: 30px 0; font-size: 18px;'>
            📁 Select firmware file (.bin)
        </div>
        
        <div style='margin: 20px auto; max-width: 500px;'>
            <input type='file' id='firmwareFile' accept='.bin' 
                   style='display: block; margin: 20px auto; padding: 10px; font-size: 16px; cursor: pointer;'>
         </div>
        
        <button id='uploadBtn' class='button' disabled 
                style='background-color: #666; font-size: 18px; padding: 15px 30px;'>
            Select File First
        </button>
        
        <div style='margin-top: 20px; color: #FFD700; font-size: 14px;'>
            The transmitter will receive and install the firmware directly via HTTP.
        </div>
        
        <div id='progress' style='margin-top: 20px; display: none;'>
            <div style='background-color: #333; border-radius: 5px; overflow: hidden;'>
                <div id='progressBar' style='background-color: #4CAF50; height: 30px; width: 0%; transition: width 0.3s;'></div>
            </div>
            <div id='progressText' style='margin-top: 10px; color: #FFD700;'>0%</div>
        </div>
    </div>
    
    <div class='note'>
        ⚠️ Important: Ensure the firmware file is compatible with ESP32-POE-ISO (WROVER).<br>
        📝 Expected file location: Select from your computer's file system.
    </div>
)rawliteral";

    String script = R"rawliteral(
        let selectedFile = null;
        
        window.onload = function() {
            console.log('OTA page loaded');
            const fileInput = document.getElementById('firmwareFile');
            const uploadBtn = document.getElementById('uploadBtn');
            const statusDiv = document.getElementById('status');
            
            if (!fileInput || !uploadBtn || !statusDiv) {
                console.error('Required elements not found!');
                return;
            }
            
            // Handle file selection
            fileInput.addEventListener('change', function(e) {
                if (e.target.files.length > 0) {
                    selectedFile = e.target.files[0];
                    const sizeMB = (selectedFile.size / 1024 / 1024).toFixed(2);
                    statusDiv.innerHTML = '📄 ' + selectedFile.name + ' (' + sizeMB + ' MB)';
                    uploadBtn.disabled = false;
                    uploadBtn.style.backgroundColor = '#ff6b35';
                    uploadBtn.innerText = 'Upload and Update Transmitter';
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
                
                console.log('Starting OTA upload...');
                uploadBtn.disabled = true;
                uploadBtn.style.backgroundColor = '#666';
                uploadBtn.innerText = 'Uploading...';
                
                const statusDiv = document.getElementById('status');
                const progressDiv = document.getElementById('progress');
                const progressBar = document.getElementById('progressBar');
                const progressText = document.getElementById('progressText');
                
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
                                statusDiv.innerHTML = '✅ Firmware uploaded! ESP-NOW command sent to transmitter.<br><br>Redirecting in <span id=\"countdown\">10</span> seconds...';
                                progressBar.style.backgroundColor = '#4CAF50';
                                
                                // Countdown and redirect
                                let seconds = 10;
                                const countdownInterval = setInterval(function() {
                                    seconds--;
                                    const countdownEl = document.getElementById('countdown');
                                    if (countdownEl) {
                                        countdownEl.innerText = seconds;
                                    }
                                    if (seconds <= 0) {
                                        clearInterval(countdownInterval);
                                        window.location.href = '/';
                                    }
                                }, 1000);
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
                
                xhr.open('POST', '/api/ota_upload');
                xhr.send(formData);
            });
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
