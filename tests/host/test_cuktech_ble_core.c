#include "cuktech_ble_core.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition)                                                          \
    do {                                                                          \
        if (!(condition)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                           \
        }                                                                         \
    } while (0)

static void test_mac_parsing_and_nimble_layout(void)
{
    const uint8_t expected_display[6] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};
    const uint8_t expected_nimble[6] = {0xF6, 0xE5, 0xD4, 0xC3, 0xB2, 0xA1};
    uint8_t display[6] = {0};
    uint8_t nimble[6] = {0};

    CHECK(cuktech_ble_parse_mac("A1:B2:C3:D4:E5:F6", display));
    CHECK(memcmp(display, expected_display, sizeof(display)) == 0);
    cuktech_ble_mac_to_nimble(display, nimble);
    CHECK(memcmp(nimble, expected_nimble, sizeof(nimble)) == 0);

    memset(display, 0, sizeof(display));
    CHECK(cuktech_ble_parse_mac("a1-b2-c3-d4-e5-f6", display));
    CHECK(memcmp(display, expected_display, sizeof(display)) == 0);
    CHECK(!cuktech_ble_parse_mac("A1:B2:C3:D4:E5", display));
    CHECK(!cuktech_ble_parse_mac("A1:B2:C3-D4:E5:F6", display));
    CHECK(!cuktech_ble_parse_mac("A1:B2:C3:D4:E5:GG", display));
    CHECK(!cuktech_ble_parse_mac(NULL, display));
    CHECK(!cuktech_ble_parse_mac("A1:B2:C3:D4:E5:F6", NULL));
}

static void test_bounded_backoff(void)
{
    uint32_t delay = 0U;
    const uint32_t expected[] = {1U, 2U, 4U, 8U, 16U, 32U,
                                 64U, 128U, 256U, 300U, 300U};
    for (size_t index = 0U; index < sizeof(expected) / sizeof(expected[0]);
         ++index) {
        delay = cuktech_ble_next_backoff(delay, 300U);
        CHECK(delay == expected[index]);
    }
    CHECK(cuktech_ble_next_backoff(0U, 0U) == 0U);
    CHECK(cuktech_ble_next_backoff(200U, 300U) == 300U);
}

int main(void)
{
    test_mac_parsing_and_nimble_layout();
    test_bounded_backoff();
    if (failures != 0) {
        fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    puts("cuktech BLE core host tests passed");
    return 0;
}
