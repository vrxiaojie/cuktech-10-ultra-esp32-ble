#pragma once

#include <stdbool.h>

#include "app_config_core.h"
#include "esp_err.h"

esp_err_t app_config_store_init(void);
esp_err_t app_config_load(app_config_t *config, bool *found);
esp_err_t app_config_save(const app_config_t *config);
