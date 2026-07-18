#include "mqtt_bridge_control.h"

#include <stdint.h>
#include <string.h>

#include "cJSON.h"

static bool read_uint32(const cJSON *item, uint32_t *value)
{
    if (!cJSON_IsNumber(item) || item->valuedouble < 0.0 ||
        item->valuedouble > (double)UINT32_MAX) {
        return false;
    }
    uint32_t converted = (uint32_t)item->valuedouble;
    if ((double)converted != item->valuedouble) {
        return false;
    }
    *value = converted;
    return true;
}

static cJSON *parse_object(const char *payload, size_t payload_len)
{
    if (payload == NULL || payload_len == 0U) {
        return NULL;
    }
    cJSON *root = cJSON_ParseWithLength(payload, payload_len);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

mqtt_bridge_control_status_t mqtt_bridge_parse_set_command(
    const char *payload, size_t payload_len,
    cuktech_control_command_t *command)
{
    if (command == NULL || payload == NULL || payload_len == 0U) {
        return MQTT_BRIDGE_CONTROL_INVALID_ARGUMENT;
    }
    cJSON *root = parse_object(payload, payload_len);
    if (root == NULL) {
        return MQTT_BRIDGE_CONTROL_INVALID_JSON;
    }
    const cJSON *piid_item =
        cJSON_GetObjectItemCaseSensitive(root, "piid");
    const cJSON *value_item =
        cJSON_GetObjectItemCaseSensitive(root, "value");
    if (piid_item == NULL || value_item == NULL) {
        cJSON_Delete(root);
        return MQTT_BRIDGE_CONTROL_MISSING_FIELD;
    }
    uint32_t piid = 0U;
    uint32_t value = 0U;
    if (!read_uint32(piid_item, &piid) || !read_uint32(value_item, &value) ||
        piid > UINT16_MAX) {
        cJSON_Delete(root);
        return MQTT_BRIDGE_CONTROL_INVALID_FIELD;
    }
    cuktech_control_command_t candidate = {
        .type = CUKTECH_CONTROL_COMMAND_SET,
        .data.set = {
            .piid = (uint16_t)piid,
            .value = value,
        },
    };
    if (!cuktech_control_command_valid(&candidate)) {
        cJSON_Delete(root);
        return MQTT_BRIDGE_CONTROL_OUT_OF_RANGE;
    }
    *command = candidate;
    cJSON_Delete(root);
    return MQTT_BRIDGE_CONTROL_OK;
}

static bool parse_port_target(const char *name, cuktech_port_target_t *target)
{
    static const char *NAMES[] = {"c1", "c2", "c3", "a", "all"};
    for (size_t index = 0U; index < sizeof(NAMES) / sizeof(NAMES[0]); ++index) {
        if (strcmp(name, NAMES[index]) == 0) {
            *target = (cuktech_port_target_t)index;
            return true;
        }
    }
    return false;
}

mqtt_bridge_control_status_t mqtt_bridge_parse_port_command(
    const char *payload, size_t payload_len,
    cuktech_control_command_t *command)
{
    if (command == NULL || payload == NULL || payload_len == 0U) {
        return MQTT_BRIDGE_CONTROL_INVALID_ARGUMENT;
    }
    cJSON *root = parse_object(payload, payload_len);
    if (root == NULL) {
        return MQTT_BRIDGE_CONTROL_INVALID_JSON;
    }
    const cJSON *port = cJSON_GetObjectItemCaseSensitive(root, "port");
    const cJSON *action = cJSON_GetObjectItemCaseSensitive(root, "action");
    if (port == NULL || action == NULL) {
        cJSON_Delete(root);
        return MQTT_BRIDGE_CONTROL_MISSING_FIELD;
    }
    if (!cJSON_IsString(port) || !cJSON_IsString(action) ||
        port->valuestring == NULL || action->valuestring == NULL) {
        cJSON_Delete(root);
        return MQTT_BRIDGE_CONTROL_INVALID_FIELD;
    }
    cuktech_port_target_t target;
    if (!parse_port_target(port->valuestring, &target) ||
        (strcmp(action->valuestring, "on") != 0 &&
         strcmp(action->valuestring, "off") != 0)) {
        cJSON_Delete(root);
        return MQTT_BRIDGE_CONTROL_OUT_OF_RANGE;
    }
    cuktech_control_command_t candidate = {
        .type = CUKTECH_CONTROL_COMMAND_PORT,
        .data.port = {
            .target = target,
            .enabled = strcmp(action->valuestring, "on") == 0,
        },
    };
    *command = candidate;
    cJSON_Delete(root);
    return MQTT_BRIDGE_CONTROL_OK;
}

const char *mqtt_bridge_control_status_name(
    mqtt_bridge_control_status_t status)
{
    switch (status) {
    case MQTT_BRIDGE_CONTROL_OK:
        return "ok";
    case MQTT_BRIDGE_CONTROL_INVALID_ARGUMENT:
        return "invalid_argument";
    case MQTT_BRIDGE_CONTROL_INVALID_JSON:
        return "invalid_json";
    case MQTT_BRIDGE_CONTROL_MISSING_FIELD:
        return "missing_field";
    case MQTT_BRIDGE_CONTROL_INVALID_FIELD:
        return "invalid_field";
    case MQTT_BRIDGE_CONTROL_OUT_OF_RANGE:
        return "out_of_range";
    default:
        return "unknown";
    }
}
