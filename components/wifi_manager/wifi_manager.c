#include "wifi_manager.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#define WIFI_MANAGER_GOT_IP_BIT BIT0

ESP_EVENT_DEFINE_BASE(WIFI_MANAGER_INTERNAL_EVENT);

enum {
    WIFI_MANAGER_EVENT_ENABLE_FALLBACK = 1,
};

static const char *TAG = "wifi_manager";
static EventGroupHandle_t s_events;
static SemaphoreHandle_t s_state_lock;
static SemaphoreHandle_t s_provision_lock;
static esp_timer_handle_t s_fallback_timer;
static esp_netif_t *s_sta_netif;
static app_config_t s_config;
static wifi_manager_state_t s_state = WIFI_MANAGER_STATE_STOPPED;
static bool s_started;
static bool s_got_ip;
static bool s_ap_active;
static char s_ap_ssid[33];
static char s_ip_address[16];

static void set_state(wifi_manager_state_t state)
{
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    s_state = state;
    xSemaphoreGive(s_state_lock);
}

static app_config_t get_runtime_config(void)
{
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    app_config_t config = s_config;
    xSemaphoreGive(s_state_lock);
    return config;
}

static void set_runtime_config(const app_config_t *config)
{
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    s_config = *config;
    xSemaphoreGive(s_state_lock);
}

static bool runtime_wifi_configured(void)
{
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    bool configured = s_config.wifi_configured;
    xSemaphoreGive(s_state_lock);
    return configured;
}

static bool got_ip(void)
{
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    bool value = s_got_ip;
    xSemaphoreGive(s_state_lock);
    return value;
}

static void set_ip_state(bool connected, const char *ip_address)
{
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    s_got_ip = connected;
    if (connected && ip_address != NULL) {
        strlcpy(s_ip_address, ip_address, sizeof(s_ip_address));
    } else {
        s_ip_address[0] = '\0';
    }
    xSemaphoreGive(s_state_lock);
}

static bool ap_active(void)
{
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    bool value = s_ap_active;
    xSemaphoreGive(s_state_lock);
    return value;
}

static void set_ap_active(bool active)
{
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    s_ap_active = active;
    xSemaphoreGive(s_state_lock);
}

static void copy_ip_address(char *output, size_t output_size)
{
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    strlcpy(output, s_ip_address, output_size);
    xSemaphoreGive(s_state_lock);
}

static bool fallback_timer_active(void)
{
    return s_fallback_timer != NULL && esp_timer_is_active(s_fallback_timer);
}

static void stop_fallback_timer(void)
{
    if (fallback_timer_active()) {
        esp_timer_stop(s_fallback_timer);
    }
}

static void start_fallback_timer(void)
{
    if (!fallback_timer_active()) {
        esp_err_t error = esp_timer_start_once(
            s_fallback_timer, (uint64_t)CONFIG_CUKTECH_WIFI_FALLBACK_SECONDS * 1000000ULL);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "failed to start fallback timer: %s", esp_err_to_name(error));
        }
    }
}

static esp_err_t make_ap_config(wifi_config_t *ap_config)
{
    memset(ap_config, 0, sizeof(*ap_config));
    size_t password_len = strlen(CONFIG_CUKTECH_PROVISION_AP_PASSWORD);
    if (password_len != 0U && (password_len < 8U || password_len > 63U)) {
        ESP_LOGE(TAG, "provisioning AP password must be empty or 8-63 characters");
        return ESP_ERR_INVALID_ARG;
    }

    strlcpy((char *)ap_config->ap.ssid, s_ap_ssid, sizeof(ap_config->ap.ssid));
    ap_config->ap.ssid_len = strlen(s_ap_ssid);
    ap_config->ap.channel = 1;
    ap_config->ap.max_connection = 4;
    ap_config->ap.pmf_cfg.required = false;
    if (password_len == 0U) {
        ap_config->ap.authmode = WIFI_AUTH_OPEN;
    } else {
        strlcpy((char *)ap_config->ap.password, CONFIG_CUKTECH_PROVISION_AP_PASSWORD,
                sizeof(ap_config->ap.password));
        ap_config->ap.authmode = WIFI_AUTH_WPA2_PSK;
    }
    return ESP_OK;
}

static esp_err_t enable_ap(bool fallback)
{
    wifi_config_t ap_config;
    esp_err_t error = make_ap_config(&ap_config);
    if (error != ESP_OK) {
        return error;
    }
    error = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (error == ESP_OK) {
        error = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    }
    if (error == ESP_OK) {
        set_ap_active(true);
        set_state(fallback ? WIFI_MANAGER_STATE_FALLBACK_AP
                           : WIFI_MANAGER_STATE_PROVISIONING_AP);
        ESP_LOGW(TAG, "%s SoftAP enabled: ssid=%s",
                 fallback ? "fallback" : "provisioning", s_ap_ssid);
    }
    return error;
}

static void fallback_timer_callback(void *argument)
{
    (void)argument;
    esp_event_post(WIFI_MANAGER_INTERNAL_EVENT, WIFI_MANAGER_EVENT_ENABLE_FALLBACK,
                   NULL, 0, 0);
}

static void internal_event_handler(void *argument, esp_event_base_t base,
                                   int32_t event_id, void *event_data)
{
    (void)argument;
    (void)base;
    (void)event_data;
    if (event_id == WIFI_MANAGER_EVENT_ENABLE_FALLBACK && !got_ip() &&
        runtime_wifi_configured()) {
        esp_err_t error = enable_ap(true);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "failed to enable fallback SoftAP: %s", esp_err_to_name(error));
        }
    }
}

static void wifi_event_handler(void *argument, esp_event_base_t base, int32_t event_id,
                               void *event_data)
{
    (void)argument;
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (runtime_wifi_configured() &&
            wifi_manager_get_state() != WIFI_MANAGER_STATE_VERIFYING_STA) {
            esp_wifi_connect();
        }
        return;
    }

    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = event_data;
        set_ip_state(false, NULL);
        xEventGroupClearBits(s_events, WIFI_MANAGER_GOT_IP_BIT);
        if (runtime_wifi_configured()) {
            if (wifi_manager_get_state() != WIFI_MANAGER_STATE_VERIFYING_STA) {
                set_state(ap_active() ? WIFI_MANAGER_STATE_FALLBACK_AP
                                      : WIFI_MANAGER_STATE_STA_CONNECTING);
                ESP_LOGW(TAG, "STA disconnected: reason=%u; retrying", event->reason);
                esp_wifi_connect();
                start_fallback_timer();
            }
        }
        return;
    }

    if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = event_data;
        char ip_address[16];
        snprintf(ip_address, sizeof(ip_address), IPSTR, IP2STR(&event->ip_info.ip));
        set_ip_state(true, ip_address);
        stop_fallback_timer();
        wifi_manager_state_t previous_state = wifi_manager_get_state();
        if (previous_state != WIFI_MANAGER_STATE_VERIFYING_STA) {
            set_state(WIFI_MANAGER_STATE_STA_CONNECTED);
            if (previous_state == WIFI_MANAGER_STATE_FALLBACK_AP) {
                esp_err_t mode_error = esp_wifi_set_mode(WIFI_MODE_STA);
                if (mode_error == ESP_OK) {
                    set_ap_active(false);
                    ESP_LOGI(TAG, "fallback SoftAP disabled after STA recovery");
                }
            }
        }
        xEventGroupSetBits(s_events, WIFI_MANAGER_GOT_IP_BIT);
        ESP_LOGI(TAG, "STA obtained IP: %s", ip_address);
    }
}

static esp_err_t set_station_config(const app_config_t *config)
{
    wifi_config_t sta_config = {0};
    strlcpy((char *)sta_config.sta.ssid, config->wifi_ssid,
            sizeof(sta_config.sta.ssid));
    strlcpy((char *)sta_config.sta.password, config->wifi_password,
            sizeof(sta_config.sta.password));
    sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    sta_config.sta.pmf_cfg.capable = true;
    sta_config.sta.pmf_cfg.required = false;
    return esp_wifi_set_config(WIFI_IF_STA, &sta_config);
}

esp_err_t wifi_manager_start(const app_config_t *config)
{
    if (config == NULL || s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    s_config = *config;

    s_events = xEventGroupCreate();
    s_state_lock = xSemaphoreCreateMutex();
    s_provision_lock = xSemaphoreCreateMutex();
    if (s_events == NULL || s_state_lock == NULL || s_provision_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    esp_err_t error = esp_event_loop_create_default();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        return error;
    }
    s_sta_netif = esp_netif_create_default_wifi_sta();
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (s_sta_netif == NULL || ap_netif == NULL) {
        return ESP_ERR_NO_MEM;
    }

    uint8_t mac[6];
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_STA), TAG, "read MAC failed");
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "CUKTECH-BLE-%02X%02X", mac[4], mac[5]);

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "esp_wifi_init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG,
                        "set Wi-Fi RAM storage failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                   wifi_event_handler, NULL),
                        TAG, "register Wi-Fi handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                   wifi_event_handler, NULL),
                        TAG, "register IP handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_MANAGER_INTERNAL_EVENT,
                                                   ESP_EVENT_ANY_ID,
                                                   internal_event_handler, NULL),
                        TAG, "register internal handler failed");

    esp_timer_create_args_t timer_args = {
        .callback = fallback_timer_callback,
        .name = "wifi_fallback",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_fallback_timer), TAG,
                        "create fallback timer failed");

    wifi_config_t ap_config;
    ESP_RETURN_ON_ERROR(make_ap_config(&ap_config), TAG, "invalid AP configuration");
    if (s_config.wifi_configured) {
        ESP_RETURN_ON_ERROR(set_station_config(&s_config), TAG, "set STA config failed");
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set STA mode failed");
        set_state(WIFI_MANAGER_STATE_STA_CONNECTING);
    } else {
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set AP mode failed");
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG,
                            "set AP config failed");
        set_ap_active(true);
        set_state(WIFI_MANAGER_STATE_PROVISIONING_AP);
    }
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");
    s_started = true;
    if (s_config.wifi_configured) {
        start_fallback_timer();
    } else {
        ESP_LOGW(TAG, "provisioning SoftAP enabled: ssid=%s", s_ap_ssid);
    }
    return ESP_OK;
}

wifi_manager_state_t wifi_manager_get_state(void)
{
    if (s_state_lock == NULL) {
        return WIFI_MANAGER_STATE_STOPPED;
    }
    xSemaphoreTake(s_state_lock, portMAX_DELAY);
    wifi_manager_state_t state = s_state;
    xSemaphoreGive(s_state_lock);
    return state;
}

const char *wifi_manager_state_name(wifi_manager_state_t state)
{
    switch (state) {
    case WIFI_MANAGER_STATE_PROVISIONING_AP:
        return "provisioning_ap";
    case WIFI_MANAGER_STATE_STA_CONNECTING:
        return "sta_connecting";
    case WIFI_MANAGER_STATE_STA_CONNECTED:
        return "sta_connected";
    case WIFI_MANAGER_STATE_VERIFYING_STA:
        return "verifying_sta";
    case WIFI_MANAGER_STATE_FALLBACK_AP:
        return "fallback_ap";
    default:
        return "stopped";
    }
}

const char *wifi_manager_get_ap_ssid(void)
{
    return s_ap_ssid;
}

static void restore_previous_station(const app_config_t *previous)
{
    set_runtime_config(previous);
    if (previous->wifi_configured) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(set_station_config(previous));
        esp_wifi_set_mode(WIFI_MODE_APSTA);
        set_ap_active(true);
        set_state(WIFI_MANAGER_STATE_FALLBACK_AP);
        esp_wifi_disconnect();
        esp_wifi_connect();
        start_fallback_timer();
    } else {
        esp_wifi_set_mode(WIFI_MODE_AP);
        set_ap_active(true);
        set_state(WIFI_MANAGER_STATE_PROVISIONING_AP);
    }
}

wifi_manager_provision_result_t wifi_manager_provision(const char *ssid,
                                                       const char *password,
                                                       char *ip_address,
                                                       size_t ip_address_size)
{
    if (!s_started || ssid == NULL || password == NULL || ip_address == NULL ||
        ip_address_size == 0U) {
        return WIFI_MANAGER_PROVISION_INTERNAL_ERROR;
    }
    size_t ssid_len = strnlen(ssid, APP_CONFIG_WIFI_SSID_MAX_LEN + 1U);
    size_t password_len = strnlen(password, APP_CONFIG_WIFI_PASSWORD_MAX_LEN + 1U);
    if (ssid_len == 0U || ssid_len > APP_CONFIG_WIFI_SSID_MAX_LEN ||
        password_len > APP_CONFIG_WIFI_PASSWORD_MAX_LEN) {
        return WIFI_MANAGER_PROVISION_INVALID_INPUT;
    }
    if (xSemaphoreTake(s_provision_lock, 0) != pdTRUE) {
        return WIFI_MANAGER_PROVISION_BUSY;
    }

    app_config_t previous = get_runtime_config();
    app_config_t candidate = previous;
    candidate.wifi_configured = true;
    memset(candidate.wifi_ssid, 0, sizeof(candidate.wifi_ssid));
    memset(candidate.wifi_password, 0, sizeof(candidate.wifi_password));
    memcpy(candidate.wifi_ssid, ssid, ssid_len);
    memcpy(candidate.wifi_password, password, password_len);
    if (app_config_normalize(&candidate) != APP_CONFIG_STATUS_OK) {
        xSemaphoreGive(s_provision_lock);
        return WIFI_MANAGER_PROVISION_INVALID_INPUT;
    }

    set_state(WIFI_MANAGER_STATE_VERIFYING_STA);
    xEventGroupClearBits(s_events, WIFI_MANAGER_GOT_IP_BIT);
    set_ip_state(false, NULL);
    set_runtime_config(&candidate);
    esp_err_t error = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (error == ESP_OK) {
        set_ap_active(true);
        error = set_station_config(&candidate);
    }
    if (error == ESP_OK) {
        esp_wifi_disconnect();
        error = esp_wifi_connect();
    }
    if (error != ESP_OK) {
        restore_previous_station(&previous);
        xSemaphoreGive(s_provision_lock);
        return WIFI_MANAGER_PROVISION_INTERNAL_ERROR;
    }

    EventBits_t bits = xEventGroupWaitBits(
        s_events, WIFI_MANAGER_GOT_IP_BIT, pdFALSE, pdTRUE,
        pdMS_TO_TICKS(CONFIG_CUKTECH_WIFI_VERIFY_SECONDS * 1000U));
    if ((bits & WIFI_MANAGER_GOT_IP_BIT) == 0U) {
        ESP_LOGW(TAG, "provisioning credentials did not obtain an IP before timeout");
        restore_previous_station(&previous);
        xSemaphoreGive(s_provision_lock);
        return WIFI_MANAGER_PROVISION_CONNECT_FAILED;
    }

    error = app_config_save(&candidate);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "validated Wi-Fi configuration could not be saved: %s",
                 esp_err_to_name(error));
        restore_previous_station(&previous);
        xSemaphoreGive(s_provision_lock);
        return WIFI_MANAGER_PROVISION_SAVE_FAILED;
    }

    set_runtime_config(&candidate);
    set_state(WIFI_MANAGER_STATE_STA_CONNECTED);
    copy_ip_address(ip_address, ip_address_size);
    xSemaphoreGive(s_provision_lock);
    return WIFI_MANAGER_PROVISION_OK;
}
