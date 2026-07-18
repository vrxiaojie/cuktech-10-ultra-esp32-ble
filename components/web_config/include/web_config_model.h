#pragma once

#include <stddef.h>

#include "app_config_core.h"

#define WEB_CONFIG_API_MAX_BODY_SIZE 1024U
#define WEB_CONFIG_PUBLIC_JSON_SIZE 1024U

typedef enum {
    WEB_CONFIG_MODEL_OK = 0,
    WEB_CONFIG_MODEL_INVALID_ARGUMENT,
    WEB_CONFIG_MODEL_BODY_TOO_LARGE,
    WEB_CONFIG_MODEL_INVALID_JSON,
    WEB_CONFIG_MODEL_MISSING_FIELDS,
    WEB_CONFIG_MODEL_INVALID_FIELD,
    WEB_CONFIG_MODEL_INVALID_CONFIG,
    WEB_CONFIG_MODEL_OUTPUT_TOO_SMALL,
} web_config_model_status_t;

web_config_model_status_t web_config_apply_json(const char *json, size_t json_len,
                                                app_config_t *config);
web_config_model_status_t web_config_build_public_json(const app_config_t *config,
                                                       char *output,
                                                       size_t output_size);
const char *web_config_model_status_name(web_config_model_status_t status);
