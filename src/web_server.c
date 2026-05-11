#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "web_server.h"
#include "nvs_storage.h"
#include "wifi_manager.h"
#include "config.h"

static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;

// HTML page con organizzazione migliorata e sezione Debug
static const char* HTML_PAGE = 
"<!DOCTYPE html>"
"<html>"
"<head>"
"    <meta charset='UTF-8'>"
"    <meta name='viewport' content='width=device-width, initial-scale=1'>"
"    <title>ESP32 C3 Serial Bridge</title>"
"    <style>"
"        * { box-sizing: border-box; }"
"        body { font-family: 'Segoe UI', Arial, sans-serif; margin: 0; padding: 20px; background: #1a1a2e; color: #eee; }"
"        .container { max-width: 1000px; margin: 0 auto; }"
"        .card { background: #16213e; border-radius: 10px; padding: 20px; margin-bottom: 20px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }"
"        h1 { color: #ffd700; margin: 0 0 10px 0; font-size: 28px; text-shadow: 2px 2px 4px rgba(0,0,0,0.5); }"
"        h2 { color: #ffd700; margin: 0 0 15px 0; font-size: 1.4em; border-left: 4px solid #ffd700; padding-left: 10px; }"
"        .status { padding: 12px; border-radius: 8px; margin: 10px 0; background: #0f3460; }"
"        .status-online { border-left: 4px solid #00ff00; }"
"        .status-offline { border-left: 4px solid #ff4444; }"
"        .info-row { margin: 12px 0; display: flex; flex-wrap: wrap; align-items: center; }"
"        .info-label { font-weight: bold; width: 150px; color: #ffd700; }"
"        .info-value { flex: 1; font-family: monospace; }"
"        input, button { padding: 8px 12px; margin: 5px; border-radius: 5px; border: none; }"
"        input { background: #0f3460; color: #eee; }"
"        input[type='text'] { width: 250px; }"
"        input[type='password'] { width: 180px; }"
"        input[type='checkbox'] { width: 18px; height: 18px; margin: 0 5px 0 0; vertical-align: middle; }"
"        select { background: #0f3460; color: #eee; border: none; padding: 8px 12px; border-radius: 5px; }"
"        button { background: #ffd700; color: #1a1a2e; cursor: pointer; transition: 0.3s; font-weight: bold; padding: 8px 16px; }"
"        button:hover { background: #ffaa00; transform: scale(1.02); }"
"        button.danger { background: #ff4444; color: white; }"
"        button.danger:hover { background: #cc0000; }"
"        button.success { background: #00cc66; color: white; }"
"        button.success:hover { background: #009944; }"
"        .network-item { background: #0f3460; padding: 12px; margin: 8px 0; border-radius: 5px; display: flex; justify-content: space-between; align-items: center; }"
"        .network-ssid { font-weight: bold; color: #ffd700; }"
"        .flex { display: flex; flex-wrap: wrap; gap: 10px; align-items: center; }"
"        .flex-right { display: flex; justify-content: flex-end; gap: 10px; margin-top: 15px; }"
"        .checkbox-row { display: flex; align-items: center; margin: 12px 0; }"
"        hr { border-color: #0f3460; margin: 15px 0; }"
"        .note-box { background: #0f3460; padding: 10px; border-radius: 5px; margin-bottom: 15px; border-left: 3px solid #ffd700; }"
"        .note-box small { color: #ffd700; }"
"        .example-networks { background: #0a0a1a; padding: 10px; border-radius: 5px; margin-top: 10px; }"
"        .example-title { color: #ffd700; font-size: 12px; margin-bottom: 5px; }"
"        .example-item { font-family: monospace; font-size: 11px; color: #888; }"
"        .label-show { display: flex; align-items: center; margin-left: 5px; cursor: pointer; }"
"        .label-show span { font-size: 12px; color: #ffd700; }"
"    </style>"
"</head>"
"<body>"
"<div class='container'>"
"    <div class='card'>"
"        <h1>🔧 ESP32 C3 Serial Bridge</h1>"
"        <div id='status'></div>"
"    </div>"
"    <div class='card'>"
"        <h2>📡 System</h2>"
"        <div id='sysinfo'></div>"
"    </div>"
"    <div class='card'>"
"        <h2>📶 Known WiFi Networks</h2>"
"        <div id='network-list'></div>"
"        <div class='example-networks'>"
"            <div class='example-title'>📝 Add your networks below:</div>"
"            <div class='example-item'>1. Enter your WiFi SSID and password</div>"
"            <div class='example-item'>2. Add up to 10 networks</div>"
"            <div class='example-item'>3. Bridge auto-connects to best signal</div>"
"        </div>"
"        <div class='flex' style='margin-top: 15px;'>"
"            <input type='text' id='new-ssid' placeholder='SSID (e.g., Your_Network)'>"
"            <input type='password' id='new-pwd' placeholder='Password'>"
"            <label class='label-show'>"
"                <input type='checkbox' id='show-pwd' onclick='togglePassword()'>"
"                <span>Show</span>"
"            </label>"
"            <button onclick='addNetwork()'>➕ Add / Update</button>"
"        </div>"
"    </div>"
"    <div class='card'>"
"        <h2>⚙️ Network Settings (STA Mode)</h2>"
"        <div class='note-box'>"
"            <small>⚠️ <strong>Note:</strong> These settings apply only when the bridge connects to your home/work WiFi network.<br>"
"            The <strong>AP (Access Point)</strong> always uses <strong>192.168.4.1</strong> regardless of this setting.</small>"
"        </div>"
"        <div class='info-row'>"
"            <span class='info-label'>Static IP (STA):</span>"
"            <input type='text' id='static-ip' placeholder='192.168.1.123' style='width:130px'>"
"        </div>"
"        <div class='checkbox-row'>"
"            <input type='checkbox' id='use-static'>"
"            <span class='info-label' style='width:auto; margin-left:5px;'>Use static IP for STA mode</span>"
"        </div>"
"    </div>"
"    <div class='card'>"
"        <h2>⚙️ Access Point Settings (AP Mode)</h2>"
"        <div class='info-row'>"
"            <span class='info-label'>AP SSID:</span>"
"            <input type='text' id='ap-ssid' style='width:250px'>"
"        </div>"
"        <div class='info-row'>"
"            <span class='info-label'>AP Password:</span>"
"            <input type='text' id='ap-pwd' style='width:250px'>"
"        </div>"
"        <div class='info-row'>"
"            <span class='info-label'>AP Channel:</span>"
"            <input type='number' id='ap-channel' min='1' max='13' style='width:80px'>"
"        </div>"
"    </div>"
"    <div class='card'>"
"        <h2>🐛 Debug Settings</h2>"
"        <div class='note-box'>"
"            <small>⚠️ <strong>Note:</strong> Enable debug logs only for troubleshooting.<br>"
"            Debug mode will show detailed serial output and may affect performance.</small>"
"        </div>"
"        <div class='info-row'>"
"            <span class='info-label'>Debug Mode:</span>"
"            <select id='debug-level' style='padding: 8px 12px; margin: 5px; border-radius: 5px; background: #0f3460; color: #eee; border: none;'>"
"                <option value='0'>Disabled (default)</option>"
"                <option value='1'>Basic Logs</option>"
"                <option value='2'>Verbose (WiFi/ESP-NOW)</option>"
"            </select>"
"            <button onclick='setDebugLevel()' style='margin-left: 10px;'>Apply</button>"
"        </div>"
"        <div class='info-row'>"
"            <span class='info-label'>Current Status:</span>"
"            <span class='info-value' id='debug-current'>Loading...</span>"
"        </div>"
"    </div>"
"    <div class='flex-right'>"
"        <button onclick='saveSettings()'>💾 Save All</button>"
"        <button class='danger' onclick='resetConfig()'>🔄 Reset Default</button>"
"        <button class='success' onclick='reboot()'>🔁 Reboot</button>"
"    </div>"
"</div>"
"<script>"
"function togglePassword() {"
"    var pwd = document.getElementById('new-pwd');"
"    if (pwd.type === 'password') {"
"        pwd.type = 'text';"
"    } else {"
"        pwd.type = 'password';"
"    }"
"}"
"function fetchStatus() {"
"    fetch('/api/status').then(r=>r.json()).then(data=>{"
"        let cls = data.wifi_connected ? 'status-online' : 'status-offline';"
"        let html = '<div class=\"status ' + cls + '\">';"
"        html += '<div class=\"info-row\"><span class=\"info-label\">📡 WiFi (STA):</span><span class=\"info-value\">' + (data.wifi_connected ? data.wifi_ssid + ' (' + data.wifi_ip + ')' : 'Disconnected') + '</span></div>';"
"        html += '<div class=\"info-row\"><span class=\"info-label\">🔗 ESP-NOW:</span><span class=\"info-value\">' + (data.espnow_paired ? '✅ Paired' : '⏳ Waiting for pairing') + '</span></div>';"
"        html += '<div class=\"info-row\"><span class=\"info-label\">🖥️ Telnet Clients:</span><span class=\"info-value\">' + data.telnet_clients + ' connected</span></div>';"
"        html += '<div class=\"info-row\"><span class=\"info-label\">📱 AP IP:</span><span class=\"info-value\">192.168.4.1 (always)</span></div>';"
"        html += '</div>';"
"        document.getElementById('status').innerHTML = html;"
"    });"
"}"
"function fetchSysInfo() {"
"    fetch('/api/sysinfo').then(r=>r.json()).then(data=>{"
"        let html = '<div class=\"info-row\"><span class=\"info-label\">Version:</span><span class=\"info-value\">' + data.version + '</span></div>';"
"        html += '<div class=\"info-row\"><span class=\"info-label\">Compiled:</span><span class=\"info-value\">' + data.compile_date + ' ' + data.compile_time + '</span></div>';"
"        html += '<div class=\"info-row\"><span class=\"info-label\">Connected to:</span><span class=\"info-value\">' + (data.wifi.connected ? data.wifi.ssid : 'None') + '</span></div>';"
"        html += '<div class=\"info-row\"><span class=\"info-label\">WiFi IP (STA):</span><span class=\"info-value\">' + (data.wifi.connected ? data.wifi.ip : 'N/A') + '</span></div>';"
"        html += '<div class=\"info-row\"><span class=\"info-label\">Free Memory:</span><span class=\"info-value\">' + (data.memory.free_heap / 1024).toFixed(1) + ' KB</span></div>';"
"        html += '<div class=\"info-row\"><span class=\"info-label\">Min Free Memory:</span><span class=\"info-value\">' + (data.memory.min_free_heap / 1024).toFixed(1) + ' KB</span></div>';"
"        document.getElementById('sysinfo').innerHTML = html;"
"    });"
"}"
"function fetchNetworks() {"
"    fetch('/api/networks').then(r=>r.json()).then(data=>{"
"        let html = '';"
"        if (data.networks.length === 0) {"
"            html = '<p style=\"color: #ffaa00;\">⚠️ No networks configured. Add a network below.</p>';"
"        } else {"
"            data.networks.forEach(n => {"
"                html += '<div class=\"network-item\">';"
"                html += '<span class=\"network-ssid\">📡 ' + n.ssid + '</span>';"
"                html += '<button onclick=\"removeNetwork(\\'' + n.ssid + '\\')\">🗑️ Remove</button>';"
"                html += '</div>';"
"            });"
"        }"
"        document.getElementById('network-list').innerHTML = html;"
"    });"
"}"
"function fetchSettings() {"
"    fetch('/api/settings').then(r=>r.json()).then(data=>{"
"        document.getElementById('ap-ssid').value = data.ap_ssid;"
"        document.getElementById('ap-pwd').value = data.ap_password;"
"        document.getElementById('ap-channel').value = data.ap_channel;"
"        document.getElementById('static-ip').value = data.static_ip;"
"        document.getElementById('use-static').checked = data.use_static_ip;"
"    });"
"}"
"function fetchDebugStatus() {"
"    fetch('/api/debug').then(r=>r.json()).then(data=>{"
"        let status = '';"
"        if (data.level == 0) status = '🔇 Disabled';"
"        else if (data.level == 1) status = '🔊 Basic Logs';"
"        else status = '🔊🔊 Verbose (WiFi/ESP-NOW)';"
"        document.getElementById('debug-current').innerHTML = status;"
"        document.getElementById('debug-level').value = data.level;"
"    });"
"}"
"function setDebugLevel() {"
"    let level = document.getElementById('debug-level').value;"
"    fetch('/api/debug', {"
"        method: 'POST',"
"        body: JSON.stringify({level: parseInt(level)}),"
"        headers: {'Content-Type': 'application/json'}"
"    }).then(() => {"
"        alert('Debug level changed. Reboot to take effect.');"
"        fetchDebugStatus();"
"    });"
"}"
"function addNetwork() {"
"    let ssid = document.getElementById('new-ssid').value.trim();"
"    let pwd = document.getElementById('new-pwd').value;"
"    if (!ssid) { alert('Enter SSID'); return; }"
"    fetch('/api/networks/add', {"
"        method: 'POST',"
"        body: JSON.stringify({ssid: ssid, password: pwd}),"
"        headers: {'Content-Type': 'application/json'}"
"    }).then(() => { fetchNetworks(); document.getElementById('new-ssid').value = ''; document.getElementById('new-pwd').value = ''; });"
"}"
"function removeNetwork(ssid) {"
"    if (confirm('Remove network ' + ssid + '?')) {"
"        fetch('/api/networks/remove', {"
"            method: 'POST',"
"            body: JSON.stringify({ssid: ssid}),"
"            headers: {'Content-Type': 'application/json'}"
"        }).then(() => fetchNetworks());"
"    }"
"}"
"function saveSettings() {"
"    let settings = {"
"        ap_ssid: document.getElementById('ap-ssid').value,"
"        ap_password: document.getElementById('ap-pwd').value,"
"        ap_channel: parseInt(document.getElementById('ap-channel').value) || 6,"
"        static_ip: document.getElementById('static-ip').value,"
"        use_static_ip: document.getElementById('use-static').checked"
"    };"
"    fetch('/api/settings', {"
"        method: 'POST',"
"        body: JSON.stringify(settings),"
"        headers: {'Content-Type': 'application/json'}"
"    }).then(() => { alert('Settings saved. Reboot to apply.'); });"
"}"
"function resetConfig() {"
"    if (confirm('⚠️ Reset ALL settings? The bridge will reboot.')) {"
"        fetch('/api/reset', {method: 'POST'}).then(() => { setTimeout(() => { location.reload(); }, 2000); });"
"    }"
"}"
"function reboot() {"
"    if (confirm('Reboot the bridge?')) {"
"        fetch('/api/reboot', {method: 'POST'});"
"        alert('Rebooting...');"
"    }"
"}"
"setInterval(() => { fetchStatus(); fetchSysInfo(); fetchDebugStatus(); }, 5000);"
"fetchStatus();"
"fetchNetworks();"
"fetchSettings();"
"fetchSysInfo();"
"fetchDebugStatus();"
"</script>"
"</body>"
"</html>";

// Helper per inviare risposta JSON
static void send_json_response(httpd_req_t *req, const char *fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buffer, strlen(buffer));
}

// ========== FUNZIONE PARSING JSON (supporta sia stringhe che numeri) ==========
static char* extract_json_value(const char *json, const char *key) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    
    char *key_pos = strstr(json, search);
    if (!key_pos) return NULL;
    
    // Cerca i due punti dopo la chiave
    char *colon = strchr(key_pos, ':');
    if (!colon) return NULL;
    
    // Salta eventuali spazi
    char *value_start = colon + 1;
    while (*value_start == ' ' || *value_start == '\t') value_start++;
    
    char *value_end = NULL;
    bool is_quoted = (*value_start == '"');
    
    if (is_quoted) {
        // Valore tra virgolette (stringa)
        value_start++;
        value_end = strchr(value_start, '"');
    } else {
        // Valore numerico o booleano (senza virgolette)
        value_end = value_start;
        while (*value_end && *value_end != ',' && *value_end != '}' && *value_end != ' ') {
            value_end++;
        }
    }
    
    if (!value_end || value_end == value_start) return NULL;
    
    int len = value_end - value_start;
    char *value = malloc(len + 1);
    if (!value) return NULL;
    
    strncpy(value, value_start, len);
    value[len] = '\0';
    
    return value;
}

// ========== HANDLER PAGINA PRINCIPALE ==========
static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_PAGE, strlen(HTML_PAGE));
    return ESP_OK;
}

// ========== API: /api/status ==========
static esp_err_t status_get_handler(httpd_req_t *req) {
    const bridge_config_t *cfg = nvs_storage_get_config();
    char connected_ssid[64] = "None";
    
    if (wifi_is_connected() && cfg->network_count > 0) {
        strncpy(connected_ssid, cfg->networks[0].ssid, sizeof(connected_ssid) - 1);
        connected_ssid[sizeof(connected_ssid) - 1] = '\0';
    }
    
    send_json_response(req,
        "{\"wifi_connected\":%s,\"wifi_ip\":\"%s\",\"wifi_ssid\":\"%s\",\"telnet_clients\":0,\"espnow_paired\":false}",
        wifi_is_connected() ? "true" : "false",
        wifi_get_ip(),
        connected_ssid);
    return ESP_OK;
}

// ========== API: /api/sysinfo ==========
static esp_err_t sysinfo_get_handler(httpd_req_t *req) {
    const bridge_config_t *cfg = nvs_storage_get_config();
    char connected_ssid[64] = "None";
    
    if (wifi_is_connected() && cfg->network_count > 0) {
        strncpy(connected_ssid, cfg->networks[0].ssid, sizeof(connected_ssid) - 1);
        connected_ssid[sizeof(connected_ssid) - 1] = '\0';
    }
    
    send_json_response(req,
        "{\"version\":\"1.0.0\",\"compile_date\":\"%s\",\"compile_time\":\"%s\","
        "\"wifi\":{\"connected\":%s,\"ssid\":\"%s\",\"ip\":\"%s\"},"
        "\"memory\":{\"free_heap\":%d,\"min_free_heap\":%d}}",
        __DATE__, __TIME__,
        wifi_is_connected() ? "true" : "false",
        connected_ssid,
        wifi_get_ip(),
        esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
    return ESP_OK;
}

// ========== API: /api/networks ==========
static esp_err_t networks_get_handler(httpd_req_t *req) {
    const bridge_config_t *cfg = nvs_storage_get_config();
    char networks_json[800] = "";
    
    for (int i = 0; i < cfg->network_count; i++) {
        if (i > 0) strcat(networks_json, ",");
        char net[100];
        snprintf(net, sizeof(net), "{\"ssid\":\"%s\"}", cfg->networks[i].ssid);
        strcat(networks_json, net);
    }
    
    send_json_response(req, "{\"networks\":[%s]}", networks_json);
    return ESP_OK;
}

// ========== API: /api/settings ==========
static esp_err_t settings_get_handler(httpd_req_t *req) {
    const bridge_config_t *cfg = nvs_storage_get_config();
    send_json_response(req,
        "{\"ap_ssid\":\"%s\",\"ap_password\":\"%s\",\"ap_channel\":%d,"
        "\"static_ip\":\"%s\",\"use_static_ip\":%s}",
        cfg->ap_ssid, cfg->ap_password, cfg->ap_channel,
        cfg->static_ip, cfg->use_static_ip ? "true" : "false");
    return ESP_OK;
}

// ========== API: /api/debug (GET - legge da NVS) ==========
static esp_err_t debug_get_handler(httpd_req_t *req) {
    const bridge_config_t *cfg = nvs_storage_get_config();
    int debug_level = cfg->debug_level;
    
    send_json_response(req, "{\"level\":%d}", debug_level);
    return ESP_OK;
}

// ========== API: /api/debug (POST - salva in NVS) ==========
static esp_err_t debug_post_handler(httpd_req_t *req) {
    char buffer[256];
    int ret = httpd_req_recv(req, buffer, sizeof(buffer) - 1);
    
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    buffer[ret] = '\0';
    
    ESP_LOGI(TAG, "📨 DEBUG POST ricevuto: '%s'", buffer);
    
    char *level_str = extract_json_value(buffer, "level");
    if (level_str) {
        int new_level = atoi(level_str);
        free(level_str);
        
        ESP_LOGI(TAG, "📊 Nuovo livello debug richiesto: %d", new_level);
        
        if (new_level >= 0 && new_level <= 2) {
            bridge_config_t *cfg = nvs_storage_get_config_mutable();
            cfg->debug_level = (uint8_t)new_level;
            nvs_storage_save_config();
            ESP_LOGI(TAG, "✅ Debug level saved: %d (reboot required)", new_level);
            httpd_resp_send(req, "{\"status\":\"reboot_required\"}", 28);
        } else {
            ESP_LOGE(TAG, "❌ Livello non valido: %d", new_level);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Level must be 0,1,2");
            return ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "❌ Impossibile estrarre 'level' da: %s", buffer);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid level");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

// ========== API: /api/reboot ==========
static esp_err_t reboot_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Reboot requested via web");
    httpd_resp_send(req, "{\"status\":\"rebooting\"}", 22);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

// ========== API: /api/reset ==========
static esp_err_t reset_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Reset config requested via web");
    nvs_storage_reset_default();
    httpd_resp_send(req, "{\"status\":\"reset_done\"}", 21);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

// ========== API POST GENERICO per add/remove/settings ==========
static esp_err_t api_post_handler(httpd_req_t *req) {
    char buffer[1024];
    int ret = httpd_req_recv(req, buffer, sizeof(buffer) - 1);
    
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    buffer[ret] = '\0';
    
    // Aggiungi rete
    if (strstr(req->uri, "/add")) {
        char *ssid = extract_json_value(buffer, "ssid");
        char *pwd = extract_json_value(buffer, "password");
        
        if (ssid && pwd) {
            ESP_LOGI(TAG, "========================================");
            ESP_LOGI(TAG, "🌐 RICEVUTO DA WEB:");
            ESP_LOGI(TAG, "   SSID: '%s'", ssid);
            ESP_LOGI(TAG, "   PWD:  '%s'", pwd);
            ESP_LOGI(TAG, "========================================");
            
            nvs_storage_add_network(ssid, pwd);
        } else {
            ESP_LOGE(TAG, "❌ Errore: ssid o password non trovati");
        }
        
        if (ssid) free(ssid);
        if (pwd) free(pwd);
        
        httpd_resp_send(req, "OK", 2);
    }
    // Rimuovi rete
    else if (strstr(req->uri, "/remove")) {
        char *ssid = extract_json_value(buffer, "ssid");
        if (ssid) {
            nvs_storage_remove_network(ssid);
            free(ssid);
        }
        httpd_resp_send(req, "OK", 2);
    }
    // Salva impostazioni
    else if (strstr(req->uri, "/settings")) {
        bridge_config_t *cfg = nvs_storage_get_config_mutable();
        
        char *ap_ssid = extract_json_value(buffer, "ap_ssid");
        char *ap_pwd = extract_json_value(buffer, "ap_password");
        char *ap_ch_str = extract_json_value(buffer, "ap_channel");
        char *static_ip = extract_json_value(buffer, "static_ip");
        char *use_static_str = extract_json_value(buffer, "use_static_ip");
        
        if (ap_ssid) {
            strncpy(cfg->ap_ssid, ap_ssid, MAX_AP_SSID_LEN - 1);
            cfg->ap_ssid[MAX_AP_SSID_LEN - 1] = '\0';
            free(ap_ssid);
        }
        if (ap_pwd) {
            strncpy(cfg->ap_password, ap_pwd, MAX_AP_PASSWORD_LEN - 1);
            cfg->ap_password[MAX_AP_PASSWORD_LEN - 1] = '\0';
            free(ap_pwd);
        }
        if (ap_ch_str) {
            cfg->ap_channel = atoi(ap_ch_str);
            free(ap_ch_str);
        }
        if (static_ip) {
            strncpy(cfg->static_ip, static_ip, MAX_IP_STR_LEN - 1);
            cfg->static_ip[MAX_IP_STR_LEN - 1] = '\0';
            free(static_ip);
        }
        if (use_static_str) {
            cfg->use_static_ip = (strcmp(use_static_str, "true") == 0);
            free(use_static_str);
        }
        
        nvs_storage_save_config();
        httpd_resp_send(req, "{\"status\":\"saved\"}", 17);
    }
    else {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

// ========== HANDLER FAVICON ==========
static esp_err_t favicon_get_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// ========== AVVIA WEB SERVER ==========
void web_server_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.stack_size = 8192;
    config.max_uri_handlers = 20;
    
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get_handler };
        httpd_uri_t favicon = { .uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_get_handler };
        httpd_uri_t status = { .uri = "/api/status", .method = HTTP_GET, .handler = status_get_handler };
        httpd_uri_t sysinfo = { .uri = "/api/sysinfo", .method = HTTP_GET, .handler = sysinfo_get_handler };
        httpd_uri_t networks = { .uri = "/api/networks", .method = HTTP_GET, .handler = networks_get_handler };
        httpd_uri_t settings = { .uri = "/api/settings", .method = HTTP_GET, .handler = settings_get_handler };
        httpd_uri_t debug_get = { .uri = "/api/debug", .method = HTTP_GET, .handler = debug_get_handler };
        httpd_uri_t debug_post = { .uri = "/api/debug", .method = HTTP_POST, .handler = debug_post_handler };
        httpd_uri_t add = { .uri = "/api/networks/add", .method = HTTP_POST, .handler = api_post_handler };
        httpd_uri_t remove = { .uri = "/api/networks/remove", .method = HTTP_POST, .handler = api_post_handler };
        httpd_uri_t save = { .uri = "/api/settings", .method = HTTP_POST, .handler = api_post_handler };
        httpd_uri_t reset = { .uri = "/api/reset", .method = HTTP_POST, .handler = reset_post_handler };
        httpd_uri_t reboot = { .uri = "/api/reboot", .method = HTTP_POST, .handler = reboot_post_handler };
        
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &favicon);
        httpd_register_uri_handler(server, &status);
        httpd_register_uri_handler(server, &sysinfo);
        httpd_register_uri_handler(server, &networks);
        httpd_register_uri_handler(server, &settings);
        httpd_register_uri_handler(server, &debug_get);
        httpd_register_uri_handler(server, &debug_post);
        httpd_register_uri_handler(server, &add);
        httpd_register_uri_handler(server, &remove);
        httpd_register_uri_handler(server, &save);
        httpd_register_uri_handler(server, &reset);
        httpd_register_uri_handler(server, &reboot);
        
        ESP_LOGI(TAG, "✅ Web server started (handlers: 13)");
    } else {
        ESP_LOGE(TAG, "❌ Failed to start web server");
    }
}

void web_server_stop(void) {
    if (server) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "Web server stopped");
    }
}

bool web_server_is_running(void) {
    return (server != NULL);
}