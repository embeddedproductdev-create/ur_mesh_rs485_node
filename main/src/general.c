#include "esp_chip_info.h"
#include "esp_log.h"
#include <general.h>

#define GENERAL_TAG "GENERAL"

void print_chip_info(void)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(GENERAL_TAG, "ESP32 chip — cores: %d, revision: %d",
             chip_info.cores, chip_info.revision);
}
