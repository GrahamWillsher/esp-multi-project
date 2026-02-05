#ifndef INDEX_HTML_H
#define INDEX_HTML_H

// Common HTML header with minimal inline styles (detailed styles in COMMON_STYLES)
#define INDEX_HTML_HEADER \
  R"rawliteral(<!doctype html><html><head><meta charset="utf-8"><title>Battery Emulator</title><meta content="width=device-width"name=viewport>)rawliteral"

#define INDEX_HTML_FOOTER R"rawliteral(</body></html>)rawliteral"

// Centralized stylesheet for all pages - ensures consistent look and feel
#define COMMON_STYLES \
  R"rawliteral(<style>
html { font-family: Arial; display: inline-block; text-align: center; }
body { max-width: 800px; margin: 0 auto; background-color: #ADD8E6; color: white; }
h2 { font-size: 3rem; }
h4 { margin: 0.6em 0; line-height: 1.2; }
button { 
  background-color: #505E67; 
  color: white; 
  border: none; 
  padding: 10px 20px; 
  margin-bottom: 20px; 
  cursor: pointer; 
  border-radius: 10px; 
}
button:hover { background-color: #3A4A52; }
select, input { max-width: 250px; box-sizing: border-box; }
.tooltip { position: relative; display: inline-block; }
.tooltip .tooltiptext {
  visibility: hidden;
  width: 200px;
  background-color: #3A4A52;
  color: white;
  text-align: center;
  border-radius: 6px;
  padding: 8px;
  position: absolute;
  z-index: 1;
  margin-left: -100px;
  opacity: 0;
  transition: opacity 0.3s;
  font-size: 0.9em;
  font-weight: normal;
  line-height: 1.4;
}
.tooltip:hover .tooltiptext { visibility: visible; opacity: 1; }
.tooltip-icon { color: #505E67; cursor: help; }
</style></head><body>)rawliteral"

// Common JavaScript functions used across multiple pages
#define COMMON_JAVASCRIPT \
  R"rawliteral(<script>
function askReboot() {
  if (window.confirm('Are you sure you want to reboot the emulator? NOTE: If emulator is handling contactors, they will open during reboot!')) {
    reboot();
  }
}
function reboot() {
  var xhr = new XMLHttpRequest();
  xhr.open('GET', '/reboot', true);
  xhr.send();
  setTimeout(function() {
    window.location = "/";
  }, 3000);
}
</script>)rawliteral"

extern const char index_html[];
extern const char index_html_header[];
extern const char index_html_footer[];

#endif  // INDEX_HTML_H
