#include "web_config.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "cJSON.h"
#include "charger_state.h"
#include "cuktech_ble.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "web_config_model.h"
#include "wifi_manager.h"

#define WEB_CONFIG_MAX_BODY_SIZE 256U

static const char *TAG = "web_config";
static httpd_handle_t s_server;

static void secure_zero(void *data, size_t length)
{
    volatile unsigned char *bytes = data;
    while (length-- > 0U) {
        *bytes++ = 0U;
    }
}

static const char PROVISION_PAGE[] =
    "<!doctype html><html lang='zh-CN'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>CUKTECH BLE 配网</title><style>body{font-family:sans-serif;max-width:32rem;"
    "margin:3rem auto;padding:0 1rem}label{display:block;margin-top:1rem}input,button{"
    "box-sizing:border-box;width:100%;padding:.75rem;margin-top:.35rem}#msg{white-space:pre-wrap}"
    "</style></head><body><h1>CUKTECH BLE 网关配网</h1>"
    "<p>此页面只接收 Wi-Fi SSID 和密码。Token、BLE Key 与 MQTT 密码请勿在配网 AP 中输入。</p>"
    "<form id='f'><label>Wi-Fi SSID<input id='s' maxlength='32' required></label>"
    "<label>Wi-Fi 密码<input id='p' type='password' maxlength='64'></label>"
    "<button>验证并保存</button></form><p id='msg'></p><script>"
    "f.onsubmit=async(e)=>{e.preventDefault();msg.textContent='正在验证网络…';try{"
    "let r=await fetch('/api/provision',{method:'POST',headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({ssid:s.value,password:p.value})});let j=await r.json();"
    "msg.textContent=j.ok?'连接成功，IP：'+j.ip+'。设备将在 3 秒后重启。':'失败：'+j.error;"
    "}catch(x){msg.textContent='请求失败，请重新连接配网热点后重试。'}};</script></body></html>";

static const char MANAGEMENT_PAGE[] =
    "<!doctype html><html lang='zh-CN'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>CUKTECH BLE 网关</title><style>body{font-family:sans-serif;max-width:42rem;"
    "margin:2rem auto;padding:0 1rem}label{display:block;margin-top:.8rem}input,button{"
    "box-sizing:border-box;width:100%;padding:.6rem;margin-top:.25rem}.row{display:grid;"
    "grid-template-columns:1fr 1fr;gap:.8rem}.check{display:flex;gap:.5rem}.check input{width:auto}"
    "code{word-break:break-all}</style></head><body><h1>CUKTECH BLE 网关</h1>"
    "<p>当前页面使用局域网 HTTP，不提供 TLS；只应在可信网络中使用。</p>"
    "<p id='status'>正在读取状态…</p><form id='f'><label>充电器 MAC<input id='mac' maxlength='17'></label>"
    "<label>米家 Token（24 位十六进制，留空保持）<input id='token' type='password' maxlength='24'></label>"
    "<label class='check'><input id='ct' type='checkbox'>清除 Token</label>"
    "<label>BLE Key（可选/预留，32 位十六进制，留空保持）<input id='key' type='password' maxlength='32'></label>"
    "<label class='check'><input id='ck' type='checkbox'>清除 BLE Key</label>"
    "<div class='row'><label>MQTT Host<input id='mh' maxlength='128'></label>"
    "<label>MQTT Port<input id='mp' type='number' min='1' max='65535'></label></div>"
    "<label>MQTT Username<input id='mu' maxlength='64'></label>"
    "<label>MQTT Password（留空保持）<input id='mw' type='password' maxlength='64'></label>"
    "<label class='check'><input id='cm' type='checkbox'>清除 MQTT Password</label>"
    "<label>MQTT Topic Prefix<input id='mt' maxlength='128'></label>"
    "<p>默认 <code>cuktech/charger</code> 才能直接兼容未经修改的现有 HA 集成。</p>"
    "<div class='row'><label>Keepalive<input id='mk' type='number' min='1' max='65535'></label>"
    "<label class='check'><input id='be' type='checkbox'>启用 BLE</label></div>"
    "<button>校验、保存并重启</button></form><p id='msg'></p><script>"
    "async function load(){let [s,c]=await Promise.all([fetch('/api/status').then(r=>r.json()),"
    "fetch('/api/config').then(r=>r.json())]);status.textContent='Wi-Fi: '+s.wifi_state+"
    "'；BLE: '+s.ble_state;mac.value=c.charger_mac;mh.value=c.mqtt_host;mp.value=c.mqtt_port;"
    "mu.value=c.mqtt_username;mt.value=c.mqtt_topic_prefix;mk.value=c.mqtt_keepalive;"
    "be.checked=c.ble_enabled;msg.textContent='Token: '+(c.token_configured?'已配置':'未配置')+"
    "'；BLE Key: '+(c.ble_key_configured?'已配置':'未配置')+'；MQTT 密码: '+"
    "(c.mqtt_password_configured?'已配置':'未配置');}"
    "f.onsubmit=async(e)=>{e.preventDefault();let b={charger_mac:mac.value,token:token.value,"
    "clear_token:ct.checked,ble_key:key.value,clear_ble_key:ck.checked,mqtt_host:mh.value,"
    "mqtt_port:Number(mp.value),mqtt_username:mu.value,mqtt_password:mw.value,"
    "clear_mqtt_password:cm.checked,mqtt_topic_prefix:mt.value,mqtt_keepalive:Number(mk.value),"
    "ble_enabled:be.checked};let r=await fetch('/api/config',{method:'POST',headers:{"
    "'Content-Type':'application/json'},body:JSON.stringify(b)});let j=await r.json();"
    "msg.textContent=j.ok?'保存成功，设备将在 3 秒后重启。':'失败：'+j.error;};load();"
    "</script></body></html>";

static esp_err_t set_security_headers(httpd_req_t *request)
{
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(request, "Cache-Control", "no-store"), TAG,
                        "set Cache-Control failed");
    ESP_RETURN_ON_ERROR(httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff"), TAG,
                        "set security header failed");
    return httpd_resp_set_hdr(request, "Content-Security-Policy",
                              "default-src 'self' 'unsafe-inline'; connect-src 'self'");
}

static esp_err_t root_handler(httpd_req_t *request)
{
    set_security_headers(request);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    const char *page = wifi_manager_get_state() == WIFI_MANAGER_STATE_STA_CONNECTED
                           ? MANAGEMENT_PAGE
                           : PROVISION_PAGE;
    return httpd_resp_send(request, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t send_json(httpd_req_t *request, const char *status,
                           const char *json)
{
    set_security_headers(request);
    httpd_resp_set_status(request, status);
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, json);
}

static esp_err_t receive_body(httpd_req_t *request, char *body, size_t body_size)
{
    if (request->content_len <= 0 || (size_t)request->content_len >= body_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t received = 0;
    while (received < (size_t)request->content_len) {
        int result = httpd_req_recv(request, body + received,
                                    request->content_len - received);
        if (result <= 0) {
            return ESP_FAIL;
        }
        received += (size_t)result;
    }
    body[received] = '\0';
    return ESP_OK;
}

static void restart_task(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
}

static void schedule_restart(void)
{
    if (xTaskCreate(restart_task, "config_restart", 2048, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to schedule configuration restart");
    }
}

static bool management_allowed(void)
{
    return wifi_manager_get_state() == WIFI_MANAGER_STATE_STA_CONNECTED;
}

static const char *provision_error_name(wifi_manager_provision_result_t result)
{
    switch (result) {
    case WIFI_MANAGER_PROVISION_INVALID_INPUT:
        return "invalid_input";
    case WIFI_MANAGER_PROVISION_BUSY:
        return "busy";
    case WIFI_MANAGER_PROVISION_CONNECT_FAILED:
        return "connect_failed";
    case WIFI_MANAGER_PROVISION_SAVE_FAILED:
        return "save_failed";
    default:
        return "internal_error";
    }
}

static esp_err_t provision_handler(httpd_req_t *request)
{
    char body[WEB_CONFIG_MAX_BODY_SIZE + 1U] = {0};
    if (receive_body(request, body, sizeof(body)) != ESP_OK) {
        secure_zero(body, sizeof(body));
        return send_json(request, "413 Payload Too Large",
                         "{\"ok\":false,\"error\":\"invalid_body\"}");
    }

    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        secure_zero(body, sizeof(body));
        return send_json(request, "400 Bad Request",
                         "{\"ok\":false,\"error\":\"invalid_json\"}");
    }
    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    const cJSON *password = cJSON_GetObjectItemCaseSensitive(root, "password");
    if (!cJSON_IsString(ssid) || !cJSON_IsString(password) ||
        ssid->valuestring == NULL || password->valuestring == NULL) {
        if (cJSON_IsString(password) && password->valuestring != NULL) {
            secure_zero(password->valuestring, strlen(password->valuestring));
        }
        cJSON_Delete(root);
        secure_zero(body, sizeof(body));
        return send_json(request, "400 Bad Request",
                         "{\"ok\":false,\"error\":\"missing_fields\"}");
    }

    char ip_address[16] = {0};
    wifi_manager_provision_result_t result = wifi_manager_provision(
        ssid->valuestring, password->valuestring, ip_address, sizeof(ip_address));
    secure_zero(password->valuestring, strlen(password->valuestring));
    cJSON_Delete(root);
    secure_zero(body, sizeof(body));

    if (result != WIFI_MANAGER_PROVISION_OK) {
        char response[96];
        snprintf(response, sizeof(response), "{\"ok\":false,\"error\":\"%s\"}",
                 provision_error_name(result));
        return send_json(request, result == WIFI_MANAGER_PROVISION_BUSY
                                      ? "409 Conflict"
                                      : "400 Bad Request",
                         response);
    }

    char response[80];
    snprintf(response, sizeof(response), "{\"ok\":true,\"ip\":\"%s\"}", ip_address);
    esp_err_t error = send_json(request, "200 OK", response);
    schedule_restart();
    return error;
}

static esp_err_t status_handler(httpd_req_t *request)
{
    const esp_app_desc_t *app = esp_app_get_description();
    cuktech_ble_status_t ble_status;
    cuktech_ble_get_status(&ble_status);
    charger_state_snapshot_t state;
    charger_state_get_snapshot(&state);

    cJSON *root = cJSON_CreateObject();
    cJSON *ports = cJSON_CreateObject();
    cJSON *settings = cJSON_CreateObject();
    cJSON *protocol_switches = cJSON_CreateObject();
    if (root == NULL || ports == NULL || settings == NULL ||
        protocol_switches == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(ports);
        cJSON_Delete(settings);
        cJSON_Delete(protocol_switches);
        return send_json(request, "500 Internal Server Error",
                         "{\"error\":\"out_of_memory\"}");
    }
    cJSON_AddBoolToObject(root, "connected", state.connected);
    cJSON_AddBoolToObject(root, "authenticated", state.authenticated);
    cJSON_AddBoolToObject(root, "mqtt_connected", false);
    cJSON_AddStringToObject(root, "device_model", state.device_model);
    cJSON_AddStringToObject(root, "firmware_version",
                           state.firmware_version);

    static const char *PORT_NAMES[CHARGER_STATE_PORT_COUNT] = {
        "c1", "c2", "c3", "a",
    };
    uint32_t enabled_mask =
        state.setting_valid[16U] ? state.settings[16U] : 0x0fU;
    for (size_t index = 0U; index < CHARGER_STATE_PORT_COUNT; ++index) {
        cJSON *port = cJSON_CreateObject();
        if (port == NULL) {
            cJSON_Delete(root);
            cJSON_Delete(ports);
            cJSON_Delete(settings);
            cJSON_Delete(protocol_switches);
            return send_json(request, "500 Internal Server Error",
                             "{\"error\":\"out_of_memory\"}");
        }
        cJSON_AddNumberToObject(port, "voltage", state.ports[index].voltage);
        cJSON_AddNumberToObject(port, "current", state.ports[index].current);
        cJSON_AddNumberToObject(port, "power", state.ports[index].power);
        cJSON_AddBoolToObject(port, "active", state.ports[index].active);
        cJSON_AddStringToObject(
            port, "protocol",
            cuktech_charge_protocol_name(state.ports[index].protocol));
        cJSON_AddBoolToObject(port, "enabled",
                              (enabled_mask & (1UL << index)) != 0U);
        cJSON_AddItemToObject(ports, PORT_NAMES[index], port);
    }
    cJSON_AddItemToObject(root, "ports", ports);

    char piid_name[4];
    for (uint16_t piid = 0U; piid <= CHARGER_STATE_MAX_PIID; ++piid) {
        if (state.setting_valid[piid]) {
            snprintf(piid_name, sizeof(piid_name), "%u", piid);
            cJSON_AddNumberToObject(settings, piid_name,
                                   state.settings[piid]);
        }
    }
    cJSON_AddItemToObject(root, "settings", settings);
    cJSON_AddNumberToObject(root, "protocol_extend", state.protocol_extend);
    for (size_t index = 0U; index < CHARGER_STATE_PORT_COUNT; ++index) {
        cJSON *switches = cJSON_CreateObject();
        if (switches == NULL) {
            cJSON_Delete(root);
            cJSON_Delete(protocol_switches);
            return send_json(request, "500 Internal Server Error",
                             "{\"error\":\"out_of_memory\"}");
        }
        if (index <= 1U) {
            cJSON_AddBoolToObject(switches, "pd",
                                  state.protocol_switches[index].pd);
            cJSON_AddBoolToObject(switches, "pps",
                                  state.protocol_switches[index].pps);
        } else {
            cJSON_AddBoolToObject(switches, "scp",
                                  state.protocol_switches[index].scp);
        }
        cJSON_AddBoolToObject(switches, "ufcs",
                              state.protocol_switches[index].ufcs);
        cJSON_AddItemToObject(protocol_switches, PORT_NAMES[index], switches);
    }
    cJSON_AddItemToObject(root, "protocol_switches", protocol_switches);
    cJSON_AddStringToObject(root, "gateway_firmware_version", app->version);
    cJSON_AddStringToObject(root, "wifi_state",
                           wifi_manager_state_name(wifi_manager_get_state()));
    cJSON_AddStringToObject(root, "ble_state",
                           cuktech_ble_state_name(ble_status.state));
    cJSON_AddBoolToObject(root, "ble_gatt_ready", ble_status.gatt_ready);
    cJSON_AddNumberToObject(root, "ble_mtu", ble_status.mtu);
    cJSON_AddNumberToObject(root, "ble_notify_dropped",
                           ble_status.notifications_dropped);
    cJSON_AddStringToObject(root, "last_error", ble_status.last_error);
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "state_revision", state.revision);

    char *response = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (response == NULL) {
        return send_json(request, "500 Internal Server Error",
                         "{\"error\":\"out_of_memory\"}");
    }
    esp_err_t error = send_json(request, "200 OK", response);
    cJSON_free(response);
    return error;
}

static esp_err_t config_get_handler(httpd_req_t *request)
{
    if (!management_allowed()) {
        return send_json(request, "403 Forbidden", "{\"error\":\"sta_required\"}");
    }
    app_config_t config;
    bool found = false;
    esp_err_t error = app_config_load(&config, &found);
    if (error != ESP_OK) {
        return send_json(request, "500 Internal Server Error",
                         "{\"error\":\"config_load_failed\"}");
    }
    (void)found;
    char response[WEB_CONFIG_PUBLIC_JSON_SIZE];
    if (web_config_build_public_json(&config, response, sizeof(response)) !=
        WEB_CONFIG_MODEL_OK) {
        return send_json(request, "500 Internal Server Error",
                         "{\"error\":\"config_render_failed\"}");
    }
    return send_json(request, "200 OK", response);
}

static esp_err_t config_post_handler(httpd_req_t *request)
{
    if (!management_allowed()) {
        return send_json(request, "403 Forbidden", "{\"error\":\"sta_required\"}");
    }
    char body[WEB_CONFIG_API_MAX_BODY_SIZE + 1U] = {0};
    if (receive_body(request, body, sizeof(body)) != ESP_OK) {
        secure_zero(body, sizeof(body));
        return send_json(request, "413 Payload Too Large",
                         "{\"ok\":false,\"error\":\"body_too_large\"}");
    }

    app_config_t config;
    bool found = false;
    esp_err_t error = app_config_load(&config, &found);
    if (error != ESP_OK) {
        secure_zero(body, sizeof(body));
        return send_json(request, "500 Internal Server Error",
                         "{\"ok\":false,\"error\":\"config_load_failed\"}");
    }
    (void)found;
    web_config_model_status_t status =
        web_config_apply_json(body, request->content_len, &config);
    secure_zero(body, sizeof(body));
    if (status != WEB_CONFIG_MODEL_OK) {
        char response[96];
        snprintf(response, sizeof(response), "{\"ok\":false,\"error\":\"%s\"}",
                 web_config_model_status_name(status));
        return send_json(request, "400 Bad Request", response);
    }
    error = app_config_save(&config);
    if (error != ESP_OK) {
        return send_json(request, "500 Internal Server Error",
                         "{\"ok\":false,\"error\":\"config_save_failed\"}");
    }
    esp_err_t response_error = send_json(request, "200 OK", "{\"ok\":true}");
    schedule_restart();
    return response_error;
}

static esp_err_t captive_redirect_handler(httpd_req_t *request)
{
    httpd_resp_set_status(request, "302 Found");
    httpd_resp_set_hdr(request, "Location", "/");
    return httpd_resp_send(request, NULL, 0);
}

esp_err_t web_config_start(void)
{
    if (s_server != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;
    config.max_uri_handlers = 8;
    config.recv_wait_timeout = CONFIG_CUKTECH_WIFI_VERIFY_SECONDS + 5;
    config.send_wait_timeout = 10;
    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), TAG, "HTTP server start failed");

    const httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = root_handler};
    const httpd_uri_t provision = {
        .uri = "/api/provision", .method = HTTP_POST, .handler = provision_handler};
    const httpd_uri_t generate_204 = {
        .uri = "/generate_204", .method = HTTP_GET, .handler = captive_redirect_handler};
    const httpd_uri_t hotspot = {.uri = "/hotspot-detect.html",
                                 .method = HTTP_GET,
                                 .handler = captive_redirect_handler};
    const httpd_uri_t status = {
        .uri = "/api/status", .method = HTTP_GET, .handler = status_handler};
    const httpd_uri_t config_get = {
        .uri = "/api/config", .method = HTTP_GET, .handler = config_get_handler};
    const httpd_uri_t config_post = {
        .uri = "/api/config", .method = HTTP_POST, .handler = config_post_handler};
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &root), TAG,
                        "register root failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &provision), TAG,
                        "register provision failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &generate_204), TAG,
                        "register captive path failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &hotspot), TAG,
                        "register captive path failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &status), TAG,
                        "register status failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &config_get), TAG,
                        "register config GET failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &config_post), TAG,
                        "register config POST failed");
    ESP_LOGI(TAG, "HTTP configuration server started");
    return ESP_OK;
}
