#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mqtt_bridge_control.h"

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,         \
                    __LINE__, #condition);                                     \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

int main(void)
{
    cuktech_control_command_t command;
    const char set_json[] = "{\"piid\":21,\"value\":50532111}";
    CHECK(mqtt_bridge_parse_set_command(set_json, strlen(set_json), &command) ==
          MQTT_BRIDGE_CONTROL_OK);
    CHECK(command.type == CUKTECH_CONTROL_COMMAND_SET);
    CHECK(command.data.set.piid == 21U);
    CHECK(command.data.set.value == 50532111U);

    const char invalid_range[] = "{\"piid\":5,\"value\":9}";
    CHECK(mqtt_bridge_parse_set_command(invalid_range, strlen(invalid_range),
                                        &command) ==
          MQTT_BRIDGE_CONTROL_OUT_OF_RANGE);
    const char invalid_type[] = "{\"piid\":5,\"value\":true}";
    CHECK(mqtt_bridge_parse_set_command(invalid_type, strlen(invalid_type),
                                        &command) ==
          MQTT_BRIDGE_CONTROL_INVALID_FIELD);

    const char port_json[] = "{\"port\":\"c3\",\"action\":\"off\"}";
    CHECK(mqtt_bridge_parse_port_command(port_json, strlen(port_json),
                                         &command) == MQTT_BRIDGE_CONTROL_OK);
    CHECK(command.type == CUKTECH_CONTROL_COMMAND_PORT);
    CHECK(command.data.port.target == CUKTECH_PORT_TARGET_C3);
    CHECK(!command.data.port.enabled);

    const char all_json[] = "{\"port\":\"all\",\"action\":\"on\"}";
    CHECK(mqtt_bridge_parse_port_command(all_json, strlen(all_json), &command) ==
          MQTT_BRIDGE_CONTROL_OK);
    CHECK(command.data.port.target == CUKTECH_PORT_TARGET_ALL);
    CHECK(command.data.port.enabled);

    const char unknown_port[] = "{\"port\":\"usb\",\"action\":\"on\"}";
    CHECK(mqtt_bridge_parse_port_command(unknown_port, strlen(unknown_port),
                                         &command) ==
          MQTT_BRIDGE_CONTROL_OUT_OF_RANGE);
    puts("mqtt control tests passed");
    return 0;
}
