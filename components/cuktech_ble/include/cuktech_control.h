#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CUKTECH_CONTROL_COMMAND_SET = 0,
    CUKTECH_CONTROL_COMMAND_PORT,
} cuktech_control_command_type_t;

typedef enum {
    CUKTECH_PORT_TARGET_C1 = 0,
    CUKTECH_PORT_TARGET_C2,
    CUKTECH_PORT_TARGET_C3,
    CUKTECH_PORT_TARGET_A,
    CUKTECH_PORT_TARGET_ALL,
} cuktech_port_target_t;

typedef struct {
    cuktech_control_command_type_t type;
    union {
        struct {
            uint16_t piid;
            uint32_t value;
        } set;
        struct {
            cuktech_port_target_t target;
            bool enabled;
        } port;
    } data;
} cuktech_control_command_t;

bool cuktech_control_command_valid(const cuktech_control_command_t *command);
bool cuktech_control_apply_port_mask(uint32_t current_mask,
                                     cuktech_port_target_t target,
                                     bool enabled, uint32_t *new_mask);
const char *cuktech_port_target_name(cuktech_port_target_t target);
