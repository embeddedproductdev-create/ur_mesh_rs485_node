/*
 * RS485 Energy Meter Node — app_main
 *
 * Peripheral stack init order:
 *   1. NVS            — must be first, loads all persisted globals
 *   2. BLE Mesh       — provisioning, mesh transport
 *   3. LED            — needs provisioned/configured flags from NVS
 *   4. GPIO ISR svc   — must precede gpio_subsystem_init()
 *   5. GPIO subsystem — loads config from NVS, starts event task
 *   6. Heartbeat      — esp_timer periodic push, auto-starts if provisioned
 *   7. Modbus poll    — loads device registry, starts poll task
 *   8. Boot log
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include <main.h>
#include <common_types.h>
#include <general.h>
#include <flash.h>
#include <led.h>
#include <ble_mesh_node.h>
#include <ble_packet_handler.h>
#include <gpio_subsystem.h>
#include <heartbeat.h>
#include <modbus/modbus_poll_task.h>

#define TAG "MAIN"

/*===========================================================
 * Global state
 * Extern'd by modules via their respective headers.
 *===========================================================*/
uint8_t  newDevice            = 1;
char     serialNoStr[SERIAL_NO_LEN];
uint8_t  provisioned          = 0;
uint8_t  configured           = 0;
bool     fault_active         = false;
char     device_location_str[LOCATION_STR_LEN];
uint16_t publishPeriod        = DEFAULT_PUBLISH_PERIOD_SEC;
uint16_t elemaddr_global      = 0;

/*===========================================================
 * Boot info
 *===========================================================*/
static void print_basic_info(void)
{
    ESP_LOGW(TAG, "=-=-= BOOT SUCCESSFUL — v%d.%d.%d =-=-=",
             MAJ_VERSION, MIN_VERSION, PATCH_VERSION);
    ESP_LOGI(TAG, "Compiled    : %s %s", __DATE__, __TIME__);
    ESP_LOGI(TAG, "%-20s : %d",   "NewDevice",   newDevice);
    ESP_LOGI(TAG, "%-20s : %s",   "SerialNo",    serialNoStr);
    ESP_LOGI(TAG, "%-20s : %s",   "Location",    device_location_str);
    ESP_LOGI(TAG, "%-20s : %d",   "Provisioned", provisioned);
    ESP_LOGI(TAG, "%-20s : %d",   "Configured",  configured);
    ESP_LOGI(TAG, "%-20s : %ds",  "PubPeriod",   publishPeriod);
    ESP_LOGI(TAG, "%-20s : %d",   "Meter devs",  modbus_registry_get_count());
    ESP_LOGW(TAG, "=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=");
}

/*===========================================================
 * Entry point
 *===========================================================*/
void app_main(void)
{
    /* 1. NVS — must be first */
    nvs_init();

    /* 2. BLE Mesh node init */
    ble_init();

    /* 3. LED — provisioned/configured/fault_active loaded by nvs_init */
    led_init();

    /* 4. GPIO ISR service — must precede gpio_subsystem_init() */
    gpio_install_isr_service(0);

    /* 5. GPIO subsystem */
    gpio_subsystem_init();

    /* 6. Heartbeat timer */
    hb_init();

    /* 7. Modbus RS485 — loads device registry, starts poll task */
    modbus_poll_init();

    /* 8. Boot log */
    print_basic_info();
    print_chip_info();

    /* Drive LED to correct initial state */
    update_led_status();
}
