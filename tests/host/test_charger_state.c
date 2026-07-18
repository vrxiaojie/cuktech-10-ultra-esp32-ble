#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "charger_state.h"

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__,         \
                    __LINE__, #condition);                                     \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

static void test_settings_decode(void)
{
    charger_state_snapshot_t state;
    charger_state_core_reset(&state);
    CHECK(charger_state_core_apply_setting(&state, 17U, 0x0732081eU));
    CHECK(state.pdo[0].valid);
    CHECK(state.pdo[0].capability == 0x32U);
    CHECK(state.pdo[0].kind == CUKTECH_PDO_PD_FIXED);
    CHECK(state.pdo[1].valid);
    CHECK(state.pdo[1].capability == 0x1eU);
    CHECK(state.pdo[1].kind == CUKTECH_PDO_PD_PPS);

    CHECK(charger_state_core_apply_setting(&state, 18U, 0x0814070fU));
    CHECK(state.pdo[2].kind == CUKTECH_PDO_PD_PPS);
    CHECK(state.pdo[3].kind == CUKTECH_PDO_PD_FIXED);

    CHECK(charger_state_core_apply_setting(&state, 21U, 0x03030f0fU));
    CHECK(state.protocol_switches[0].pd);
    CHECK(state.protocol_switches[0].pps);
    CHECK(state.protocol_switches[0].ufcs);
    CHECK(state.protocol_switches[1].pd);
    CHECK(state.protocol_switches[2].ufcs);
    CHECK(state.protocol_switches[2].scp);
    CHECK(state.protocol_switches[3].ufcs);
    CHECK(state.protocol_switches[3].scp);

    cuktech_pdo_kind_t kind = CUKTECH_PDO_UNKNOWN;
    cuktech_type_c_switches_t switches = {0};
    charger_state_core_protocol_inputs(&state, 1U, &kind, &switches);
    CHECK(kind == CUKTECH_PDO_PD_FIXED);
    CHECK(switches.available && switches.pd && switches.pps);
    CHECK(!charger_state_core_apply_setting(&state, 7U, 1U));
}

static void test_port_snapshot(void)
{
    charger_state_snapshot_t state;
    charger_state_core_reset(&state);
    cuktech_port_state_t port = {
        .voltage = 20.1F,
        .current = 2.5F,
        .power = 50.2F,
        .active = true,
        .protocol = CUKTECH_CHARGE_PD,
    };
    CHECK(charger_state_core_apply_port(&state, 1U, &port));
    CHECK(fabsf(state.ports[0].voltage - 20.1F) < 0.01F);
    CHECK(state.ports[0].protocol == CUKTECH_CHARGE_PD);
    CHECK(!charger_state_core_apply_port(&state, 0U, &port));
}

int main(void)
{
    test_settings_decode();
    test_port_snapshot();
    puts("charger_state tests passed");
    return 0;
}
