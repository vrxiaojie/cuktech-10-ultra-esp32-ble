#include "cuktech_control.h"

#include "cuktech_protocol.h"

bool cuktech_control_command_valid(const cuktech_control_command_t *command)
{
    if (command == NULL) {
        return false;
    }
    if (command->type == CUKTECH_CONTROL_COMMAND_SET) {
        return cuktech_piid_value_valid(command->data.set.piid,
                                        command->data.set.value);
    }
    if (command->type == CUKTECH_CONTROL_COMMAND_PORT) {
        return command->data.port.target >= CUKTECH_PORT_TARGET_C1 &&
               command->data.port.target <= CUKTECH_PORT_TARGET_ALL;
    }
    return false;
}

bool cuktech_control_apply_port_mask(uint32_t current_mask,
                                     cuktech_port_target_t target,
                                     bool enabled, uint32_t *new_mask)
{
    if (new_mask == NULL || current_mask > 0x0fU ||
        target < CUKTECH_PORT_TARGET_C1 ||
        target > CUKTECH_PORT_TARGET_ALL) {
        return false;
    }
    if (target == CUKTECH_PORT_TARGET_ALL) {
        *new_mask = enabled ? 0x0fU : 0U;
        return true;
    }
    uint32_t bit = 1UL << (uint32_t)target;
    *new_mask = enabled ? current_mask | bit : current_mask & ~bit;
    return true;
}

const char *cuktech_port_target_name(cuktech_port_target_t target)
{
    switch (target) {
    case CUKTECH_PORT_TARGET_C1:
        return "c1";
    case CUKTECH_PORT_TARGET_C2:
        return "c2";
    case CUKTECH_PORT_TARGET_C3:
        return "c3";
    case CUKTECH_PORT_TARGET_A:
        return "a";
    case CUKTECH_PORT_TARGET_ALL:
        return "all";
    default:
        return "unknown";
    }
}
