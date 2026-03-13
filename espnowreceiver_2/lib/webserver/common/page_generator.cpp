#include "page_generator.h"
#include "common_styles.h"

// Generate standard HTML page with common template
String generatePage(const String& title, const String& content, const String& extraStyles, const String& script) {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='utf-8'>";
    html += "<title>" + title + "</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>" + String(COMMON_STYLES) + extraStyles + "</style>";
    if (script.length() > 0) {
        html += "<script>" + script + "</script>";
    }
    html += "</head><body>";
    html += content;
    html += "</body></html>";
    return html;
}
