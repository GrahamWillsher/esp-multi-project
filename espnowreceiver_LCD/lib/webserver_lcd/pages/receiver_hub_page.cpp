#include "receiver_hub_page.h"
#include "../common/page_generator.h"

#include <Arduino.h>
#include <WiFi.h>
#include <firmware_metadata.h>
#include <firmware_version.h>
#include <cstring>

namespace {
struct ReceiverHubState {
    char device_subtitle[64] = "Unknown Device";
    char status_color[16] = "#4CAF50";
    char status_text[24] = "Online";
    char ip_text[48] = "Not available";
    char version_text[24] = FW_VERSION_STRING;
    char build_date[32] = __DATE__;
} g_rx;

void format_display_name(const char* raw, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!raw || raw[0] == '\0') return;

    char scratch[96];
    strlcpy(scratch, raw, sizeof(scratch));
    size_t len = strlen(scratch);
    while (len > 1 && scratch[0] == '"' && scratch[len - 1] == '"') {
        memmove(scratch, scratch + 1, len - 2);
        scratch[len - 2] = '\0';
        len = strlen(scratch);
    }

    for (size_t i = 0; scratch[i] != '\0'; ++i) {
        if (scratch[i] == '-' || scratch[i] == '_') scratch[i] = ' ';
        if (i == 0 || scratch[i - 1] == ' ') {
            scratch[i] = static_cast<char>(toupper(static_cast<uint8_t>(scratch[i])));
        }
    }
    strlcpy(out, scratch, out_size);
}

esp_err_t receiver_hub_content_generator(httpd_req_t* req) {
    String content = R"rawliteral(
    <h1>📱 Receiver <span style='font-size: 0.5em; font-weight: 600;'>()rawliteral";
    content += g_rx.device_subtitle;
    content += R"rawliteral()</span></h1>

    <div class='info-box' style='margin: 20px 0; border-left: 5px solid )rawliteral";
    content += g_rx.status_color;
    content += R"rawliteral(;'>
        <h3 style='margin: 0 0 15px 0;'>&#128202; Status Summary</h3>
        <div style='display: grid; grid-template-columns: repeat(2, 1fr); gap: 15px;'>
            <div>
                <div style='color: #888; font-size: 13px;'>Connection</div>
                <div style='font-size: 18px; font-weight: bold; color: )rawliteral";
    content += g_rx.status_color;
    content += R"rawliteral(; margin-top: 5px;'>)rawliteral";
    content += g_rx.status_text;
    content += R"rawliteral(</div>
            </div>
            <div>
                <div style='color: #888; font-size: 13px;'>IP Address</div>
                <div style='font-size: 16px; font-weight: bold; margin-top: 5px; font-family: monospace;'>)rawliteral";
    content += g_rx.ip_text;
    content += R"rawliteral(</div>
            </div>
            <div>
                <div style='color: #888; font-size: 13px;'>Firmware</div>
                <div style='font-size: 16px; font-weight: bold; margin-top: 5px;'>)rawliteral";
    content += g_rx.version_text;
    content += R"rawliteral(</div>
            </div>
            <div>
                <div style='color: #888; font-size: 13px;'>Build Date</div>
                <div style='font-size: 13px; margin-top: 5px; color: #888;'>)rawliteral";
    content += g_rx.build_date;
    content += R"rawliteral(</div>
            </div>
        </div>
    </div>

    <h3 style='margin: 30px 0 15px 0;'>&#9881;&#65039; Functions</h3>
    <div style='display: grid; grid-template-columns: repeat(2, 1fr); gap: 15px;'>
        <a href='/receiver/config' style='text-decoration: none;'>
            <div class='info-box' style='cursor: pointer; text-align: center; transition: transform 0.2s, border-color 0.2s; border: 2px solid #4CAF50;'
                 onmouseover='this.style.transform="translateY(-3px)"; this.style.borderColor="#6CCF84"'
                 onmouseout='this.style.transform="translateY(0)"; this.style.borderColor="#4CAF50"'>
                <div style='font-size: 36px; margin: 10px 0;'>&#9881;&#65039;</div>
                <div style='font-weight: bold; color: #4CAF50; font-size: 16px;'>Configuration</div>
                <div style='font-size: 12px; color: #888; margin-top: 8px;'>WiFi, IP, MQTT, Display</div>
            </div>
        </a>

        <a href='/receiver/memoryhealth' style='text-decoration: none;'>
            <div class='info-box' style='cursor: pointer; text-align: center; transition: transform 0.2s, border-color 0.2s; border: 2px solid #4CAF50;'
                 onmouseover='this.style.transform="translateY(-3px)"; this.style.borderColor="#6CCF84"'
                 onmouseout='this.style.transform="translateY(0)"; this.style.borderColor="#4CAF50"'>
                <div style='font-size: 36px; margin: 10px 0;'>&#129504;</div>
                <div style='font-weight: bold; color: #4CAF50; font-size: 16px;'>Memory Health</div>
                <div style='font-size: 12px; color: #888; margin-top: 8px;'>Pressure gate + heap</div>
            </div>
        </a>
    </div>
    )rawliteral";

    return send_page_content_chunk(req, "receiver_hub_content", content.c_str(), content.length());
}
}

static esp_err_t receiver_hub_handler(httpd_req_t* req) {
    strlcpy(g_rx.status_text, "Online", sizeof(g_rx.status_text));
    strlcpy(g_rx.status_color, "#4CAF50", sizeof(g_rx.status_color));

    wifi_mode_t mode = WiFi.getMode();
    if ((mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) && WiFi.status() != WL_CONNECTED) {
        strlcpy(g_rx.status_text, "AP Mode", sizeof(g_rx.status_text));
        strlcpy(g_rx.status_color, "#FF9800", sizeof(g_rx.status_color));
    }

    {
        String ip = WiFi.localIP().toString();
        if (ip == "0.0.0.0") {
            ip = WiFi.softAPIP().toString();
        }
        if (ip == "0.0.0.0") {
            ip = "Not available";
        }
        strlcpy(g_rx.ip_text, ip.c_str(), sizeof(g_rx.ip_text));
    }

    strlcpy(g_rx.version_text, FW_VERSION_STRING, sizeof(g_rx.version_text));
    strlcpy(g_rx.build_date, __DATE__, sizeof(g_rx.build_date));
    strlcpy(g_rx.device_subtitle, "Unknown Device", sizeof(g_rx.device_subtitle));

    if (FirmwareMetadata::isValid(FirmwareMetadata::metadata)) {
        format_display_name(FirmwareMetadata::metadata.env_name, g_rx.device_subtitle, sizeof(g_rx.device_subtitle));

        snprintf(g_rx.version_text, sizeof(g_rx.version_text), "v%u.%u.%u",
                 static_cast<unsigned>(FirmwareMetadata::metadata.version_major),
                 static_cast<unsigned>(FirmwareMetadata::metadata.version_minor),
                 static_cast<unsigned>(FirmwareMetadata::metadata.version_patch));

        if (FirmwareMetadata::metadata.build_date[0] != '\0') {
            strlcpy(g_rx.build_date, FirmwareMetadata::metadata.build_date, sizeof(g_rx.build_date));
            for (size_t i = 0; g_rx.build_date[i] != '\0'; ++i) {
                if (g_rx.build_date[i] == '-') g_rx.build_date[i] = ' ';
            }
        }
    }

    return send_rendered_page_streaming(req,
                                       "Receiver Hub",
                                       receiver_hub_content_generator,
                                       PageRenderOptions("", ""));
}

esp_err_t register_receiver_hub_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/receiver",
        .method    = HTTP_GET,
        .handler   = receiver_hub_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
