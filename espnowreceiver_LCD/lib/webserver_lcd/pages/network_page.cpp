#include "network_page.h"

#include <Arduino.h>
#include <WiFi.h>

namespace NetworkPage {

// ─────────────────────────────────────────────────────────────────────────────
// The entire page is a single static string to keep RAM usage minimal and avoid
// Arduino String heap churn.  The JS fetches the current config on load and
// populates the form; saving POSTs JSON to /api/v1/network.
// ─────────────────────────────────────────────────────────────────────────────

static const char kPageHtml[] PROGMEM = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>LCD Receiver — Network Config</title>
<style>
  body{font-family:Arial,sans-serif;background:#1a1a2e;color:#e0e0e0;margin:0;padding:20px}
  h1{color:#4fc3f7;text-align:center;margin-bottom:4px}
  .sub{text-align:center;color:#888;font-size:.85em;margin-bottom:24px}
  .card{background:#16213e;border-radius:8px;padding:20px;margin-bottom:18px;max-width:640px;margin-left:auto;margin-right:auto}
  h3{color:#4fc3f7;margin:0 0 12px}
  .row{display:grid;grid-template-columns:160px 1fr;gap:8px;align-items:center;margin-bottom:6px}
  label{font-size:.9em;color:#bbb}
  input[type=text],input[type=password],input[type=number],select{
    width:100%;box-sizing:border-box;padding:6px 8px;background:#0f3460;
    border:1px solid #4fc3f7;border-radius:4px;color:#e0e0e0;font-size:.9em}
  input[readonly]{opacity:.6;cursor:default;border-color:#444}
  input[type=checkbox]{width:20px;height:20px;cursor:pointer}
  .btn{display:block;width:100%;max-width:640px;margin:0 auto;padding:12px;
       background:#4fc3f7;color:#1a1a2e;font-size:1.1em;font-weight:bold;
       border:none;border-radius:6px;cursor:pointer;transition:background .2s}
  .btn:hover{background:#81d4fa}
  .msg{text-align:center;margin-top:12px;min-height:22px;font-size:.9em}
  .ok{color:#66bb6a} .err{color:#ef5350}
  .section-toggle{cursor:pointer;user-select:none;display:flex;align-items:center;gap:8px}
  .collapsible{overflow:hidden;transition:max-height .3s ease;max-height:0}
  .collapsible.open{max-height:600px}
</style>
</head>
<body>
<h1>&#128225; LCD Receiver</h1>
<div class="sub">Network Configuration</div>

<div class="card">
  <h3>&#128270; WiFi Credentials</h3>
  <div class="row"><label>MAC address</label><input id="mac" type="text" readonly></div>
  <div class="row"><label>Hostname</label><input id="hostname" type="text" maxlength="31" placeholder="lcd-receiver"></div>
  <div class="row"><label>SSID <span style="color:#ef5350">*</span></label><input id="ssid" type="text" maxlength="31" required></div>
  <div class="row"><label>Password</label><input id="password" type="password" maxlength="63" placeholder="Leave blank to keep current"></div>
  <div class="row"><label>Current IP</label><input id="cur_ip" type="text" readonly></div>
</div>

<div class="card">
  <h3 class="section-toggle" onclick="toggle('static_ip_sec')">&#128279; Static IP <span id="static_ip_arrow">&#9654;</span></h3>
  <div class="row" style="margin-top:8px">
    <label>Use static IP</label>
    <input id="use_static_ip" type="checkbox" onchange="toggle('static_ip_sec',this.checked)">
  </div>
  <div id="static_ip_sec" class="collapsible">
    <div class="row"><label>IP address</label><input id="static_ip" type="text" placeholder="192.168.1.100"></div>
    <div class="row"><label>Gateway</label><input id="gateway" type="text" placeholder="192.168.1.1"></div>
    <div class="row"><label>Subnet mask</label><input id="subnet" type="text" placeholder="255.255.255.0"></div>
    <div class="row"><label>Primary DNS</label><input id="dns_primary" type="text" placeholder="8.8.8.8"></div>
    <div class="row"><label>Secondary DNS</label><input id="dns_secondary" type="text" placeholder="8.8.4.4"></div>
  </div>
</div>

<div class="card">
  <h3 class="section-toggle" onclick="toggle('mqtt_sec')">&#128268; MQTT <span id="mqtt_arrow">&#9654;</span></h3>
  <div class="row" style="margin-top:8px">
    <label>MQTT enabled</label>
    <input id="mqtt_enabled" type="checkbox" onchange="toggle('mqtt_sec',this.checked)">
  </div>
  <div id="mqtt_sec" class="collapsible">
    <div class="row"><label>Broker IP</label><input id="mqtt_server" type="text" placeholder="192.168.1.x"></div>
    <div class="row"><label>Port</label><input id="mqtt_port" type="number" min="1" max="65535" placeholder="1883"></div>
    <div class="row"><label>Username</label><input id="mqtt_username" type="text" maxlength="31"></div>
    <div class="row"><label>Password</label><input id="mqtt_password" type="password" maxlength="63" placeholder="Leave blank to keep current"></div>
  </div>
</div>

<div class="card">
  <h3>&#127912; Display</h3>
  <div class="row">
    <label for="power_bar_renderer_mode">Power Bar Renderer</label>
    <select id="power_bar_renderer_mode">
      <option value="0">Original</option>
      <option value="4">Original (Rounded Ends)</option>
      <option value="1">Soft</option>
      <option value="2">Linear</option>
      <option value="3">Hybrid</option>
    </select>
  </div>
</div>

<button class="btn" onclick="save()">&#128190; Save &amp; Reboot</button>
<div id="msg" class="msg"></div>

<script>
function toggle(id, force){
  const el=document.getElementById(id);
  const open=(force!==undefined)?force:!el.classList.contains('open');
  el.classList.toggle('open',open);
}
function msg(text,ok){
  const el=document.getElementById('msg');
  el.textContent=text;
  el.className='msg '+(ok?'ok':'err');
}
async function load(){
  try{
    const r=await fetch('/api/v1/network');
    const d=await r.json();
    if(!d.success){msg('Failed to load config',false);return;}
    document.getElementById('mac').value=d.wifi_mac||'';
    document.getElementById('hostname').value=d.hostname||'';
    document.getElementById('ssid').value=d.ssid||'';
    document.getElementById('cur_ip').value=d.wifi_ip||'';
    const si=d.use_static_ip||false;
    document.getElementById('use_static_ip').checked=si;
    toggle('static_ip_sec',si);
    document.getElementById('static_ip').value=d.static_ip||'';
    document.getElementById('gateway').value=d.gateway||'';
    document.getElementById('subnet').value=d.subnet||'';
    document.getElementById('dns_primary').value=d.dns_primary||'';
    document.getElementById('dns_secondary').value=d.dns_secondary||'';
    const me=d.mqtt_enabled||false;
    document.getElementById('mqtt_enabled').checked=me;
    toggle('mqtt_sec',me);
    document.getElementById('mqtt_server').value=d.mqtt_server||'';
    document.getElementById('mqtt_port').value=d.mqtt_port||1883;
    document.getElementById('mqtt_username').value=d.mqtt_username||'';
    const mode = Number.isInteger(Number(d.power_bar_renderer_mode)) ? Number(d.power_bar_renderer_mode) : 0;
    const safeMode = (mode >= 0 && mode <= 4) ? mode : 0;
    document.getElementById('power_bar_renderer_mode').value=String(safeMode);
  }catch(e){msg('Network error: '+e,false);}
}
async function save(){
  const ssid=document.getElementById('ssid').value.trim();
  if(!ssid){msg('SSID is required',false);return;}
  const body={
    hostname:document.getElementById('hostname').value.trim(),
    ssid,
    password:document.getElementById('password').value,
    use_static_ip:document.getElementById('use_static_ip').checked,
    static_ip:document.getElementById('static_ip').value.trim(),
    gateway:document.getElementById('gateway').value.trim(),
    subnet:document.getElementById('subnet').value.trim(),
    dns_primary:document.getElementById('dns_primary').value.trim(),
    dns_secondary:document.getElementById('dns_secondary').value.trim(),
    mqtt_enabled:document.getElementById('mqtt_enabled').checked,
    mqtt_server:document.getElementById('mqtt_server').value.trim(),
    mqtt_port:parseInt(document.getElementById('mqtt_port').value)||1883,
    mqtt_username:document.getElementById('mqtt_username').value.trim(),
    mqtt_password:document.getElementById('mqtt_password').value,
    power_bar_renderer_mode:parseInt(document.getElementById('power_bar_renderer_mode').value,10),
  };
  try{
    const r=await fetch('/api/v1/network',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
    const d=await r.json();
    if(d.success){msg('Saved! Rebooting in ~2 seconds\u2026',true);}
    else{msg('Error: '+(d.message||'unknown'),false);}
  }catch(e){msg('Network error: '+e,false);}
}
load();
</script>
</body>
</html>
)rawhtml";

// ─────────────────────────────────────────────────────────────────────────────

esp_err_t handle_get(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, kPageHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t handle_root(httpd_req_t* req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/config");
    return httpd_resp_send(req, nullptr, 0);
}

esp_err_t register_handler(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri      = "/config",
        .method   = HTTP_GET,
        .handler  = handle_get,
        .user_ctx = nullptr,
    };
    return httpd_register_uri_handler(server, &uri);
}

esp_err_t register_root(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = handle_root,
        .user_ctx = nullptr,
    };
    return httpd_register_uri_handler(server, &uri);
}

}  // namespace NetworkPage
