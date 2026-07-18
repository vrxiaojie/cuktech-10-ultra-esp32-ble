#include <stdio.h>
#include <stdlib.h>

#include "cuktech_control.h"

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
    cuktech_control_command_t set = {
        .type = CUKTECH_CONTROL_COMMAND_SET,
        .data.set = {.piid = 5U, .value = 3U},
    };
    CHECK(cuktech_control_command_valid(&set));
    set.data.set.value = 5U;
    CHECK(!cuktech_control_command_valid(&set));

    uint32_t mask = 0U;
    CHECK(cuktech_control_apply_port_mask(0x0fU, CUKTECH_PORT_TARGET_C1,
                                          false, &mask));
    CHECK(mask == 0x0eU);
    CHECK(cuktech_control_apply_port_mask(0x0fU, CUKTECH_PORT_TARGET_C2,
                                          false, &mask));
    CHECK(mask == 0x0dU);
    CHECK(cuktech_control_apply_port_mask(mask, CUKTECH_PORT_TARGET_C2, true,
                                          &mask));
    CHECK(mask == 0x0fU);
    CHECK(cuktech_control_apply_port_mask(0x0fU, CUKTECH_PORT_TARGET_C3,
                                          false, &mask));
    CHECK(mask == 0x0bU);
    CHECK(cuktech_control_apply_port_mask(0x0fU, CUKTECH_PORT_TARGET_A,
                                          false, &mask));
    CHECK(mask == 0x07U);
    CHECK(cuktech_control_apply_port_mask(mask, CUKTECH_PORT_TARGET_ALL,
                                          false, &mask));
    CHECK(mask == 0U);
    CHECK(cuktech_control_apply_port_mask(mask, CUKTECH_PORT_TARGET_ALL, true,
                                          &mask));
    CHECK(mask == 0x0fU);
    CHECK(!cuktech_control_apply_port_mask(0x10U, CUKTECH_PORT_TARGET_C1,
                                           true, &mask));
    CHECK(cuktech_port_target_name(CUKTECH_PORT_TARGET_A)[0] == 'a');
    puts("control tests passed");
    return 0;
}
