#include "webserver.h"
#include <Preferences.h>
#include <ctime>
#include <vector>
#include <cstring>
#include <string>
#include "../../battery/BATTERIES.h"
#include "../../battery/Battery.h"
#include "../../charger/CHARGERS.h"
#include "../../communication/can/comm_can.h"
#include "../../communication/contactorcontrol/comm_contactorcontrol.h"
#include "../../communication/equipmentstopbutton/comm_equipmentstopbutton.h"
#include "../../communication/nvm/comm_nvm.h"
#include "../../datalayer/datalayer.h"
#include "../../datalayer/datalayer_extended.h"
#include "../../inverter/INVERTERS.h"
#include "../../lib/bblanchon-ArduinoJson/ArduinoJson.h"
#include "../sdcard/sdcard.h"
#include "../utils/events.h"
#include "../utils/led_handler.h"
#include "../utils/timer.h"
#include "LittleFS.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "html_escape.h"
#include "settings_html.h"
#include "../hal/ethernet_compat.h"  // Compatibility layer for Ethernet
#include "../hal/hal.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_netif.h"

extern std::string http_username;
extern std::string http_password;
extern const char* version_number;

// ESP-IDF HTTP Server handle
httpd_handle_t server = NULL;

#include "advanced_battery_html.h"
#include "can_logging_html.h"
#include "can_replay_html.h"
#include "cellmonitor_html.h"
#include "debug_logging_html.h"
#include "events_html.h"
#include "index_html.h"
#include <Update.h>
#include <esp_ota_ops.h>
#include "../utils/events.h"

const char get_firmware_info_html[] = R"rawliteral(%X%)rawliteral";

String importedLogs = "";
bool isReplayRunning = false;
bool settingsUpdated = false;

CAN_frame currentFrame = {.FD = true, .ext_ID = false, .DLC = 64, .ID = 0x12F, .data = {0}};

// Helper function to send HTTP response with content
static esp_err_t send_html_response(httpd_req_t *req, const char *content, const char *content_type = "text/html") {
  httpd_resp_set_type(req, content_type);
  if (content) {
    httpd_resp_send(req, content, strlen(content));
  } else {
    httpd_resp_send(req, "", 0);
  }
  return ESP_OK;
}

// Helper function to check authentication
static bool check_auth(httpd_req_t *req) {
  // TODO: Implement proper HTTP basic auth if needed
  // For now, always allow access
  return true;
}

// Helper function to process templates (replace %X% with processor output)
static String process_template(const char* html_template, String (*processor)(const String&)) {
  String html(html_template);
  int start = html.indexOf("%X%");
  if (start != -1) {
    String processed_content = processor("X");
    html.replace("%X%", processed_content);
  }
  return html;
}

// Helper function to render a standard page with unified template
// All pages should use this to ensure consistent styling and structure
static String render_page(String (*content_processor)(const String&)) {
  return String(INDEX_HTML_HEADER) + 
         String(COMMON_STYLES) + 
         String(COMMON_JAVASCRIPT) + 
         content_processor("X") + 
         String(INDEX_HTML_FOOTER);
}

// Helper function to process settings template (replace all %VAR% placeholders)
static String process_settings_template(const char* html_template, BatteryEmulatorSettingsStore& settings) {
  String html(html_template);
  
  // Find and replace all %VAR% placeholders
  int start = 0;
  while ((start = html.indexOf('%', start)) != -1) {
    int end = html.indexOf('%', start + 1);
    if (end == -1) break;  // No closing %
    
    // Extract variable name between % %
    String var = html.substring(start + 1, end);
    
    // Get the replacement value from settings_processor
    String replacement = settings_processor(var, settings);
    
    // Replace %VAR% with the processed value
    String placeholder = "%" + var + "%";
    html.replace(placeholder, replacement);
    
    // Continue searching after the replacement
    start = 0;  // Start from beginning since replace() changes the string
  }
  
  return html;
}

// Handler for root page
static esp_err_t root_handler(httpd_req_t *req) {
  if (!check_auth(req)) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_sendstr(req, "Unauthorized");
    return ESP_OK;
  }
  String page = render_page(processor);
  return send_html_response(req, page.c_str());
}

// Handler for settings page
static esp_err_t settings_handler(httpd_req_t *req) {
  if (!check_auth(req)) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_sendstr(req, "Unauthorized");
    return ESP_OK;
  }
  
  // Load settings and process template
  BatteryEmulatorSettingsStore settings(true);  // Read-only mode
  String page = process_settings_template(settings_html, settings);
  return send_html_response(req, page.c_str());
}

// Handler for advanced battery page
static esp_err_t advanced_handler(httpd_req_t *req) {
  if (!check_auth(req)) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_sendstr(req, "Unauthorized");
    return ESP_OK;
  }
  String page = render_page(advanced_battery_processor);
  return send_html_response(req, page.c_str());
}

// Handler for cell monitor page
static esp_err_t cellmonitor_handler(httpd_req_t *req) {
  if (!check_auth(req)) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_sendstr(req, "Unauthorized");
    return ESP_OK;
  }
  String page = render_page(cellmonitor_processor);
  return send_html_response(req, page.c_str());
}

// Handler for events page
static esp_err_t events_handler(httpd_req_t *req) {
  if (!check_auth(req)) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_sendstr(req, "Unauthorized");
    return ESP_OK;
  }
  String page = render_page(events_processor);
  return send_html_response(req, page.c_str());
}

// Handler for firmware info (JSON endpoint)
static esp_err_t firmware_info_handler(httpd_req_t *req) {
  if (!check_auth(req)) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_sendstr(req, "Unauthorized");
    return ESP_OK;
  }
  
  // Build JSON firmware info
  JsonDocument doc;
  doc["firmware"] = "Battery Emulator";
  doc["version"] = version_number;
  
  // Add network interface info
  if (WiFi.status() == WL_CONNECTED) {
    doc["wifi_ip"] = WiFi.localIP().toString().c_str();
  }
  if (ethernetPresent && Ethernet.linkStatus() == LinkON) {
    doc["ethernet_ip"] = Ethernet.localIP().toString().c_str();
  }
  
  String json_response;
  serializeJson(doc, json_response);
  
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json_response.c_str(), json_response.length());
  return ESP_OK;
}

// Handler for status page (simple test endpoint)
static esp_err_t status_handler(httpd_req_t *req) {
  String response = "<!DOCTYPE html><html><head><title>Server Status</title>";
  response += String(COMMON_STYLES);  // Use unified styles
  response += "</head><body>";
  response += "<h1>Battery Emulator - Unified WebServer</h1>";
  response += "<p><strong>Server:</strong> ESP-IDF http_server (unified WiFi + Ethernet)</p>";
  
  response += "<p><strong>Network Interfaces:</strong><br>";
  if (WiFi.status() == WL_CONNECTED) {
    response += "WiFi: " + WiFi.localIP().toString() + "<br>";
  }
  if (ethernetPresent && Ethernet.linkStatus() == LinkON) {
    response += "Ethernet: " + Ethernet.localIP().toString() + "<br>";
  }
  response += "</p>";
  
  response += "<p><a href='/'>Main Page</a> | <a href='/settings'>Settings</a> | ";
  response += "<a href='/advanced'>Advanced</a> | <a href='/cellmonitor'>Cell Monitor</a> | ";
  response += "<a href='/events'>Events</a></p>";
  response += "</body></html>";
  
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, response.c_str(), response.length());
  return ESP_OK;
}

// OTA Update Page Handler
static esp_err_t update_handler(httpd_req_t *req) {
  const char* update_html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset='utf-8'>
  <title>Battery Emulator OTA Update</title>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <style>
    /* Base styles from COMMON_STYLES are applied inline here for standalone OTA page */
    html { font-family: Arial; display: inline-block; text-align: center; }
    body { max-width: 800px; margin: 0 auto; background-color: #ADD8E6; color: white; padding: 20px; }
    button { 
      background-color: #505E67; 
      color: white; 
      border: none; 
      padding: 12px 24px; 
      margin: 10px; 
      cursor: pointer; 
      border-radius: 10px; 
      font-size: 16px;
    }
    button:hover { background-color: #3A4A52; }
    /* OTA-specific styles */
    .container {
      background-color: #303E47;
      padding: 30px;
      border-radius: 20px;
      margin: 20px 0;
    }
    h1 { color: white; margin-bottom: 10px; }
    h3 { color: #FFD700; margin-top: 5px; }
    input[type='file'] {
      background-color: #505E67;
      color: white;
      border: none;
      padding: 12px 24px;
      margin: 10px;
      cursor: pointer;
      border-radius: 10px;
      font-size: 16px;
      display: inline-block;
    }
    #progress {
      width: 100%;
      height: 30px;
      background-color: #505E67;
      border-radius: 15px;
      margin: 20px 0;
      overflow: hidden;
    }
    #progressBar {
      height: 100%;
      background-color: #4CAF50;
      width: 0%;
      transition: width 0.3s;
      line-height: 30px;
      color: white;
      text-align: center;
    }
    .status {
      margin: 15px 0;
      font-size: 18px;
    }
    .warning {
      background-color: #FF6E00;
      padding: 15px;
      border-radius: 10px;
      margin: 15px 0;
    }
  </style>
</head>
<body>
  <div class='container'>
    <h1>Battery Emulator</h1>
    <h3>Over-The-Air Firmware Update</h3>
    <div class='warning'>
      <strong>⚠️ Warning:</strong> Do not power off the device during update!
    </div>
    <form id='uploadForm' enctype='multipart/form-data'>
      <input type='file' id='fileInput' name='update' accept='.bin' required>
      <br>
      <button type='submit'>Upload Firmware</button>
      <button type='button' onclick='window.location.href="/"'>Cancel</button>
    </form>
    <div id='progress' style='display:none;'>
      <div id='progressBar'>0%</div>
    </div>
    <div id='status' class='status'></div>
  </div>
  <script>
    const form = document.getElementById('uploadForm');
    const fileInput = document.getElementById('fileInput');
    const progress = document.getElementById('progress');
    const progressBar = document.getElementById('progressBar');
    const status = document.getElementById('status');

    form.addEventListener('submit', function(e) {
      e.preventDefault();
      
      if (!fileInput.files.length) {
        status.textContent = 'Please select a file';
        status.style.color = 'red';
        return;
      }

      const file = fileInput.files[0];
      if (!file.name.endsWith('.bin')) {
        status.textContent = 'Please select a .bin file';
        status.style.color = 'red';
        return;
      }

      const formData = new FormData();
      formData.append('update', file);

      const xhr = new XMLHttpRequest();
      
      xhr.upload.addEventListener('progress', function(e) {
        if (e.lengthComputable) {
          const percentComplete = Math.round((e.loaded / e.total) * 100);
          progress.style.display = 'block';
          progressBar.style.width = percentComplete + '%';
          progressBar.textContent = percentComplete + '%';
        }
      });

      xhr.addEventListener('load', function() {
        if (xhr.status === 200) {
          status.textContent = 'Upload successful! Device will reboot...';
          status.style.color = '#4CAF50';
          progressBar.style.width = '100%';
          progressBar.textContent = '100%';
          setTimeout(function() {
            window.location.href = '/';
          }, 5000);
        } else {
          status.textContent = 'Upload failed: ' + xhr.statusText;
          status.style.color = 'red';
        }
      });

      xhr.addEventListener('error', function() {
        status.textContent = 'Upload error occurred';
        status.style.color = 'red';
      });

      status.textContent = 'Uploading firmware...';
      status.style.color = 'white';
      xhr.open('POST', '/update');
      xhr.send(formData);
    });
  </script>
</body>
</html>
)rawliteral";

  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, update_html, strlen(update_html));
  return ESP_OK;
}

// OTA Update POST Handler
static esp_err_t update_post_handler(httpd_req_t *req) {
  char boundary[128];
  size_t boundary_len = 0;
  
  // Get content type to extract boundary
  size_t hdr_len = httpd_req_get_hdr_value_len(req, "Content-Type");
  if (hdr_len > 0 && hdr_len < sizeof(boundary)) {
    char content_type[256];
    httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type));
    
    // Extract boundary from content-type
    char* boundary_start = strstr(content_type, "boundary=");
    if (boundary_start) {
      boundary_start += 9; // Skip "boundary="
      snprintf(boundary, sizeof(boundary), "--%s", boundary_start);
      boundary_len = strlen(boundary);
    }
  }

  char buf[512];
  int received;
  size_t total_received = 0;
  bool update_started = false;
  bool in_file_data = false;
  int header_end_count = 0;

  logging.println("OTA Update starting...");

  // Read and process multipart data
  while ((received = httpd_req_recv(req, buf, sizeof(buf))) > 0) {
    for (int i = 0; i < received; i++) {
      // Look for end of headers (double CRLF)
      if (!in_file_data) {
        if (buf[i] == '\r' || buf[i] == '\n') {
          header_end_count++;
          if (header_end_count >= 4) {
            in_file_data = true;
            if (!update_started) {
              if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                logging.println("OTA Update.begin() failed");
                httpd_resp_set_status(req, "500 Internal Server Error");
                httpd_resp_sendstr(req, "Update begin failed");
                return ESP_FAIL;
              }
              update_started = true;
              logging.println("OTA Update.begin() successful");
            }
            continue;
          }
        } else {
          header_end_count = 0;
        }
      }
      
      // Write firmware data
      if (in_file_data && update_started) {
        // Check for boundary marker (end of file data)
        if (received - i > boundary_len && 
            memcmp(&buf[i], boundary, boundary_len) == 0) {
          break;
        }
        
        if (Update.write((uint8_t*)&buf[i], 1) != 1) {
          logging.println("OTA Update.write() failed");
          Update.abort();
          httpd_resp_set_status(req, "500 Internal Server Error");
          httpd_resp_sendstr(req, "Write failed");
          return ESP_FAIL;
        }
        total_received++;
      }
    }
  }

  if (received < 0) {
    logging.println("OTA receive error");
    if (update_started) {
      Update.abort();
    }
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_sendstr(req, "Receive failed");
    return ESP_FAIL;
  }

  if (update_started) {
    if (Update.end(true)) {
      logging.printf("OTA Update successful! %d bytes written\n", total_received);
      httpd_resp_set_type(req, "text/plain");
      httpd_resp_sendstr(req, "Update successful! Rebooting...");
      
      // Reboot after a short delay
      delay(1000);
      ESP.restart();
      return ESP_OK;
    } else {
      logging.printf("OTA Update.end() failed. Error: %s\n", Update.errorString());
      Update.abort();
      httpd_resp_set_status(req, "500 Internal Server Error");
      httpd_resp_sendstr(req, Update.errorString());
      return ESP_FAIL;
    }
  }

  httpd_resp_set_status(req, "400 Bad Request");
  httpd_resp_sendstr(req, "No firmware data received");
  return ESP_FAIL;
}

// Clear events handler
static esp_err_t clearevents_handler(httpd_req_t *req) {
  logging.println("Clearing all events...");
  reset_all_events();
  
  // Redirect back to events page
  httpd_resp_set_status(req, "303 See Other");
  httpd_resp_set_hdr(req, "Location", "/events");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

// Reboot handler
static esp_err_t reboot_handler(httpd_req_t *req) {
  logging.println("Reboot requested via web interface");
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_sendstr(req, "Rebooting...");
  
  delay(1000);
  ESP.restart();
  return ESP_OK;
}

// Pause/Resume battery handler
static esp_err_t pause_handler(httpd_req_t *req) {
  char buf[64];
  int ret = httpd_req_get_url_query_str(req, buf, sizeof(buf));
  
  if (ret == ESP_OK) {
    char param[32];
    if (httpd_query_key_value(buf, "value", param, sizeof(param)) == ESP_OK) {
      bool pause = (strcmp(param, "true") == 0 || strcmp(param, "1") == 0);
      datalayer.battery.settings.max_user_set_charge_dA = pause ? 0 : 3000;  // 0A or 300A
      datalayer.battery.settings.max_user_set_discharge_dA = pause ? 0 : 3000;
      emulator_pause_request_ON = pause;
      
      logging.printf("Battery pause %s\n", pause ? "enabled" : "disabled");
    }
  }
  
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_sendstr(req, "OK");
  return ESP_OK;
}

// Equipment stop handler
static esp_err_t equipment_stop_handler(httpd_req_t *req) {
  char buf[64];
  int ret = httpd_req_get_url_query_str(req, buf, sizeof(buf));
  
  if (ret == ESP_OK) {
    char param[32];
    if (httpd_query_key_value(buf, "value", param, sizeof(param)) == ESP_OK) {
      bool stop = (strcmp(param, "true") == 0 || strcmp(param, "1") == 0);
      
      if (stop) {
        datalayer.system.info.equipment_stop_active = true;
        datalayer.battery.settings.max_user_set_charge_dA = 0;
        datalayer.battery.settings.max_user_set_discharge_dA = 0;
      } else {
        datalayer.system.info.equipment_stop_active = false;
        datalayer.battery.settings.max_user_set_charge_dA = 3000;
        datalayer.battery.settings.max_user_set_discharge_dA = 3000;
      }
      
      logging.printf("Equipment stop %s\n", stop ? "activated" : "deactivated");
    }
  }
  
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_sendstr(req, "OK");
  return ESP_OK;
}

// Logout handler
static esp_err_t logout_handler(httpd_req_t *req) {
  httpd_resp_set_status(req, "401 Unauthorized");
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_sendstr(req, "Logged out");
  return ESP_OK;
}

// CAN log page handler
static esp_err_t canlog_handler(httpd_req_t *req) {
  String page = render_page(can_logger_processor);
  return send_html_response(req, page.c_str());
}

// CAN replay page handler
static esp_err_t canreplay_handler(httpd_req_t *req) {
  String page = render_page(can_replay_processor);
  return send_html_response(req, page.c_str());
}

// Debug log page handler
static esp_err_t log_handler(httpd_req_t *req) {
  if (datalayer.system.info.web_logging_active || datalayer.system.info.SD_logging_active) {
    String page = render_page(debug_logger_processor);
    return send_html_response(req, page.c_str());
  } else {
    httpd_resp_set_status(req, "404 Not Found");
    httpd_resp_sendstr(req, "Logging not enabled");
  }
  return ESP_OK;
}

// Export log handler (placeholder - needs SD card implementation)
static esp_err_t export_log_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"log.txt\"");
  
  if (datalayer.system.info.SD_logging_active) {
    // TODO: Send actual SD card log file
    httpd_resp_sendstr(req, "SD card logging - export not yet implemented in ESP-IDF webserver");
  } else {
    String logs = "No logs available - logging not active";
    httpd_resp_send(req, logs.c_str(), logs.length());
  }
  return ESP_OK;
}

// Delete log handler
static esp_err_t delete_log_handler(httpd_req_t *req) {
  if (datalayer.system.info.SD_logging_active) {
    // TODO: Implement SD card log deletion
    logging.println("Log deletion requested - not yet implemented");
  }
  
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_sendstr(req, "Log file deleted");
  return ESP_OK;
}

// Export CAN log handler
static esp_err_t export_can_log_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"canlog.txt\"");
  
  if (datalayer.system.info.CAN_SD_logging_active) {
    // TODO: Send actual SD card CAN log file
    httpd_resp_sendstr(req, "CAN SD logging - export not yet implemented in ESP-IDF webserver");
  } else {
    String logs = datalayer.system.info.logged_can_messages;
    if (logs.length() == 0) {
      logs = "No CAN logs available";
    }
    httpd_resp_send(req, logs.c_str(), logs.length());
  }
  return ESP_OK;
}

// Delete CAN log handler
static esp_err_t delete_can_log_handler(httpd_req_t *req) {
  if (datalayer.system.info.CAN_SD_logging_active) {
    // TODO: Implement SD card CAN log deletion
    logging.println("CAN log deletion requested - not yet implemented");
  }
  
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_sendstr(req, "CAN log file deleted");
  return ESP_OK;
}

// Stop CAN logging handler
static esp_err_t stop_can_logging_handler(httpd_req_t *req) {
  datalayer.system.info.can_logging_active = false;
  logging.println("CAN logging stopped via web interface");
  
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_sendstr(req, "Logging stopped");
  return ESP_OK;
}

// Helper function to URL decode a string
static String url_decode(String str) {
  String decoded = "";
  char temp[] = "0x00";
  unsigned int len = str.length();
  
  for (unsigned int i = 0; i < len; i++) {
    char c = str.charAt(i);
    if (c == '+') {
      decoded += ' ';
    } else if (c == '%') {
      temp[2] = str.charAt(++i);
      temp[3] = str.charAt(++i);
      decoded += (char)strtol(temp, NULL, 16);
    } else {
      decoded += c;
    }
  }
  
  return decoded;
}

// Helper function to parse form data and extract parameter value
static bool get_post_param(const char* data, size_t len, const char* param_name, char* value, size_t value_len) {
  // Find parameter in form data (format: name=value&name2=value2)
  String form_data(data, len);
  String param_pattern = String(param_name) + "=";
  int param_start = form_data.indexOf(param_pattern);
  
  if (param_start == -1) {
    return false;
  }
  
  param_start += param_pattern.length(); // Skip past "name="
  int param_end = form_data.indexOf('&', param_start);
  
  if (param_end == -1) {
    param_end = len; // Last parameter
  }
  
  String param_value = form_data.substring(param_start, param_end);
  
  // URL decode the value
  param_value = url_decode(param_value);
  
  strncpy(value, param_value.c_str(), value_len - 1);
  value[value_len - 1] = '\0';
  
  return true;
}

// Save settings POST handler
static esp_err_t save_settings_handler(httpd_req_t *req) {
  // Log the content length
  logging.printf("SaveSettings: Receiving POST with content_len=%d\n", req->content_len);
  
  if (req->content_len == 0) {
    logging.println("SaveSettings: No content received!");
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "No data received");
    return ESP_FAIL;
  }
  
  char* buf = (char*)malloc(req->content_len + 1);
  if (!buf) {
    logging.println("SaveSettings: Out of memory!");
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_sendstr(req, "Out of memory");
    return ESP_FAIL;
  }
  
  int ret = httpd_req_recv(req, buf, req->content_len);
  if (ret <= 0) {
    free(buf);
    logging.printf("SaveSettings: httpd_req_recv failed with ret=%d\n", ret);
    if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
      httpd_resp_send_408(req);
    }
    return ESP_FAIL;
  }
  
  buf[ret] = '\0';
  
  logging.printf("SaveSettings: Received %d bytes of form data\n", ret);
  logging.printf("SaveSettings: First 200 chars: %.200s\n", buf);
  
  // Create settings store
  BatteryEmulatorSettingsStore settings;
  
  char value[256];
  
  // Network settings
  if (get_post_param(buf, ret, "SSID", value, sizeof(value))) {
    settings.saveString("SSID", value);
  }
  if (get_post_param(buf, ret, "PASSWORD", value, sizeof(value))) {
    settings.saveString("PASSWORD", value);
  }
  if (get_post_param(buf, ret, "HOSTNAME", value, sizeof(value))) {
    settings.saveString("HOSTNAME", value);
  }
  if (get_post_param(buf, ret, "APNAME", value, sizeof(value))) {
    settings.saveString("APNAME", value);
  }
  if (get_post_param(buf, ret, "APPASSWORD", value, sizeof(value))) {
    settings.saveString("APPASSWORD", value);
  }
  
  // Battery type
  if (get_post_param(buf, ret, "battery", value, sizeof(value))) {
    settings.saveUInt("BATTTYPE", atoi(value));
  }
  
  // Inverter type
  if (get_post_param(buf, ret, "inverter", value, sizeof(value))) {
    settings.saveUInt("INVTYPE", atoi(value));
  }
  
  // Charger type
  if (get_post_param(buf, ret, "charger", value, sizeof(value))) {
    settings.saveUInt("CHGTYPE", atoi(value));
  }
  
  // Communication interfaces
  if (get_post_param(buf, ret, "BATTCOMM", value, sizeof(value))) {
    settings.saveUInt("BATTCOMM", atoi(value));
  }
  if (get_post_param(buf, ret, "INVCOMM", value, sizeof(value))) {
    settings.saveUInt("INVCOMM", atoi(value));
  }
  if (get_post_param(buf, ret, "CHGCOMM", value, sizeof(value))) {
    settings.saveUInt("CHGCOMM", atoi(value));
  }
  
  // MQTT settings
  if (get_post_param(buf, ret, "MQTTSERVER", value, sizeof(value))) {
    settings.saveString("MQTTSERVER", value);
  }
  if (get_post_param(buf, ret, "MQTTPORT", value, sizeof(value))) {
    settings.saveUInt("MQTTPORT", atoi(value));
  }
  if (get_post_param(buf, ret, "MQTTUSER", value, sizeof(value))) {
    settings.saveString("MQTTUSER", value);
  }
  if (get_post_param(buf, ret, "MQTTPASSWORD", value, sizeof(value))) {
    settings.saveString("MQTTPASSWORD", value);
  }
  if (get_post_param(buf, ret, "MQTTTOPIC", value, sizeof(value))) {
    settings.saveString("MQTTTOPIC", value);
  }
  
  // Boolean settings - check if checkbox parameters exist in form data
  String form_data(buf, ret);
  
  const char* bool_settings[] = {
    "DBLBTR", "CNTCTRL", "CNTCTRLDBL", "PWMCNTCTRL", "PERBMSRESET", "SDLOGENABLED", 
    "STATICIP", "REMBMSRESET", "EXTPRECHARGE", "USBENABLED", "CANLOGUSB", "WEBENABLED",
    "CANFDASCAN", "CANLOGSD", "WIFIAPENABLED", "MQTTENABLED", "NOINVDISC", "HADISC",
    "MQTTTOPICS", "MQTTCELLV", "INVICNT", "GTWRHD", "DIGITALHVIL", "PERFPROFILE",
    "INTERLOCKREQ", "SOCESTIMATED", "PYLONOFFSET", "PYLONORDER", "DEYEBYD", 
    "NCCONTACTOR", "TRIBTR", "CNTCTRLTRI"
  };
  
  for (const char* setting : bool_settings) {
    String param_pattern = String(setting) + "=on";
    bool is_checked = (form_data.indexOf(param_pattern) != -1);
    settings.saveBool(setting, is_checked);
  }
  
  // Numeric settings
  if (get_post_param(buf, ret, "LOCALIP1", value, sizeof(value))) {
    settings.saveUInt("LOCALIP1", atoi(value));
  }
  if (get_post_param(buf, ret, "LOCALIP2", value, sizeof(value))) {
    settings.saveUInt("LOCALIP2", atoi(value));
  }
  if (get_post_param(buf, ret, "LOCALIP3", value, sizeof(value))) {
    settings.saveUInt("LOCALIP3", atoi(value));
  }
  if (get_post_param(buf, ret, "LOCALIP4", value, sizeof(value))) {
    settings.saveUInt("LOCALIP4", atoi(value));
  }
  if (get_post_param(buf, ret, "GATEWAY1", value, sizeof(value))) {
    settings.saveUInt("GATEWAY1", atoi(value));
  }
  if (get_post_param(buf, ret, "GATEWAY2", value, sizeof(value))) {
    settings.saveUInt("GATEWAY2", atoi(value));
  }
  if (get_post_param(buf, ret, "GATEWAY3", value, sizeof(value))) {
    settings.saveUInt("GATEWAY3", atoi(value));
  }
  if (get_post_param(buf, ret, "GATEWAY4", value, sizeof(value))) {
    settings.saveUInt("GATEWAY4", atoi(value));
  }
  if (get_post_param(buf, ret, "SUBNET1", value, sizeof(value))) {
    settings.saveUInt("SUBNET1", atoi(value));
  }
  if (get_post_param(buf, ret, "SUBNET2", value, sizeof(value))) {
    settings.saveUInt("SUBNET2", atoi(value));
  }
  if (get_post_param(buf, ret, "SUBNET3", value, sizeof(value))) {
    settings.saveUInt("SUBNET3", atoi(value));
  }
  if (get_post_param(buf, ret, "SUBNET4", value, sizeof(value))) {
    settings.saveUInt("SUBNET4", atoi(value));
  }
  
  // Tesla-specific settings
  if (get_post_param(buf, ret, "GTWCOUNTRY", value, sizeof(value))) {
    settings.saveUInt("GTWCOUNTRY", atoi(value));
  }
  if (get_post_param(buf, ret, "GTWMAPREG", value, sizeof(value))) {
    settings.saveUInt("GTWMAPREG", atoi(value));
  }
  if (get_post_param(buf, ret, "GTWCHASSIS", value, sizeof(value))) {
    settings.saveUInt("GTWCHASSIS", atoi(value));
  }
  if (get_post_param(buf, ret, "GTWPACK", value, sizeof(value))) {
    settings.saveUInt("GTWPACK", atoi(value));
  }
  
  // Additional numeric settings
  if (get_post_param(buf, ret, "CHGPOWER", value, sizeof(value))) {
    settings.saveUInt("CHGPOWER", atoi(value));
  }
  if (get_post_param(buf, ret, "DCHGPOWER", value, sizeof(value))) {
    settings.saveUInt("DCHGPOWER", atoi(value));
  }
  if (get_post_param(buf, ret, "BATTCHEM", value, sizeof(value))) {
    settings.saveUInt("BATTCHEM", atoi(value));
  }
  if (get_post_param(buf, ret, "BATTPVMAX", value, sizeof(value))) {
    settings.saveString("BATTPVMAX", value);
  }
  if (get_post_param(buf, ret, "BATTPVMIN", value, sizeof(value))) {
    settings.saveString("BATTPVMIN", value);
  }
  if (get_post_param(buf, ret, "BATTCVMAX", value, sizeof(value))) {
    settings.saveUInt("BATTCVMAX", atoi(value));
  }
  if (get_post_param(buf, ret, "BATTCVMIN", value, sizeof(value))) {
    settings.saveUInt("BATTCVMIN", atoi(value));
  }
  if (get_post_param(buf, ret, "BATT2COMM", value, sizeof(value))) {
    settings.saveUInt("BATT2COMM", atoi(value));
  }
  if (get_post_param(buf, ret, "SOFAR_ID", value, sizeof(value))) {
    settings.saveString("SOFAR_ID", value);
  }
  if (get_post_param(buf, ret, "PYLONSEND", value, sizeof(value))) {
    settings.saveUInt("PYLONSEND", atoi(value));
  }
  if (get_post_param(buf, ret, "INVCELLS", value, sizeof(value))) {
    settings.saveUInt("INVCELLS", atoi(value));
  }
  if (get_post_param(buf, ret, "INVMODULES", value, sizeof(value))) {
    settings.saveUInt("INVMODULES", atoi(value));
  }
  if (get_post_param(buf, ret, "INVCELLSPER", value, sizeof(value))) {
    settings.saveUInt("INVCELLSPER", atoi(value));
  }
  if (get_post_param(buf, ret, "INVVLEVEL", value, sizeof(value))) {
    settings.saveUInt("INVVLEVEL", atoi(value));
  }
  if (get_post_param(buf, ret, "INVCAPACITY", value, sizeof(value))) {
    settings.saveUInt("INVCAPACITY", atoi(value));
  }
  if (get_post_param(buf, ret, "INVBTYPE", value, sizeof(value))) {
    settings.saveUInt("INVBTYPE", atoi(value));
  }
  if (get_post_param(buf, ret, "SHUNT", value, sizeof(value))) {
    settings.saveUInt("SHUNT", atoi(value));
  }
  if (get_post_param(buf, ret, "SHUNTCOMM", value, sizeof(value))) {
    settings.saveUInt("SHUNTCOMM", atoi(value));
  }
  if (get_post_param(buf, ret, "CANFREQ", value, sizeof(value))) {
    settings.saveUInt("CANFREQ", atoi(value));
  }
  if (get_post_param(buf, ret, "CANFDFREQ", value, sizeof(value))) {
    settings.saveUInt("CANFDFREQ", atoi(value));
  }
  if (get_post_param(buf, ret, "EQSTOP", value, sizeof(value))) {
    settings.saveUInt("EQSTOP", atoi(value));
  }
  if (get_post_param(buf, ret, "PRECHGMS", value, sizeof(value))) {
    settings.saveUInt("PRECHGMS", atoi(value));
  }
  if (get_post_param(buf, ret, "PWMFREQ", value, sizeof(value))) {
    settings.saveString("PWMFREQ", value);
  }
  if (get_post_param(buf, ret, "PWMHOLD", value, sizeof(value))) {
    settings.saveUInt("PWMHOLD", atoi(value));
  }
  if (get_post_param(buf, ret, "MAXPRETIME", value, sizeof(value))) {
    settings.saveUInt("MAXPRETIME", atoi(value));
  }
  if (get_post_param(buf, ret, "WIFICHANNEL", value, sizeof(value))) {
    settings.saveUInt("WIFICHANNEL", atoi(value));
  }
  if (get_post_param(buf, ret, "MQTTTIMEOUT", value, sizeof(value))) {
    settings.saveUInt("MQTTTIMEOUT", atoi(value));
  }
  if (get_post_param(buf, ret, "MQTTOBJIDPREFIX", value, sizeof(value))) {
    settings.saveString("MQTTOBJIDPREFIX", value);
  }
  if (get_post_param(buf, ret, "MQTTDEVICENAME", value, sizeof(value))) {
    settings.saveString("MQTTDEVICENAME", value);
  }
  if (get_post_param(buf, ret, "HADEVICEID", value, sizeof(value))) {
    settings.saveString("HADEVICEID", value);
  }
  if (get_post_param(buf, ret, "GPIOOPT1", value, sizeof(value))) {
    settings.saveUInt("GPIOOPT1", atoi(value));
  }
  
  // LED mode setting
  if (get_post_param(buf, ret, "LEDMODE", value, sizeof(value))) {
    settings.saveUInt("LEDMODE", atoi(value));
  }
  
  free(buf);
  
  settingsUpdated = true;
  logging.println("Settings saved successfully");
  
  // Redirect back to settings page
  httpd_resp_set_status(req, "303 See Other");
  httpd_resp_set_hdr(req, "Location", "/settings");
  httpd_resp_send(req, NULL, 0);
  
  return ESP_OK;
}

// Handler for 404 Not Found
static esp_err_t notfound_handler(httpd_req_t *req) {
  httpd_resp_set_status(req, "404 Not Found");
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_sendstr(req, "Endpoint not found");
  return ESP_OK;
}

void init_webserver() {
  Serial.printf("[%lu ms] [WEBSERVER] init_webserver() ENTRY\n", millis());
  Serial.flush();
  
  // Check if server is already running
  if (server != NULL) {
    Serial.printf("[%lu ms] [WEBSERVER] WARNING: Server already running at %p, skipping\n", millis(), server);
    Serial.flush();
    return;
  }

  Serial.printf("[%lu ms] [WEBSERVER] === Starting HTTP Server Initialization ===\n", millis());
  Serial.printf("[%lu ms] [WEBSERVER] Server pointer: %p (NULL is expected before start)\n", millis(), server);
  Serial.flush();

  // CRITICAL: Ensure lwIP TCP/IP stack is initialized
  // ESP-IDF's httpd requires lwIP even when using hardware-based W5500 Ethernet
  // WiFi initialization normally handles this, but if WiFi is disabled we must do it manually
  static bool lwip_initialized = false;
  if (!lwip_initialized) {
    Serial.printf("[%lu ms] [WEBSERVER] Checking lwIP TCP/IP stack initialization...\n", millis());
    Serial.flush();
    
    // Initialize ESP network interface layer (esp_netif)
    // This internally calls tcpip_init() which starts the lwIP TCP/IP task
    // Required for ESP-IDF httpd socket operations
    Serial.printf("[%lu ms] [WEBSERVER] *** ABOUT TO CALL esp_netif_init() ***\n", millis());
    Serial.flush();
    esp_err_t ret = esp_netif_init();
    Serial.printf("[%lu ms] [WEBSERVER] *** esp_netif_init() RETURNED: %s ***\n", millis(), esp_err_to_name(ret));
    Serial.flush();
    
    if (ret == ESP_OK) {
      Serial.printf("[%lu ms] [WEBSERVER] esp_netif initialized successfully\n", millis());
    } else if (ret == ESP_ERR_INVALID_STATE) {
      Serial.printf("[%lu ms] [WEBSERVER] esp_netif already initialized (OK)\n", millis());
    } else {
      Serial.printf("[%lu ms] [WEBSERVER] WARNING: esp_netif init returned: %s\n", millis(), esp_err_to_name(ret));
    }
    
    lwip_initialized = true;
    Serial.printf("[%lu ms] [WEBSERVER] lwIP initialization complete\n", millis());
    Serial.flush();
  } else {
    Serial.printf("[%lu ms] [WEBSERVER] lwIP already initialized (skipping)\n", millis());
    Serial.flush();
  }

  // Initialize LittleFS if needed
  static bool littlefs_initialized = false;
  if (!littlefs_initialized) {
    Serial.printf("[%lu ms] [WEBSERVER] Mounting LittleFS...\n", millis());
    Serial.flush();
    littlefs_initialized = LittleFS.begin();
    Serial.printf("[%lu ms] [WEBSERVER] LittleFS: %s\n", millis(), littlefs_initialized ? "mounted OK" : "FAILED");
    Serial.flush();
  }

  // Check network interface availability
  // Note: Start webserver if hardware is present, even if link isn't established yet
  // This ensures the server is ready when the link comes up
  bool wifi_available = WiFi.status() == WL_CONNECTED || WiFi.getMode() == WIFI_AP;
  bool ethernet_available = ethernetPresent;  // Check hardware presence, not link status
  
  Serial.printf("[WEBSERVER] Network - WiFi: %s, Ethernet: %s\n",
                 wifi_available ? "available" : "not available",
                 ethernet_available ? "present" : "not present");
  Serial.flush();
  
  if (wifi_available) {
    Serial.printf("[WEBSERVER]   WiFi mode: %d, Status: %d\n", WiFi.getMode(), WiFi.status());
    Serial.flush();
  }
  if (ethernet_available) {
    Serial.printf("[WEBSERVER]   Ethernet link: %s\n", 
                   Ethernet.linkStatus() == LinkON ? "UP" : "DOWN (will connect)");
    Serial.flush();
  }

  if (!wifi_available && !ethernet_available) {
    Serial.println("[WEBSERVER] ERROR: No network interfaces - aborting");
    Serial.flush();
    return;
  }

  // Configure HTTP server
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.task_priority = tskIDLE_PRIORITY + 3;  // Run at elevated priority for responsiveness
  config.stack_size = 8192;                      // Increase stack for complex handlers
  config.max_open_sockets = 7;                   // Increase to support multiple Ethernet clients
  config.max_uri_handlers = 32;                  // Support up to 32 URI handlers (currently using 25)
  config.uri_match_fn = httpd_uri_match_wildcard;
  config.server_port = 80;                       // Standard HTTP port
  config.ctrl_port = 32768;                      // Control port for internal communication
  config.lru_purge_enable = true;                // Enable LRU purging of old connections
  
  // CRITICAL: Explicitly bind to all interfaces (INADDR_ANY = 0.0.0.0)
  // This ensures the server accepts connections from both WiFi (192.168.4.1) and Ethernet (192.168.1.246)
  // Without this, ESP-IDF httpd only binds to the first available interface (WiFi AP)
  config.core_id = tskNO_AFFINITY;               // Allow server to run on any CPU core

  Serial.printf("[WEBSERVER] Configuring server: port=%d, max_sockets=%d, stack=%d\n", 
                config.server_port, config.max_open_sockets, config.stack_size);
  Serial.flush();

  // CRITICAL FIX for Ethernet accessibility:
  // ESP-IDF HTTP server binds to 0.0.0.0 by default, but we need to ensure
  // the server starts AFTER both network interfaces (WiFi AP + Ethernet) are ready
  // Otherwise it may only listen on the first interface that was up
  if (ethernet_available) {
    Serial.printf("[WEBSERVER] Ethernet present - ensuring interface is ready\n");
    Serial.printf("[WEBSERVER]   Ethernet IP: %s\n", Ethernet.localIP().toString().c_str());
    Serial.printf("[WEBSERVER]   WiFi AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    Serial.flush();
    // Brief delay to ensure both network stacks are fully initialized
    delay(100);
  }

  // Start HTTP server
  Serial.printf("[%lu ms] [WEBSERVER] *** CALLING httpd_start() ***\n", millis());
  Serial.flush();
  esp_err_t ret = httpd_start(&server, &config);
  Serial.printf("[%lu ms] [WEBSERVER] *** httpd_start() RETURNED: %s ***\n", millis(), esp_err_to_name(ret));
  Serial.flush();
  
  if (ret != ESP_OK) {
    Serial.printf("[%lu ms] [WEBSERVER] FAILED to start: %s\n", millis(), esp_err_to_name(ret));
    Serial.flush();
    return;
  }

  // Brief yield to allow W5500/Ethernet tasks to catch up after intensive httpd_start()
  // httpd_start() creates tasks and initializes sockets which can take 100ms+
  // During this time, incoming Ethernet packets may accumulate in W5500 receive buffer
  vTaskDelay(pdMS_TO_TICKS(50));  // Allow Ethernet connectivity task to process any queued packets

  Serial.printf("[%lu ms] [WEBSERVER] SUCCESS! Server started at %p\n", millis(), server);
  
  // CRITICAL: Verify the server is listening on all interfaces
  // ESP-IDF httpd should bind to 0.0.0.0:80 by default, making it accessible from all IPs
  Serial.println("[WEBSERVER] Server should now be accessible on ALL network interfaces:");
  if (wifi_available) {
    Serial.printf("[WEBSERVER]   - WiFi AP: http://%s\n", WiFi.softAPIP().toString().c_str());
  }
  if (ethernet_available && Ethernet.linkStatus() == LinkON) {
    Serial.printf("[WEBSERVER]   - Ethernet: http://%s\n", Ethernet.localIP().toString().c_str());
  }
  Serial.flush();
  
  // Log which IP addresses the server is accessible from
  if (wifi_available) {
    #ifdef HW_LILYGO_T_CONNECT_PRO
    if (wifi_enabled) {
      if (static_IP_enabled) {
        logging.printf("  WiFi Static IP: %d.%d.%d.%d\n", 
                      static_local_IP1, static_local_IP2, static_local_IP3, static_local_IP4);
      }
      logging.printf("  WiFi AP IP: 192.168.4.1\n");
    }
    #endif
  }
  if (ethernet_available) {
    #ifdef HW_LILYGO_T_CONNECT_PRO
    extern volatile bool ethernet_connected;
    if (ethernet_connected && static_IP_enabled) {
      uint8_t ethernet_ip4 = static_local_IP4 + 1;
      if (ethernet_ip4 > 255) ethernet_ip4 = 254;
      logging.printf("  Ethernet IP: %d.%d.%d.%d\n", 
                    static_local_IP1, static_local_IP2, static_local_IP3, ethernet_ip4);
    }
    #endif
  }

  // Register URI handlers
  httpd_uri_t root = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = root_handler,
      .user_ctx = NULL};
  httpd_register_uri_handler(server, &root);

  httpd_uri_t settings = {
      .uri = "/settings",
      .method = HTTP_GET,
      .handler = settings_handler,
      .user_ctx = NULL};
  httpd_register_uri_handler(server, &settings);

  httpd_uri_t advanced = {
      .uri = "/advanced",
      .method = HTTP_GET,
      .handler = advanced_handler,
      .user_ctx = NULL};
  httpd_register_uri_handler(server, &advanced);

  httpd_uri_t cellmonitor = {
      .uri = "/cellmonitor",
      .method = HTTP_GET,
      .handler = cellmonitor_handler,
      .user_ctx = NULL};
  httpd_register_uri_handler(server, &cellmonitor);

  httpd_uri_t events = {
      .uri = "/events",
      .method = HTTP_GET,
      .handler = events_handler,
      .user_ctx = NULL};
  httpd_register_uri_handler(server, &events);

  httpd_uri_t firmware_info = {
      .uri = "/GetFirmwareInfo",
      .method = HTTP_GET,
      .handler = firmware_info_handler,
      .user_ctx = NULL};
  httpd_register_uri_handler(server, &firmware_info);

  httpd_uri_t status = {
      .uri = "/status",
      .method = HTTP_GET,
      .handler = status_handler,
      .user_ctx = NULL};
  httpd_register_uri_handler(server, &status);

  httpd_uri_t update_get = {
      .uri = "/update",
      .method = HTTP_GET,
      .handler = update_handler,
      .user_ctx = NULL};
  httpd_register_uri_handler(server, &update_get);

  httpd_uri_t update_post = {
      .uri = "/update",
      .method = HTTP_POST,
      .handler = update_post_handler,
      .user_ctx = NULL};
  httpd_register_uri_handler(server, &update_post);

  httpd_uri_t clearevents = {
      .uri = "/clearevents",
      .method = HTTP_GET,
      .handler = clearevents_handler,
      .user_ctx = NULL};
  httpd_register_uri_handler(server, &clearevents);

  httpd_uri_t reboot = {
      .uri = "/reboot",
      .method = HTTP_GET,
      .handler = reboot_handler,
      .user_ctx = NULL};
  httpd_register_uri_handler(server, &reboot);

  httpd_uri_t pause = {
      .uri = "/pause",
      .method = HTTP_GET,
      .handler = pause_handler,
      .user_ctx = NULL};
  httpd_register_uri_handler(server, &pause);

  httpd_uri_t equipment_stop = {
      .uri = "/equipmentStop",
      .method = HTTP_GET,
      .handler = equipment_stop_handler,
      .user_ctx = NULL};
  httpd_register_uri_handler(server, &equipment_stop);

  httpd_uri_t logout = {
      .uri = "/logout",
      .method = HTTP_GET,
      .handler = logout_handler,
      .user_ctx = NULL};
  httpd_register_uri_handler(server, &logout);

  httpd_uri_t canlog = {
      .uri = "/canlog",
      .method = HTTP_GET,
      .handler = canlog_handler,
      .user_ctx = NULL};
  httpd_register_uri_handler(server, &canlog);

  httpd_uri_t canreplay = {
      .uri = "/canreplay",
      .method = HTTP_GET,
      .handler = canreplay_handler,
      .user_ctx = NULL};
  httpd_register_uri_handler(server, &canreplay);

  httpd_uri_t log = {
      .uri = "/log",
      .method = HTTP_GET,
      .handler = log_handler,
      .user_ctx = NULL};
  httpd_register_uri_handler(server, &log);

  httpd_uri_t export_log = {
      .uri = "/export_log",
      .method = HTTP_GET,
      .handler = export_log_handler,
      .user_ctx = NULL};
  httpd_register_uri_handler(server, &export_log);

  httpd_uri_t delete_log = {
      .uri = "/delete_log",
      .method = HTTP_GET,
      .handler = delete_log_handler,
      .user_ctx = NULL};
  httpd_register_uri_handler(server, &delete_log);

  httpd_uri_t export_can_log = {
      .uri = "/export_can_log",
      .method = HTTP_GET,
      .handler = export_can_log_handler,
      .user_ctx = NULL};
  httpd_register_uri_handler(server, &export_can_log);

  httpd_uri_t delete_can_log = {
      .uri = "/delete_can_log",
      .method = HTTP_GET,
      .handler = delete_can_log_handler,
      .user_ctx = NULL};
  httpd_register_uri_handler(server, &delete_can_log);

  httpd_uri_t stop_can_logging = {
      .uri = "/stop_can_logging",
      .method = HTTP_GET,
      .handler = stop_can_logging_handler,
      .user_ctx = NULL};
  httpd_register_uri_handler(server, &stop_can_logging);

  httpd_uri_t save_settings = {
      .uri = "/saveSettings",
      .method = HTTP_POST,
      .handler = save_settings_handler,
      .user_ctx = NULL};
  httpd_register_uri_handler(server, &save_settings);

  httpd_uri_t notfound_get = {
      .uri = "/*",
      .method = HTTP_GET,
      .handler = notfound_handler,
      .user_ctx = NULL};
  httpd_register_uri_handler(server, &notfound_get);

  httpd_uri_t notfound_post = {
      .uri = "/*",
      .method = HTTP_POST,
      .handler = notfound_handler,
      .user_ctx = NULL};
  httpd_register_uri_handler(server, &notfound_post);

  logging.println("Webserver initialization complete");
}

// Template implementations must come before index_processor.cpp include
template <typename T>
String formatPowerValue(String label, T value, String unit, int precision, String color) {
  String result = "<h4 style='color: " + color + ";'>" + label + ": ";
  float fvalue = static_cast<float>(value);
  if (fvalue >= 1000.0f) {
    result += String(fvalue / 1000.0f, precision) + " k" + unit;
  } else {
    result += String(fvalue, precision) + " " + unit;
  }
  result += "</h4>";
  return result;
}

template <typename T>
String formatPowerValue(T value, String unit, int precision) {
  float fvalue = static_cast<float>(value);
  if (fvalue >= 1000.0f) {
    return String(fvalue / 1000.0f, precision) + " k" + unit;
  } else {
    return String(fvalue, precision) + " " + unit;
  }
}

// Main page processor - generates the dashboard HTML
#include "index_processor.inc"

String optimised_processor(const String& var) {
  return var;
}

String optimised_advanced_battery_processor(const String& var) {
  return var;
}

String optimised_cellmonitor_processor(const String& var) {
  return var;
}

String optimised_events_processor(const String& var) {
  return var;
}

String get_firmware_info_processor(const String& var) {
  return var;
}
