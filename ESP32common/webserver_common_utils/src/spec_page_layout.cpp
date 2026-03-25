#include <webserver_common_utils/spec_page_layout.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>

namespace WebserverCommonSpecLayout {

// ============================================================================
// Static CSS block — the complete spec-page stylesheet, parameterised only via
// CSS custom properties (:root variables injected at runtime).  The array lives
// in flash (.rodata) and is never copied to the heap.
// ============================================================================
static const char kSpecPageStaticCss[] =
        "        * { margin: 0; padding: 0; box-sizing: border-box; }\n"
        "        body {\n"
        "            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;\n"
        "            background: linear-gradient(135deg, var(--grad-start) 0%, var(--grad-end) 100%);\n"
        "            min-height: 100vh;\n"
        "            padding: 20px;\n"
        "        }\n"
        "        .container { max-width: 900px; margin: 0 auto; }\n"
        "        .header {\n"
        "            background: rgba(255, 255, 255, 0.95);\n"
        "            border-radius: 12px;\n"
        "            padding: 30px;\n"
        "            margin-bottom: 20px;\n"
        "            box-shadow: 0 10px 40px rgba(0, 0, 0, 0.1);\n"
        "        }\n"
        "        .header h1 {\n"
        "            color: #333;\n"
        "            margin-bottom: 10px;\n"
        "            font-size: 2.5em;\n"
        "        }\n"
        "        .header p {\n"
        "            color: black;\n"
        "            font-size: 1.1em;\n"
        "            font-weight: 600;\n"
        "        }\n"
        "        .specs-grid {\n"
        "            display: grid;\n"
        "            grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));\n"
        "            gap: 20px;\n"
        "            margin-bottom: 20px;\n"
        "        }\n"
        "        .spec-card {\n"
        "            background: white;\n"
        "            border-radius: 12px;\n"
        "            padding: 25px;\n"
        "            box-shadow: 0 5px 20px rgba(0, 0, 0, 0.1);\n"
        "            border-left: 5px solid var(--accent);\n"
        "            transition: transform 0.3s ease, box-shadow 0.3s ease;\n"
        "        }\n"
        "        .spec-card:hover {\n"
        "            transform: translateY(-5px);\n"
        "            box-shadow: 0 10px 30px rgba(0, 0, 0, 0.15);\n"
        "        }\n"
        "        .spec-label {\n"
        "            font-size: 0.9em;\n"
        "            color: #888;\n"
        "            text-transform: uppercase;\n"
        "            letter-spacing: 1px;\n"
        "            margin-bottom: 8px;\n"
        "            font-weight: 600;\n"
        "        }\n"
        "        .spec-value {\n"
        "            font-size: 1.8em;\n"
        "            color: #333;\n"
        "            font-weight: 700;\n"
        "            margin-bottom: 5px;\n"
        "            word-break: break-word;\n"
        "        }\n"
        "        .spec-unit {\n"
        "            font-size: 0.9em;\n"
        "            color: #999;\n"
        "        }\n"
        "        .feature-badge {\n"
        "            display: inline-block;\n"
        "            padding: 5px 12px;\n"
        "            background: var(--accent);\n"
        "            color: white;\n"
        "            border-radius: 20px;\n"
        "            font-size: 0.85em;\n"
        "            margin-right: 5px;\n"
        "            margin-top: 5px;\n"
        "        }\n"
        "        .feature-badge.enabled { background: #20c997; }\n"
        "        .feature-badge.disabled { background: #ccc; }\n"
        "        .source-info {\n"
        "            padding: 15px 20px;\n"
        "            background: rgba(255, 255, 255, 0.25);\n"
        "            border: 1px solid var(--accent);\n"
        "            border-radius: 8px;\n"
        "            color: black;\n"
        "            font-size: 0.95em;\n"
        "            text-align: center;\n"
        "            margin-bottom: 20px;\n"
        "            font-weight: 500;\n"
        "        }\n"
        "        .status-grid {\n"
        "            display: grid;\n"
        "            grid-template-columns: repeat(2, 1fr);\n"
        "            gap: 15px;\n"
        "            padding: 20px;\n"
        "            background: white;\n"
        "            border-radius: 12px;\n"
        "            box-shadow: 0 5px 20px rgba(0, 0, 0, 0.1);\n"
        "            margin-bottom: 20px;\n"
        "        }\n"
        "        .status-item {\n"
        "            padding: 15px;\n"
        "            background: #f8f9fa;\n"
        "            border-radius: 8px;\n"
        "            border-left: 4px solid var(--accent);\n"
        "        }\n"
        "        .status-label { color: black; font-size: 0.9em; }\n"
        "        .status-value { color: #333; font-size: 1.4em; font-weight: 700; }\n"
        "        .nav-buttons {\n"
        "            display: flex;\n"
        "            gap: 10px;\n"
        "            justify-content: center;\n"
        "            margin-top: 20px;\n"
        "            flex-wrap: wrap;\n"
        "        }\n"
        "        .btn {\n"
        "            padding: 12px 24px;\n"
        "            border: none;\n"
        "            border-radius: 8px;\n"
        "            font-size: 1em;\n"
        "            font-weight: 600;\n"
        "            cursor: pointer;\n"
        "            transition: all 0.3s ease;\n"
        "            text-decoration: none;\n"
        "            display: inline-block;\n"
        "        }\n"
        "        .btn-primary {\n"
        "            background: var(--accent);\n"
        "            color: white;\n"
        "        }\n"
        "        .btn-primary:hover {\n"
        "            filter: brightness(0.9);\n"
        "            box-shadow: 0 5px 15px rgba(0, 0, 0, 0.2);\n"
        "        }\n"
        "        .btn-secondary {\n"
        "            background: white;\n"
        "            color: var(--accent);\n"
        "            border: 2px solid var(--accent);\n"
        "        }\n"
        "        .btn-secondary:hover {\n"
        "            background: var(--accent);\n"
        "            color: white;\n"
        "        }\n"
        "        @media (max-width: 768px) {\n"
        "            .header h1 { font-size: 1.8em; }\n"
        "            .specs-grid { grid-template-columns: 1fr; }\n"
        "            .status-grid { grid-template-columns: 1fr; }\n"
        "        }\n";

String build_spec_page_html_header(const String& page_title,
                                   const String& heading,
                                   const String& subtitle,
                                   const String& source_topic,
                                   const String& gradient_start,
                                   const String& gradient_end,
                                   const String& accent_color) {
    String html;
    // Reserve is small: only dynamic metadata is concatenated; the ~2 kB static
    // CSS block is appended as a single operation via kSpecPageStaticCss.
    html.reserve(512);

    html += "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
            "    <meta charset=\"UTF-8\">\n"
            "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
            "    <title>";
    html += page_title;
    html += "</title>\n    <style>\n    :root {\n        --accent: ";
    html += accent_color;
    html += ";\n        --grad-start: ";
    html += gradient_start;
    html += ";\n        --grad-end: ";
    html += gradient_end;
    html += ";\n    }\n";
    html += kSpecPageStaticCss;   // ~2 kB static block — single append from flash
    html += "    </style>\n</head>\n<body>\n    <div class=\"container\">\n"
            "        <div class=\"header\">\n            <h1>";
    html += heading;
    html += "</h1>\n"
            "            <p>";
    html += subtitle;
    html += "</p>\n"
            "        </div>\n\n"
            "        <div class=\"source-info\">\n"
            "            &#128225; Source: Battery Emulator via MQTT Topic: <strong>";
    html += source_topic;
    html += "</strong>\n"
            "        </div>\n";

    return html;
}

String build_spec_page_html_footer(const String& nav_links_html,
                                   const String& inline_script) {
    String html;
    html.reserve(1024 + inline_script.length());

    html += "        <div class=\"nav-buttons\">\n";
    html += nav_links_html;
    html += "        </div>\n"
            "    </div>\n";

    if (inline_script.length() > 0) {
        html += "    <script>\n";
        html += inline_script;
        html += "\n    </script>\n";
    }

    html += "</body>\n"
            "</html>\n";
    return html;
}

String build_spec_page_nav_links(const SpecPageNavLink* links,
                                 size_t link_count) {
    String html;
    html.reserve(link_count * 96);
    char link_buf[192] = {0};

    for (size_t i = 0; i < link_count; ++i) {
        if (!links[i].href || !links[i].label) {
            continue;
        }

        const int written = snprintf(link_buf,
                                     sizeof(link_buf),
                                     "            <a href=\"%s\" class=\"btn btn-secondary\">%s</a>\\n",
                                     links[i].href,
                                     links[i].label);
        if (written <= 0 || static_cast<size_t>(written) >= sizeof(link_buf)) {
            continue;
        }

        html += link_buf;
    }

    return html;
}

esp_err_t send_chunked_html_response(httpd_req_t* req,
                                     const String& html_header,
                                     const char* html_body,
                                     const String& html_footer,
                                     const char* log_tag) {
    if (!req || !html_body) {
        return ESP_ERR_INVALID_ARG;
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");

    auto send_chunk = [&](const char* chunk, size_t len) -> esp_err_t {
        if (!chunk || len == 0) {
            return ESP_OK;
        }
        return httpd_resp_send_chunk(req, chunk, len);
    };

    const size_t body_len = strlen(html_body);
    if (send_chunk(html_header.c_str(), html_header.length()) != ESP_OK ||
        send_chunk(html_body, body_len) != ESP_OK ||
        send_chunk(html_footer.c_str(), html_footer.length()) != ESP_OK ||
        httpd_resp_send_chunk(req, nullptr, 0) != ESP_OK) {
        (void)log_tag;
        return ESP_FAIL;
    }

    (void)log_tag;
    return ESP_OK;
}

static esp_err_t send_spec_page_response_v(httpd_req_t*           req,
                                            const SpecPageParams&  params,
                                            const SpecPageNavLink* nav_links,
                                            size_t                 nav_link_count,
                                            const char*            inline_script,
                                            const char*            specs_fmt,
                                            size_t                 specs_buf_size,
                                            bool                   use_psram,
                                            const char*            log_tag,
                                            va_list                args) {
    if (!req || !params.page_title || !params.heading || !params.subtitle ||
        !params.source_topic || !params.gradient_start || !params.gradient_end ||
        !params.accent_color || !specs_fmt) {
        return ESP_ERR_INVALID_ARG;
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");

    // Chunk-send helpers — return false on send failure.
    const auto ck = [&](const char* data, size_t len) -> bool {
        return len == 0 || (httpd_resp_send_chunk(req, data, len) == ESP_OK);
    };
    const auto cks = [&](const char* s) -> bool {
        return !s || ck(s, strlen(s));
    };

    // 1. Static pre-title
    static const char kPreTitle[] =
        "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
        "    <meta charset=\"UTF-8\">\n"
        "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
        "    <title>";
    if (!ck(kPreTitle, sizeof(kPreTitle) - 1)) return ESP_FAIL;
    if (!cks(params.page_title)) return ESP_FAIL;

    // 2. :root CSS custom properties (3 dynamic colour values)
    static const char kToRoot[] = "</title>\n    <style>\n    :root {\n        --accent: ";
    if (!ck(kToRoot, sizeof(kToRoot) - 1)) return ESP_FAIL;
    if (!cks(params.accent_color)) return ESP_FAIL;
    static const char kGradStart[] = ";\n        --grad-start: ";
    if (!ck(kGradStart, sizeof(kGradStart) - 1)) return ESP_FAIL;
    if (!cks(params.gradient_start)) return ESP_FAIL;
    static const char kGradEnd[] = ";\n        --grad-end: ";
    if (!ck(kGradEnd, sizeof(kGradEnd) - 1)) return ESP_FAIL;
    if (!cks(params.gradient_end)) return ESP_FAIL;
    static const char kRootClose[] = ";\n    }\n";
    if (!ck(kRootClose, sizeof(kRootClose) - 1)) return ESP_FAIL;

    // 3. Static CSS body — sent directly from flash, never copied to heap
    if (!ck(kSpecPageStaticCss, sizeof(kSpecPageStaticCss) - 1)) return ESP_FAIL;

    // 4. Close style, open body + header card + dynamic heading
    static const char kBodyOpen[] =
        "    </style>\n</head>\n<body>\n    <div class=\"container\">\n"
        "        <div class=\"header\">\n            <h1>";
    if (!ck(kBodyOpen, sizeof(kBodyOpen) - 1)) return ESP_FAIL;
    if (!cks(params.heading)) return ESP_FAIL;
    static const char kH1ToP[] = "</h1>\n            <p>";
    if (!ck(kH1ToP, sizeof(kH1ToP) - 1)) return ESP_FAIL;
    if (!cks(params.subtitle)) return ESP_FAIL;
    static const char kPToSrc[] =
        "</p>\n        </div>\n\n        <div class=\"source-info\">\n"
        "            &#128225; Source: Battery Emulator via MQTT Topic: <strong>";
    if (!ck(kPToSrc, sizeof(kPToSrc) - 1)) return ESP_FAIL;
    if (!cks(params.source_topic)) return ESP_FAIL;
    static const char kSrcClose[] = "</strong>\n        </div>\n";
    if (!ck(kSrcClose, sizeof(kSrcClose) - 1)) return ESP_FAIL;

    // 5. Specs body (stack or PSRAM scratch buffer)
    char stack_buf[2048];
    char* specs_buf = stack_buf;
    bool  free_buf  = false;
    if (use_psram || specs_buf_size > sizeof(stack_buf)) {
        specs_buf = static_cast<char*>(ps_malloc(specs_buf_size));
        if (!specs_buf) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory error");
            return ESP_FAIL;
        }
        free_buf = true;
    }
    vsnprintf(specs_buf, specs_buf_size, specs_fmt, args);
    const bool specs_ok = ck(specs_buf, strlen(specs_buf));
    if (free_buf) { free(specs_buf); }
    if (!specs_ok) return ESP_FAIL;

    // 6. Nav footer
    static const char kNavOpen[] = "        <div class=\"nav-buttons\">\n";
    if (!ck(kNavOpen, sizeof(kNavOpen) - 1)) return ESP_FAIL;
    char link_buf[192];
    for (size_t i = 0; i < nav_link_count; ++i) {
        if (!nav_links || !nav_links[i].href || !nav_links[i].label) { continue; }
        const int wlen = snprintf(link_buf, sizeof(link_buf),
                                  "            <a href=\"%s\" class=\"btn btn-secondary\">%s</a>\n",
                                  nav_links[i].href, nav_links[i].label);
        if (wlen > 0 && static_cast<size_t>(wlen) < sizeof(link_buf)) {
            if (!ck(link_buf, static_cast<size_t>(wlen))) return ESP_FAIL;
        }
    }
    static const char kNavClose[] = "        </div>\n    </div>\n";
    if (!ck(kNavClose, sizeof(kNavClose) - 1)) return ESP_FAIL;

    // 7. Optional inline <script>
    if (inline_script && inline_script[0] != '\0') {
        static const char kScriptOpen[] = "    <script>\n";
        if (!ck(kScriptOpen, sizeof(kScriptOpen) - 1)) return ESP_FAIL;
        if (!cks(inline_script)) return ESP_FAIL;
        static const char kScriptClose[] = "\n    </script>\n";
        if (!ck(kScriptClose, sizeof(kScriptClose) - 1)) return ESP_FAIL;
    }

    // 8. Close body/html + finalize chunked transfer
    static const char kPageClose[] = "</body>\n</html>\n";
    if (!ck(kPageClose, sizeof(kPageClose) - 1)) return ESP_FAIL;
    if (httpd_resp_send_chunk(req, nullptr, 0) != ESP_OK) return ESP_FAIL;

    (void)log_tag;
    return ESP_OK;
}

esp_err_t send_spec_page_response(httpd_req_t*           req,
                                   const SpecPageParams&  params,
                                   const SpecPageNavLink* nav_links,
                                   size_t                 nav_link_count,
                                   const char*            inline_script,
                                   const char*            specs_fmt,
                                   size_t                 specs_buf_size,
                                   bool                   use_psram,
                                   const char*            log_tag,
                                   ...) {
    va_list args;
    va_start(args, log_tag);
    const esp_err_t result = send_spec_page_response_v(
        req, params, nav_links, nav_link_count, inline_script,
        specs_fmt, specs_buf_size, use_psram, log_tag, args);
    va_end(args);
    return result;
}

} // namespace WebserverCommonSpecLayout
