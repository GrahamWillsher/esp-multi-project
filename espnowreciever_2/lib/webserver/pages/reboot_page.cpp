#include "reboot_page.h"
#include "../common/page_generator.h"
#include "../common/nav_buttons.h"

esp_err_t reboot_handler(httpd_req_t *req) {
    String content = R"rawliteral(
    <h1>ESP-NOW Receiver</h1>
    <h2>Reboot Transmitter</h2>
    )rawliteral";
    
    // Add navigation buttons
    content += "    " + generate_nav_buttons("/reboot");
    
    content += R"rawliteral(
    
    <div class='info-box' style='text-align: center;'>
        <h3>Reboot Control</h3>
        <div id='status' style='margin: 30px 0; font-size: 18px;'>
            ⚠️ Are you sure you want to reboot the transmitter?
        </div>
        <button id='confirmBtn' class='button' style='background-color: #ff6b35; font-size: 18px; padding: 15px 30px;'>
            Confirm Reboot
        </button>
        <div style='margin-top: 20px; color: #FFD700; font-size: 14px;'>
            The transmitter will restart and reconnect automatically.
        </div>
    </div>
    
    <div class='note'>
        ⚠️ Use with caution: This will immediately restart the remote transmitter device.
    </div>
)rawliteral";

    String script = R"rawliteral(
        window.onload = function() {
            console.log('Reboot page loaded');
            const confirmBtn = document.getElementById('confirmBtn');
            if (!confirmBtn) {
                console.error('Confirm button not found!');
                return;
            }
            
            confirmBtn.addEventListener('click', function() {
                console.log('Confirm button clicked');
                // Disable button to prevent double-clicks
                this.disabled = true;
                this.style.backgroundColor = '#666';
                this.innerText = 'Sending...';
                
                // Send reboot command via fetch
                fetch('/api/reboot')
                    .then(response => {
                        console.log('Response received:', response.status);
                        return response.json();
                    })
                    .then(data => {
                        console.log('Data received:', data);
                        const statusDiv = document.getElementById('status');
                        if (data.success) {
                            statusDiv.innerHTML = '✅ Reboot command sent successfully!<br><br>Redirecting to home in <span id="countdown">5</span> seconds...';
                            
                            // Countdown and redirect
                            let seconds = 5;
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
                            statusDiv.innerHTML = '❌ Failed: ' + data.message;
                            // Re-enable button on failure
                            this.disabled = false;
                            this.style.backgroundColor = '#ff6b35';
                            this.innerText = 'Confirm Reboot';
                        }
                    })
                    .catch(err => {
                        console.error('Fetch error:', err);
                        document.getElementById('status').innerHTML = '❌ Error: ' + err.message;
                        // Re-enable button on error
                        this.disabled = false;
                        this.style.backgroundColor = '#ff6b35';
                        this.innerText = 'Confirm Reboot';
                    });
            });
        };
    )rawliteral";

    String html = generatePage("ESP-NOW Receiver - Reboot Transmitter", content, "", script);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html.c_str(), html.length());
    return ESP_OK;
}

esp_err_t register_reboot_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri = "/reboot",
        .method = HTTP_GET,
        .handler = reboot_handler,
        .user_ctx = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
