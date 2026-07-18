#pragma once

#include <stdbool.h>
#include <stdint.h>

bool cuktech_ble_parse_mac(const char *text, uint8_t display_order[6]);
void cuktech_ble_mac_to_nimble(const uint8_t display_order[6],
                               uint8_t nimble_order[6]);
uint32_t cuktech_ble_next_backoff(uint32_t current_seconds,
                                  uint32_t maximum_seconds);
