#include "web_config.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
    return httpd_resp_send(request, PROVISION_PAGE, HTTPD_RESP_USE_STRLEN);
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
    if (xTaskCreate(restart_task, "provision_restart", 2048, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to schedule restart after provisioning");
    }
    return error;
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
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &root), TAG,
                        "register root failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &provision), TAG,
                        "register provision failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &generate_204), TAG,
                        "register captive path failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &hotspot), TAG,
                        "register captive path failed");
    ESP_LOGI(TAG, "provisioning HTTP server started");
    return ESP_OK;
}
