#include "app_orchestrator.h"

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "app";

void app_orchestrator_start(void)
{
    esp_chip_info_t chip_info = {0};
    const esp_app_desc_t *app_desc = esp_app_get_description();

    esp_chip_info(&chip_info);

    ESP_LOGI(TAG, "CUKTECH BLE gateway starting");
    ESP_LOGI(TAG, "firmware=%s idf=%s", app_desc->version, app_desc->idf_ver);
    ESP_LOGI(TAG, "target=esp32c3 cores=%d revision=%d", chip_info.cores,
             chip_info.revision);
    ESP_LOGI(TAG, "free_heap=%lu", (unsigned long)esp_get_free_heap_size());
    ESP_LOGI(TAG, "orchestrator ready; services will start in later stages");
}
