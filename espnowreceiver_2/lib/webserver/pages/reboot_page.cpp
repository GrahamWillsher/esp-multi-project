#include "reboot_page.h"
#include "../common/page_generator.h"

esp_err_t reboot_handler(httpd_req_t *req) {
    String content = R"rawliteral(
    <h1>ESP-NOW Receiver</h1>
    <h2>Reboot Transmitter</h2>
    <div style='margin-bottom: 20px;'>
        <a href='/' style='display: inline-block; padding: 10px 16px; background: #4CAF50; color: white; text-decoration: none; border-radius: 6px; font-weight: bold;'>
            ← Dashboard
        </a>
    </div>
    )rawliteral";
    
    content += R"rawliteral(
    
    <div class='info-box' style='text-align: center;'>
        <h3>Reboot Control</h3>
        <button id='confirmBtn' class='button' style='background-color: #ff6b35; font-size: 18px; padding: 15px 30px;'>
            Confirm Reboot
        </button>
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
                const restoreButton = () => {
                    confirmBtn.disabled = false;
                    confirmBtn.style.backgroundColor = '#ff6b35';
                    confirmBtn.innerText = 'Confirm Reboot';
                };

                confirmBtn.disabled = true;
                confirmBtn.style.cursor = 'not-allowed';
                confirmBtn.style.backgroundColor = '#ff9800';
                confirmBtn.innerText = 'Starting reboot sequence...';

                TransmitterReboot.run({
                    countdownSeconds: TransmitterReboot.COUNTDOWN_SECONDS,
                    updateCountdown: (seconds) => {
                        confirmBtn.style.backgroundColor = '#ff9800';
                        confirmBtn.innerText = 'Reboot in ' + seconds + 's...';
                    },
                    onCommandStart: () => {
                        confirmBtn.style.backgroundColor = '#ff9800';
                        confirmBtn.innerText = 'Sending...';
                    },
                    onSuccess: () => {
                        confirmBtn.innerText = '✓ Command sent';
                        confirmBtn.style.backgroundColor = '#28a745';
                    },
                    onFailure: (message) => {
                        console.error('Reboot failed:', message);
                        restoreButton();
                    },
                    onError: (error) => {
                        console.error('Reboot request failed:', error);
                        restoreButton();
                    }
                });
            });
        };
    )rawliteral";

    return send_rendered_page(req, "ESP-NOW Receiver - Reboot Transmitter", content, PageRenderOptions("", script));
}

esp_err_t register_reboot_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri = "/transmitter/reboot",
        .method = HTTP_GET,
        .handler = reboot_handler,
        .user_ctx = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
