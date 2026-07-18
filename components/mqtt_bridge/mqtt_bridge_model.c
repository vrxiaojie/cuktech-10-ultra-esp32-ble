#include "mqtt_bridge_model.h"

#include <stdio.h>
#include <string.h>

const char MQTT_BRIDGE_LWT_PAYLOAD[] = "{\"connected\":false}";

static bool append_text(char *output, size_t output_size, size_t *used,
                        const char *text)
{
    size_t text_len = strlen(text);
    if (*used >= output_size || text_len >= output_size - *used) {
        return false;
    }
    memcpy(output + *used, text, text_len + 1U);
    *used += text_len;
    return true;
}

static bool append_json_string(char *output, size_t output_size, size_t *used,
                               const char *value)
{
    if (!append_text(output, output_size, used, "\"")) {
        return false;
    }
    for (const unsigned char *cursor = (const unsigned char *)value;
         *cursor != 0U; ++cursor) {
        char escaped[7] = {0};
        const char *piece = escaped;
        switch (*cursor) {
        case '"':
            piece = "\\\"";
            break;
        case '\\':
            piece = "\\\\";
            break;
        case '\b':
            piece = "\\b";
            break;
        case '\f':
            piece = "\\f";
            break;
        case '\n':
            piece = "\\n";
            break;
        case '\r':
            piece = "\\r";
            break;
        case '\t':
            piece = "\\t";
            break;
        default:
            if (*cursor < 0x20U) {
                snprintf(escaped, sizeof(escaped), "\\u%04x", *cursor);
            } else {
                escaped[0] = (char)*cursor;
                escaped[1] = '\0';
            }
            break;
        }
        if (!append_text(output, output_size, used, piece)) {
            return false;
        }
    }
    return append_text(output, output_size, used, "\"");
}

bool mqtt_bridge_build_topic(const char *prefix, const char *suffix,
                             char *output, size_t output_size)
{
    if (prefix == NULL || suffix == NULL || output == NULL ||
        prefix[0] == '\0' || suffix[0] == '\0') {
        return false;
    }
    int written = snprintf(output, output_size, "%s/%s", prefix, suffix);
    return written >= 0 && (size_t)written < output_size;
}

bool mqtt_bridge_build_port_json(const cuktech_port_state_t *port,
                                 char *output, size_t output_size)
{
    if (port == NULL || output == NULL || output_size == 0U) {
        return false;
    }
    int written = snprintf(
        output, output_size,
        "{\"voltage\":%.1f,\"current\":%.1f,\"power\":%.1f,"
        "\"active\":%s,\"protocol\":\"%s\"}",
        (double)port->voltage, (double)port->current, (double)port->power,
        port->active ? "true" : "false",
        cuktech_charge_protocol_name(port->protocol));
    return written >= 0 && (size_t)written < output_size;
}

bool mqtt_bridge_build_settings_json(const charger_state_snapshot_t *state,
                                     char *output, size_t output_size)
{
    if (state == NULL || output == NULL || output_size < 3U) {
        return false;
    }
    size_t used = 0U;
    output[0] = '\0';
    if (!append_text(output, output_size, &used, "{")) {
        return false;
    }
    bool first = true;
    for (uint16_t piid = 0U; piid <= CHARGER_STATE_MAX_PIID; ++piid) {
        if (!state->setting_valid[piid]) {
            continue;
        }
        char field[32];
        int written = snprintf(field, sizeof(field), "%s\"%u\":%lu",
                               first ? "" : ",", piid,
                               (unsigned long)state->settings[piid]);
        if (written < 0 || (size_t)written >= sizeof(field) ||
            !append_text(output, output_size, &used, field)) {
            return false;
        }
        first = false;
    }
    return append_text(output, output_size, &used, "}");
}

bool mqtt_bridge_build_status_json(const charger_state_snapshot_t *state,
                                   char *output, size_t output_size)
{
    if (state == NULL || output == NULL || output_size < 3U) {
        return false;
    }
    size_t used = 0U;
    output[0] = '\0';
    char prefix[96];
    int written = snprintf(prefix, sizeof(prefix),
                           "{\"connected\":%s,\"authenticated\":%s,"
                           "\"device_model\":",
                           state->connected ? "true" : "false",
                           state->authenticated ? "true" : "false");
    if (written < 0 || (size_t)written >= sizeof(prefix) ||
        !append_text(output, output_size, &used, prefix) ||
        !append_json_string(output, output_size, &used, state->device_model) ||
        !append_text(output, output_size, &used, ",\"firmware_version\":") ||
        !append_json_string(output, output_size, &used,
                            state->firmware_version) ||
        !append_text(output, output_size, &used, "}")) {
        return false;
    }
    return true;
}

bool mqtt_bridge_ports_equal(const cuktech_port_state_t *left,
                             const cuktech_port_state_t *right)
{
    return left != NULL && right != NULL &&
           left->voltage_x10 == right->voltage_x10 &&
           left->current_x10 == right->current_x10 &&
           left->active == right->active && left->protocol == right->protocol;
}

bool mqtt_bridge_settings_equal(const charger_state_snapshot_t *left,
                                const charger_state_snapshot_t *right)
{
    return left != NULL && right != NULL &&
           memcmp(left->setting_valid, right->setting_valid,
                  sizeof(left->setting_valid)) == 0 &&
           memcmp(left->settings, right->settings,
                  sizeof(left->settings)) == 0;
}

bool mqtt_bridge_status_equal(const charger_state_snapshot_t *left,
                              const charger_state_snapshot_t *right)
{
    return left != NULL && right != NULL &&
           left->connected == right->connected &&
           left->authenticated == right->authenticated &&
           strcmp(left->device_model, right->device_model) == 0 &&
           strcmp(left->firmware_version, right->firmware_version) == 0;
}

int mqtt_bridge_publication_qos(mqtt_bridge_publication_t publication)
{
    if (publication >= MQTT_BRIDGE_PUBLICATION_PORT_C1 &&
        publication <= MQTT_BRIDGE_PUBLICATION_PORT_A) {
        return 0;
    }
    if (publication == MQTT_BRIDGE_PUBLICATION_SETTINGS ||
        publication == MQTT_BRIDGE_PUBLICATION_STATUS) {
        return 1;
    }
    return -1;
}

bool mqtt_bridge_publication_retain(mqtt_bridge_publication_t publication)
{
    return publication >= MQTT_BRIDGE_PUBLICATION_PORT_C1 &&
           publication <= MQTT_BRIDGE_PUBLICATION_STATUS;
}

uint32_t mqtt_bridge_publication_mask(
    const charger_state_snapshot_t *current,
    const charger_state_snapshot_t *previous, bool have_previous,
    bool force_full)
{
    if (current == NULL || (have_previous && previous == NULL)) {
        return 0U;
    }
    if (force_full || !have_previous) {
        return MQTT_BRIDGE_PUBLICATION_ALL_MASK;
    }
    uint32_t mask = 0U;
    for (size_t index = 0U; index < CHARGER_STATE_PORT_COUNT; ++index) {
        if (!mqtt_bridge_ports_equal(&current->ports[index],
                                     &previous->ports[index])) {
            mask |= 1U << index;
        }
    }
    if (!mqtt_bridge_settings_equal(current, previous)) {
        mask |= 1U << MQTT_BRIDGE_PUBLICATION_SETTINGS;
    }
    if (!mqtt_bridge_status_equal(current, previous)) {
        mask |= 1U << MQTT_BRIDGE_PUBLICATION_STATUS;
    }
    return mask;
}
