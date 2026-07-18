#include "cuktech_ble_core.h"

#include <stddef.h>

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

bool cuktech_ble_parse_mac(const char *text, uint8_t display_order[6])
{
    if (text == NULL || display_order == NULL) {
        return false;
    }
    char separator = text[2];
    if (separator != ':' && separator != '-') {
        return false;
    }
    for (size_t index = 0; index < 6U; ++index) {
        size_t offset = index * 3U;
        int high = hex_value(text[offset]);
        int low = hex_value(text[offset + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        display_order[index] = (uint8_t)((high << 4) | low);
        if (index < 5U && text[offset + 2U] != separator) {
            return false;
        }
    }
    return text[17] == '\0';
}

void cuktech_ble_mac_to_nimble(const uint8_t display_order[6],
                               uint8_t nimble_order[6])
{
    if (display_order == NULL || nimble_order == NULL) {
        return;
    }
    for (size_t index = 0; index < 6U; ++index) {
        nimble_order[index] = display_order[5U - index];
    }
}

uint32_t cuktech_ble_next_backoff(uint32_t current_seconds,
                                  uint32_t maximum_seconds)
{
    if (maximum_seconds == 0U) {
        return 0U;
    }
    if (current_seconds == 0U) {
        return 1U;
    }
    if (current_seconds >= maximum_seconds ||
        current_seconds > maximum_seconds / 2U) {
        return maximum_seconds;
    }
    return current_seconds * 2U;
}
