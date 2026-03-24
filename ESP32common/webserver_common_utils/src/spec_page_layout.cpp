#include <webserver_common_utils/spec_page_layout.h>

namespace WebserverCommonSpecLayout {

String build_spec_page_html_header(const String& page_title,
                                   const String& heading,
                                   const String& subtitle,
                                   const String& source_topic,
                                   const String& gradient_start,
                                   const String& gradient_end,
                                   const String& accent_color) {
    String html;
    html.reserve(3600);

    html += "<!DOCTYPE html>\n";
    html += "<html lang=\"en\">\n";
    html += "<head>\n";
    html += "    <meta charset=\"UTF-8\">\n";
    html += "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
    html += "    <title>" + page_title + "</title>\n";
    html += "    <style>\n";
    html += "        * { margin: 0; padding: 0; box-sizing: border-box; }\n";
    html += "        body {\n";
    html += "            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;\n";
    html += "            background: linear-gradient(135deg, " + gradient_start + " 0%, " + gradient_end + " 100%);\n";
    html += "            min-height: 100vh;\n";
    html += "            padding: 20px;\n";
    html += "        }\n";
    html += "        .container { max-width: 900px; margin: 0 auto; }\n";
    html += "        .header {\n";
    html += "            background: rgba(255, 255, 255, 0.95);\n";
    html += "            border-radius: 12px;\n";
    html += "            padding: 30px;\n";
    html += "            margin-bottom: 20px;\n";
    html += "            box-shadow: 0 10px 40px rgba(0, 0, 0, 0.1);\n";
    html += "        }\n";
    html += "        .header h1 {\n";
    html += "            color: #333;\n";
    html += "            margin-bottom: 10px;\n";
    html += "            font-size: 2.5em;\n";
    html += "        }\n";
    html += "        .header p {\n";
    html += "            color: black;\n";
    html += "            font-size: 1.1em;\n";
    html += "            font-weight: 600;\n";
    html += "        }\n";
    html += "        .specs-grid {\n";
    html += "            display: grid;\n";
    html += "            grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));\n";
    html += "            gap: 20px;\n";
    html += "            margin-bottom: 20px;\n";
    html += "        }\n";
    html += "        .spec-card {\n";
    html += "            background: white;\n";
    html += "            border-radius: 12px;\n";
    html += "            padding: 25px;\n";
    html += "            box-shadow: 0 5px 20px rgba(0, 0, 0, 0.1);\n";
    html += "            border-left: 5px solid " + accent_color + ";\n";
    html += "            transition: transform 0.3s ease, box-shadow 0.3s ease;\n";
    html += "        }\n";
    html += "        .spec-card:hover {\n";
    html += "            transform: translateY(-5px);\n";
    html += "            box-shadow: 0 10px 30px rgba(0, 0, 0, 0.15);\n";
    html += "        }\n";
    html += "        .spec-label {\n";
    html += "            font-size: 0.9em;\n";
    html += "            color: #888;\n";
    html += "            text-transform: uppercase;\n";
    html += "            letter-spacing: 1px;\n";
    html += "            margin-bottom: 8px;\n";
    html += "            font-weight: 600;\n";
    html += "        }\n";
    html += "        .spec-value {\n";
    html += "            font-size: 1.8em;\n";
    html += "            color: #333;\n";
    html += "            font-weight: 700;\n";
    html += "            margin-bottom: 5px;\n";
    html += "            word-break: break-word;\n";
    html += "        }\n";
    html += "        .spec-unit {\n";
    html += "            font-size: 0.9em;\n";
    html += "            color: #999;\n";
    html += "        }\n";
    html += "        .feature-badge {\n";
    html += "            display: inline-block;\n";
    html += "            padding: 5px 12px;\n";
    html += "            background: " + accent_color + ";\n";
    html += "            color: white;\n";
    html += "            border-radius: 20px;\n";
    html += "            font-size: 0.85em;\n";
    html += "            margin-right: 5px;\n";
    html += "            margin-top: 5px;\n";
    html += "        }\n";
    html += "        .feature-badge.enabled { background: #20c997; }\n";
    html += "        .feature-badge.disabled { background: #ccc; }\n";
    html += "        .source-info {\n";
    html += "            padding: 15px 20px;\n";
    html += "            background: rgba(255, 255, 255, 0.25);\n";
    html += "            border: 1px solid " + accent_color + ";\n";
    html += "            border-radius: 8px;\n";
    html += "            color: black;\n";
    html += "            font-size: 0.95em;\n";
    html += "            text-align: center;\n";
    html += "            margin-bottom: 20px;\n";
    html += "            font-weight: 500;\n";
    html += "        }\n";
    html += "        .status-grid {\n";
    html += "            display: grid;\n";
    html += "            grid-template-columns: repeat(2, 1fr);\n";
    html += "            gap: 15px;\n";
    html += "            padding: 20px;\n";
    html += "            background: white;\n";
    html += "            border-radius: 12px;\n";
    html += "            box-shadow: 0 5px 20px rgba(0, 0, 0, 0.1);\n";
    html += "            margin-bottom: 20px;\n";
    html += "        }\n";
    html += "        .status-item {\n";
    html += "            padding: 15px;\n";
    html += "            background: #f8f9fa;\n";
    html += "            border-radius: 8px;\n";
    html += "            border-left: 4px solid " + accent_color + ";\n";
    html += "        }\n";
    html += "        .status-label { color: black; font-size: 0.9em; }\n";
    html += "        .status-value { color: #333; font-size: 1.4em; font-weight: 700; }\n";
    html += "        .nav-buttons {\n";
    html += "            display: flex;\n";
    html += "            gap: 10px;\n";
    html += "            justify-content: center;\n";
    html += "            margin-top: 20px;\n";
    html += "            flex-wrap: wrap;\n";
    html += "        }\n";
    html += "        .btn {\n";
    html += "            padding: 12px 24px;\n";
    html += "            border: none;\n";
    html += "            border-radius: 8px;\n";
    html += "            font-size: 1em;\n";
    html += "            font-weight: 600;\n";
    html += "            cursor: pointer;\n";
    html += "            transition: all 0.3s ease;\n";
    html += "            text-decoration: none;\n";
    html += "            display: inline-block;\n";
    html += "        }\n";
    html += "        .btn-primary {\n";
    html += "            background: " + accent_color + ";\n";
    html += "            color: white;\n";
    html += "        }\n";
    html += "        .btn-primary:hover {\n";
    html += "            filter: brightness(0.9);\n";
    html += "            box-shadow: 0 5px 15px rgba(0, 0, 0, 0.2);\n";
    html += "        }\n";
    html += "        .btn-secondary {\n";
    html += "            background: white;\n";
    html += "            color: " + accent_color + ";\n";
    html += "            border: 2px solid " + accent_color + ";\n";
    html += "        }\n";
    html += "        .btn-secondary:hover {\n";
    html += "            background: " + accent_color + ";\n";
    html += "            color: white;\n";
    html += "        }\n";
    html += "        @media (max-width: 768px) {\n";
    html += "            .header h1 { font-size: 1.8em; }\n";
    html += "            .specs-grid { grid-template-columns: 1fr; }\n";
    html += "            .status-grid { grid-template-columns: 1fr; }\n";
    html += "        }\n";
    html += "    </style>\n";
    html += "</head>\n";
    html += "<body>\n";
    html += "    <div class=\"container\">\n";
    html += "        <div class=\"header\">\n";
    html += "            <h1>" + heading + "</h1>\n";
    html += "            <p>" + subtitle + "</p>\n";
    html += "        </div>\n\n";
    html += "        <div class=\"source-info\">\n";
    html += "            &#128225; Source: Battery Emulator via MQTT Topic: <strong>" + source_topic + "</strong>\n";
    html += "        </div>\n";

    return html;
}

String build_spec_page_html_footer(const String& nav_links_html,
                                   const String& inline_script) {
    String html;
    html.reserve(1024 + inline_script.length());

    html += "        <div class=\"nav-buttons\">\n";
    html += nav_links_html;
    html += "        </div>\n";
    html += "    </div>\n";

    if (inline_script.length() > 0) {
        html += "    <script>\n";
        html += inline_script;
        html += "\n    </script>\n";
    }

    html += "</body>\n";
    html += "</html>\n";
    return html;
}

String build_spec_page_nav_links(const SpecPageNavLink* links,
                                 size_t link_count) {
    String html;
    html.reserve(link_count * 96);

    for (size_t i = 0; i < link_count; ++i) {
        if (!links[i].href || !links[i].label) {
            continue;
        }

        html += "            <a href=\"";
        html += links[i].href;
        html += "\" class=\"btn btn-secondary\">";
        html += links[i].label;
        html += "</a>\n";
    }

    return html;
}

} // namespace WebserverCommonSpecLayout
