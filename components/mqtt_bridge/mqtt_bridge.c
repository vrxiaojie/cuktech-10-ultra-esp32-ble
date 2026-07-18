#include "mqtt_bridge.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "charger_state.h"
#include "cuktech_ble.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_bridge_model.h"
#include "mqtt_bridge_control.h"
#include "mqtt_client.h"

#define MQTT_BRIDGE_TASK_STACK_SIZE 4096U
#define MQTT_BRIDGE_TASK_PRIORITY 4U
#define MQTT_BRIDGE_POLL_MS 500U
#define MQTT_BRIDGE_CONTROL_PAYLOAD_MAX_LEN 256U

typedef struct {
    char host[APP_CONFIG_MQTT_HOST_MAX_LEN + 1U];
    uint16_t port;
    char username[APP_CONFIG_MQTT_USERNAME_MAX_LEN + 1U];
    char password[APP_CONFIG_MQTT_PASSWORD_MAX_LEN + 1U];
    bool password_configured;
    char topic_prefix[APP_CONFIG_MQTT_TOPIC_MAX_LEN + 1U];
    uint16_t keepalive;
} mqtt_runtime_config_t;

static const char *TAG = "mqtt_bridge";
static const char *PORT_NAMES[CHARGER_STATE_PORT_COUNT] = {
    "c1", "c2", "c3", "a",
};

static mqtt_runtime_config_t s_config;
static esp_mqtt_client_handle_t s_client;
static TaskHandle_t s_publish_task;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static mqtt_bridge_status_t s_status = {
    .state = MQTT_BRIDGE_STATE_NOT_STARTED,
};
static char s_topic_ports[CHARGER_STATE_PORT_COUNT][MQTT_BRIDGE_TOPIC_MAX_LEN + 1U];
static char s_topic_settings[MQTT_BRIDGE_TOPIC_MAX_LEN + 1U];
static char s_topic_status[MQTT_BRIDGE_TOPIC_MAX_LEN + 1U];
static char s_topic_set[MQTT_BRIDGE_TOPIC_MAX_LEN + 1U];
static char s_topic_port_control[MQTT_BRIDGE_TOPIC_MAX_LEN + 1U];
static bool s_force_full_snapshot;
static bool s_started;

const char *mqtt_bridge_state_name(mqtt_bridge_state_t state)
{
    switch (state) {
    case MQTT_BRIDGE_STATE_NOT_STARTED:
        return "not_started";
    case MQTT_BRIDGE_STATE_WAITING_CONFIG:
        return "waiting_config";
    case MQTT_BRIDGE_STATE_CONNECTING:
        return "connecting";
    case MQTT_BRIDGE_STATE_CONNECTED:
        return "connected";
    case MQTT_BRIDGE_STATE_DISCONNECTED:
        return "disconnected";
    case MQTT_BRIDGE_STATE_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

void mqtt_bridge_get_status(mqtt_bridge_status_t *status)
{
    if (status == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    *status = s_status;
    portEXIT_CRITICAL(&s_lock);
}

static void update_status(mqtt_bridge_state_t state, bool connected,
                          const char *last_error)
{
    portENTER_CRITICAL(&s_lock);
    s_status.state = state;
    s_status.connected = connected;
    if (last_error != NULL) {
        snprintf(s_status.last_error, sizeof(s_status.last_error), "%s",
                 last_error);
    }
    portEXIT_CRITICAL(&s_lock);
}

static void record_publish_failure(const char *stage)
{
    portENTER_CRITICAL(&s_lock);
    ++s_status.publish_failures;
    snprintf(s_status.last_error, sizeof(s_status.last_error), "publish:%s",
             stage);
    portEXIT_CRITICAL(&s_lock);
}

static void record_control_result(bool accepted, const char *error)
{
    portENTER_CRITICAL(&s_lock);
    ++s_status.commands_received;
    if (accepted) {
        ++s_status.commands_accepted;
    } else {
        ++s_status.commands_rejected;
        if (error != NULL) {
            snprintf(s_status.last_error, sizeof(s_status.last_error),
                     "control:%s", error);
        }
    }
    portEXIT_CRITICAL(&s_lock);
}

static bool is_connected(void)
{
    portENTER_CRITICAL(&s_lock);
    bool connected = s_status.connected;
    portEXIT_CRITICAL(&s_lock);
    return connected;
}

static bool take_force_full_snapshot(void)
{
    portENTER_CRITICAL(&s_lock);
    bool force = s_force_full_snapshot;
    s_force_full_snapshot = false;
    portEXIT_CRITICAL(&s_lock);
    return force;
}

static bool publish_json(const char *stage, const char *topic,
                         const char *payload,
                         mqtt_bridge_publication_t publication)
{
    int qos = mqtt_bridge_publication_qos(publication);
    int message_id = esp_mqtt_client_publish(s_client, topic, payload, 0, qos,
                                             mqtt_bridge_publication_retain(
                                                 publication));
    if (message_id < 0) {
        record_publish_failure(stage);
        ESP_LOGW(TAG, "MQTT publish enqueue failed stage=%s", stage);
        return false;
    }
    return true;
}

static bool publish_port(size_t index,
                         const charger_state_snapshot_t *snapshot)
{
    char payload[MQTT_BRIDGE_PORT_JSON_MAX_LEN];
    if (!mqtt_bridge_build_port_json(&snapshot->ports[index], payload,
                                     sizeof(payload))) {
        record_publish_failure("port_json");
        return false;
    }
    return publish_json(PORT_NAMES[index], s_topic_ports[index], payload,
                        (mqtt_bridge_publication_t)index);
}

static bool publish_settings(const charger_state_snapshot_t *snapshot)
{
    char payload[MQTT_BRIDGE_SETTINGS_JSON_MAX_LEN];
    if (!mqtt_bridge_build_settings_json(snapshot, payload, sizeof(payload))) {
        record_publish_failure("settings_json");
        return false;
    }
    return publish_json("settings", s_topic_settings, payload,
                        MQTT_BRIDGE_PUBLICATION_SETTINGS);
}

static bool publish_status(const charger_state_snapshot_t *snapshot)
{
    char payload[MQTT_BRIDGE_STATUS_JSON_MAX_LEN];
    if (!mqtt_bridge_build_status_json(snapshot, payload, sizeof(payload))) {
        record_publish_failure("status_json");
        return false;
    }
    return publish_json("status", s_topic_status, payload,
                        MQTT_BRIDGE_PUBLICATION_STATUS);
}

static void publish_task(void *argument)
{
    (void)argument;
    charger_state_snapshot_t previous;
    charger_state_core_reset(&previous);
    bool have_previous = false;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(MQTT_BRIDGE_POLL_MS));
        if (!is_connected()) {
            continue;
        }
        charger_state_snapshot_t current;
        charger_state_get_snapshot(&current);
        bool force_full = take_force_full_snapshot();
        if (!force_full && have_previous &&
            current.revision == previous.revision) {
            continue;
        }
        uint32_t publication_mask = mqtt_bridge_publication_mask(
            &current, &previous, have_previous, force_full);
        bool all_published = true;
        for (size_t index = 0U; index < CHARGER_STATE_PORT_COUNT; ++index) {
            if ((publication_mask & (1U << index)) != 0U) {
                all_published = publish_port(index, &current) && all_published;
            }
        }
        if ((publication_mask &
             (1U << MQTT_BRIDGE_PUBLICATION_SETTINGS)) != 0U) {
            all_published = publish_settings(&current) && all_published;
        }
        if ((publication_mask &
             (1U << MQTT_BRIDGE_PUBLICATION_STATUS)) != 0U) {
            all_published = publish_status(&current) && all_published;
        }
        if (all_published) {
            previous = current;
            have_previous = true;
        } else {
            portENTER_CRITICAL(&s_lock);
            s_force_full_snapshot = true;
            portEXIT_CRITICAL(&s_lock);
        }
    }
}

static bool topic_equals(const esp_mqtt_event_handle_t event,
                         const char *topic)
{
    size_t topic_len = strlen(topic);
    return event != NULL && event->topic != NULL && event->topic_len >= 0 &&
           (size_t)event->topic_len == topic_len &&
           memcmp(event->topic, topic, topic_len) == 0;
}

static void subscribe_control_topics(void)
{
    int set_id = esp_mqtt_client_subscribe(s_client, s_topic_set, 1);
    int port_id =
        esp_mqtt_client_subscribe(s_client, s_topic_port_control, 1);
    if (set_id < 0 || port_id < 0) {
        update_status(MQTT_BRIDGE_STATE_CONNECTED, true,
                      "control_subscribe_failed");
        ESP_LOGW(TAG, "MQTT control topic subscription failed");
        return;
    }
    ESP_LOGI(TAG, "MQTT control subscriptions ready");
}

static void handle_control_message(esp_mqtt_event_handle_t event)
{
    bool is_set = topic_equals(event, s_topic_set);
    bool is_port = topic_equals(event, s_topic_port_control);
    if (!is_set && !is_port) {
        return;
    }
    if (event->data == NULL || event->total_data_len <= 0 ||
        event->total_data_len > MQTT_BRIDGE_CONTROL_PAYLOAD_MAX_LEN ||
        event->current_data_offset != 0 ||
        event->data_len != event->total_data_len) {
        record_control_result(false, "invalid_length");
        ESP_LOGW(TAG, "MQTT control rejected: fragmented or oversized payload");
        return;
    }
    cuktech_control_command_t command;
    mqtt_bridge_control_status_t parse_status =
        is_set ? mqtt_bridge_parse_set_command(
                     event->data, (size_t)event->data_len, &command)
               : mqtt_bridge_parse_port_command(
                     event->data, (size_t)event->data_len, &command);
    if (parse_status != MQTT_BRIDGE_CONTROL_OK) {
        record_control_result(false,
                              mqtt_bridge_control_status_name(parse_status));
        ESP_LOGW(TAG, "MQTT control rejected stage=%s",
                 mqtt_bridge_control_status_name(parse_status));
        return;
    }
    uint32_t request_id = 0U;
    esp_err_t error = cuktech_ble_submit_command(&command, &request_id);
    if (error != ESP_OK) {
        record_control_result(false, esp_err_to_name(error));
        ESP_LOGW(TAG, "MQTT control enqueue failed error=%s",
                 esp_err_to_name(error));
        return;
    }
    record_control_result(true, NULL);
    if (command.type == CUKTECH_CONTROL_COMMAND_SET) {
        ESP_LOGI(TAG, "MQTT SET queued id=%" PRIu32 " piid=%u value=%" PRIu32,
                 request_id, command.data.set.piid, command.data.set.value);
    } else {
        ESP_LOGI(TAG, "MQTT port queued id=%" PRIu32 " target=%s action=%s",
                 request_id,
                 cuktech_port_target_name(command.data.port.target),
                 command.data.port.enabled ? "on" : "off");
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        portENTER_CRITICAL(&s_lock);
        if (s_status.state == MQTT_BRIDGE_STATE_DISCONNECTED) {
            ++s_status.reconnects;
        }
        s_status.state = MQTT_BRIDGE_STATE_CONNECTED;
        s_status.connected = true;
        s_status.last_error[0] = '\0';
        s_force_full_snapshot = true;
        portEXIT_CRITICAL(&s_lock);
        ESP_LOGI(TAG, "MQTT connected; scheduling full retained snapshot");
        subscribe_control_topics();
        if (s_publish_task != NULL) {
            xTaskNotifyGive(s_publish_task);
        }
        break;
    case MQTT_EVENT_DATA:
        handle_control_message(event);
        break;
    case MQTT_EVENT_DISCONNECTED:
        update_status(MQTT_BRIDGE_STATE_DISCONNECTED, false,
                      "broker_disconnected");
        ESP_LOGW(TAG, "MQTT disconnected; automatic reconnect enabled");
        break;
    case MQTT_EVENT_ERROR:
        if (event != NULL && event->error_handle != NULL) {
            char error[MQTT_BRIDGE_LAST_ERROR_MAX_LEN + 1U];
            snprintf(error, sizeof(error), "transport:%d",
                     event->error_handle->esp_transport_sock_errno);
            update_status(MQTT_BRIDGE_STATE_ERROR, false, error);
        } else {
            update_status(MQTT_BRIDGE_STATE_ERROR, false,
                          "transport_error");
        }
        break;
    default:
        break;
    }
}

static bool build_topics(void)
{
    for (size_t index = 0U; index < CHARGER_STATE_PORT_COUNT; ++index) {
        char suffix[16];
        int written = snprintf(suffix, sizeof(suffix), "port/%s",
                               PORT_NAMES[index]);
        if (written < 0 || (size_t)written >= sizeof(suffix) ||
            !mqtt_bridge_build_topic(s_config.topic_prefix, suffix,
                                     s_topic_ports[index],
                                     sizeof(s_topic_ports[index]))) {
            return false;
        }
    }
    return mqtt_bridge_build_topic(s_config.topic_prefix, "settings",
                                   s_topic_settings,
                                   sizeof(s_topic_settings)) &&
           mqtt_bridge_build_topic(s_config.topic_prefix, "status",
                                   s_topic_status, sizeof(s_topic_status)) &&
           mqtt_bridge_build_topic(s_config.topic_prefix, "set", s_topic_set,
                                   sizeof(s_topic_set)) &&
           mqtt_bridge_build_topic(s_config.topic_prefix, "port",
                                   s_topic_port_control,
                                   sizeof(s_topic_port_control));
}

esp_err_t mqtt_bridge_start(const app_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(&s_status, 0, sizeof(s_status));
    if (config->mqtt_host[0] == '\0') {
        s_status.state = MQTT_BRIDGE_STATE_WAITING_CONFIG;
        ESP_LOGI(TAG, "MQTT waiting for broker configuration");
        return ESP_OK;
    }
    snprintf(s_config.host, sizeof(s_config.host), "%s", config->mqtt_host);
    s_config.port = config->mqtt_port;
    snprintf(s_config.username, sizeof(s_config.username), "%s",
             config->mqtt_username);
    snprintf(s_config.password, sizeof(s_config.password), "%s",
             config->mqtt_password);
    s_config.password_configured = config->mqtt_password_configured;
    snprintf(s_config.topic_prefix, sizeof(s_config.topic_prefix), "%s",
             config->mqtt_topic_prefix);
    s_config.keepalive = config->mqtt_keepalive;
    s_status.configured = true;
    if (!build_topics()) {
        update_status(MQTT_BRIDGE_STATE_ERROR, false, "topic_too_long");
        return ESP_ERR_INVALID_SIZE;
    }
    const esp_mqtt_client_config_t mqtt_config = {
        .broker.address.hostname = s_config.host,
        .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
        .broker.address.port = s_config.port,
        .credentials.username =
            s_config.username[0] != '\0' ? s_config.username : NULL,
        .credentials.authentication.password =
            s_config.password_configured ? s_config.password : NULL,
        .session.last_will.topic = s_topic_status,
        .session.last_will.msg = MQTT_BRIDGE_LWT_PAYLOAD,
        .session.last_will.msg_len = strlen(MQTT_BRIDGE_LWT_PAYLOAD),
        .session.last_will.qos = 1,
        .session.last_will.retain = 1,
        .session.keepalive = s_config.keepalive,
        .session.protocol_ver = MQTT_PROTOCOL_V_3_1_1,
        .network.reconnect_timeout_ms = 5000,
        .network.disable_auto_reconnect = false,
    };
    s_client = esp_mqtt_client_init(&mqtt_config);
    if (s_client == NULL) {
        update_status(MQTT_BRIDGE_STATE_ERROR, false, "client_init_failed");
        return ESP_ERR_NO_MEM;
    }
    esp_err_t error = esp_mqtt_client_register_event(
        s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (error != ESP_OK) {
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        update_status(MQTT_BRIDGE_STATE_ERROR, false,
                      "event_registration_failed");
        return error;
    }
    if (xTaskCreate(publish_task, "mqtt_publish", MQTT_BRIDGE_TASK_STACK_SIZE,
                    NULL, MQTT_BRIDGE_TASK_PRIORITY,
                    &s_publish_task) != pdPASS) {
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        update_status(MQTT_BRIDGE_STATE_ERROR, false,
                      "task_allocation_failed");
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    update_status(MQTT_BRIDGE_STATE_CONNECTING, false, "");
    error = esp_mqtt_client_start(s_client);
    if (error != ESP_OK) {
        s_started = false;
        vTaskDelete(s_publish_task);
        s_publish_task = NULL;
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        update_status(MQTT_BRIDGE_STATE_ERROR, false, "client_start_failed");
        return error;
    }
    ESP_LOGI(TAG, "MQTT client started broker=%s port=%u prefix=%s",
             s_config.host, s_config.port, s_config.topic_prefix);
    return ESP_OK;
}
