#include "systemtools_hub_page.h"
#include "../common/page_generator.h"

#include <cstring>

namespace {
esp_err_t systemtools_hub_content_generator(httpd_req_t* req) {
    const char* content = R"rawliteral(
    <h1>🛠️ System Tools</h1>

    <div class='info-box' style='margin: 20px 0; border-left: 5px solid #FF9800;'>
        <h3 style='margin: 0 0 15px 0;'>&#9881;&#65039; Functions</h3>
        <div style='display: grid; grid-template-columns: repeat(2, 1fr); gap: 15px;'>
            <a href='/systemtools/debug' style='text-decoration: none;'>
                <div class='info-box' style='cursor: pointer; text-align: center; transition: transform 0.2s, border-color 0.2s; border: 2px solid #FF9800;'
                     onmouseover='this.style.transform="translateY(-3px)"; this.style.borderColor="#ffb74d"'
                     onmouseout='this.style.transform="translateY(0)"; this.style.borderColor="#FF9800"'>
                    <div style='font-size: 36px; margin: 10px 0;'>&#128027;</div>
                    <div style='font-weight: bold; color: #FF9800; font-size: 16px;'>Debug Logging</div>
                    <div style='font-size: 12px; color: #888; margin-top: 8px;'>Transmitter debug level control</div>
                </div>
            </a>

            <a href='/systemtools/ota' style='text-decoration: none;'>
                <div class='info-box' style='cursor: pointer; text-align: center; transition: transform 0.2s, border-color 0.2s; border: 2px solid #FF9800;'
                     onmouseover='this.style.transform="translateY(-3px)"; this.style.borderColor="#ffb74d"'
                     onmouseout='this.style.transform="translateY(0)"; this.style.borderColor="#FF9800"'>
                    <div style='font-size: 36px; margin: 10px 0;'>&#128228;</div>
                    <div style='font-weight: bold; color: #FF9800; font-size: 16px;'>OTA Update</div>
                    <div style='font-size: 12px; color: #888; margin-top: 8px;'>Receiver + transmitter firmware update</div>
                </div>
            </a>

            <a href='/systemtools/events' style='text-decoration: none;'>
                <div class='info-box' style='cursor: pointer; text-align: center; transition: transform 0.2s, border-color 0.2s; border: 2px solid #FF9800;'
                     onmouseover='this.style.transform="translateY(-3px)"; this.style.borderColor="#ffb74d"'
                     onmouseout='this.style.transform="translateY(0)"; this.style.borderColor="#FF9800"'>
                    <div style='font-size: 36px; margin: 10px 0;'>&#128203;</div>
                    <div style='font-weight: bold; color: #FF9800; font-size: 16px;'>Event Logs</div>
                    <div style='font-size: 12px; color: #888; margin-top: 8px;'>History, errors and live events</div>
                </div>
            </a>
        </div>
    </div>
    )rawliteral";

    return send_page_content_chunk(req, "systemtools_hub_content", content, strlen(content));
}
}

static esp_err_t systemtools_hub_handler(httpd_req_t* req) {
    return send_rendered_page_streaming(req,
                                        "System Tools",
                                        systemtools_hub_content_generator,
                                        PageRenderOptions("", ""));
}

esp_err_t register_systemtools_hub_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/systemtools",
        .method    = HTTP_GET,
        .handler   = systemtools_hub_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
