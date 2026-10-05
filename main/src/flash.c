#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#include <main.h>
#include <common_types.h>
#include <flash.h>
#include <led.h>

#define NVS_TAG "NVS"

/*===========================================================
 * NVS handles
 *===========================================================*/
nvs_handle_t general_nvs_handle;
static nvs_handle_t serial_num_nvs_handle;

/*===========================================================
 * NVS key string definitions
 * @warning Keys must be <= 15 characters (NVS constraint)
 *===========================================================*/
const char *NVS_NEW_DEVICE_KEY      = "NewDevice";
const char *NVS_SERIAL_NO_KEY       = "SerialNo";
const char *NVS_DEVICE_LOCATION_KEY = "Location";
const char *NVS_CONFIGURED_KEY      = "Configured";
const char *NVS_PROVISIONED_KEY     = "Provisioned";
const char *NVS_PUBPERIOD_KEY       = "PubPeriod";
const char *NVS_GPIO_CONFIG_KEY      = "GpioCfg";
const char *NVS_DEVICE_REGISTRY_KEY = "DevReg";

/*===========================================================
 * Externals from main.c
 *===========================================================*/
extern uint8_t  newDevice;
extern char     serialNoStr[SERIAL_NO_LEN];
extern uint8_t  provisioned;
extern uint8_t  configured;
extern char     device_location_str[LOCATION_STR_LEN];
extern uint16_t publishPeriod;

/* ------------------------------------------------------------------ */

/**
 * @brief Erase all persisted data and reset to factory state.
 */
error_codes factory_reset_device(void)
{
    esp_err_t err = ESP_OK;

    err = nvs_flash_erase_partition(GENERAL_NVS_PARTITION_NAME);
    if (err)
    {
        ESP_LOGE(NVS_TAG, "Erasing GENERAL partition failed: %s", esp_err_to_name(err));
        goto done;
    }

    err = nvs_flash_erase();   /* Clears BLE mesh NVS partition */
    if (err)
    {
        ESP_LOGE(NVS_TAG, "Erasing BLE NVS partition failed: %s", esp_err_to_name(err));
        goto done;
    }

    ESP_LOGW(NVS_TAG, "Flash data erased successfully");

done:
    if (err)
    {
        led_notify_fault(true);
        return DEVICE_DATA_ERASURE_FAILED;
    }
    return DEVICE_DATA_ERASURE_SUCCESSFUL;
}

/* ------------------------------------------------------------------ */

/**
 * @brief Write an integer value to NVS.
 */
void set_number_in_nvs_flash(handle_enum_t nvshandle, const char *key, int value, sizes_t size)
{
    esp_err_t err = ESP_OK;
    nvs_handle_t handle;

    switch (nvshandle)
    {
    case GENERAL_HANDLE: handle = general_nvs_handle; break;
    default:             return;
    }

    switch (size)
    {
    case INT8_SIZE:   err = nvs_set_i8 (handle, key, (int8_t)  value); break;
    case UINT8_SIZE:  err = nvs_set_u8 (handle, key, (uint8_t) value); break;
    case INT16_SIZE:  err = nvs_set_i16(handle, key, (int16_t) value); break;
    case UINT16_SIZE: err = nvs_set_u16(handle, key, (uint16_t)value); break;
    case INT32_SIZE:  err = nvs_set_i32(handle, key, (int32_t) value); break;
    case UINT32_SIZE: err = nvs_set_u32(handle, key, (uint32_t)value); break;
    default:          break;
    }

    if (err) ESP_LOGE(NVS_TAG, "Failed to set %s=%d: %s", key, value, esp_err_to_name(err));

    err = nvs_commit(handle);
    if (err) ESP_LOGE(NVS_TAG, "Commit failed [%s]: %s", key, esp_err_to_name(err));
}

/* ------------------------------------------------------------------ */

/**
 * @brief Read an integer value from NVS.
 * @note  Currently a stub — extend per call-site needs.
 */
int32_t get_number_from_nvs_flash(handle_enum_t nvshandle, const char *key, sizes_t size)
{
    (void)nvshandle;
    (void)key;
    (void)size;
    return 0;
}

/* ------------------------------------------------------------------ */

/**
 * @brief Write a string to NVS.
 */
void set_str_in_nvs_flash(handle_enum_t nvshandle, const char *key, char *value)
{
    esp_err_t err;
    nvs_handle_t handle;

    switch (nvshandle)
    {
    case GENERAL_HANDLE: handle = general_nvs_handle; break;
    default:             return;
    }

    err = nvs_set_str(handle, key, value);
    if (err) ESP_LOGE(NVS_TAG, "Failed to set str %s: %s", key, esp_err_to_name(err));

    err = nvs_commit(handle);
    if (err) ESP_LOGE(NVS_TAG, "Commit failed [%s]: %s", key, esp_err_to_name(err));
}

/* ------------------------------------------------------------------ */

/**
 * @brief Read a string from NVS.
 * @note  Currently a stub — returns empty string.
 */
char *get_str_from_nvs_flash(handle_enum_t nvshandle, const char *key)
{
    (void)nvshandle;
    (void)key;
    return "";
}

/* ------------------------------------------------------------------ */

/**
 * @brief Write a blob to NVS.
 */
void set_blob_in_nvs_flash(handle_enum_t nvshandle, const char *key, const void *value, size_t length)
{
    esp_err_t err;
    nvs_handle_t handle;

    switch (nvshandle)
    {
    case GENERAL_HANDLE: handle = general_nvs_handle; break;
    default:             return;
    }

    err = nvs_set_blob(handle, key, value, length);
    if (err) ESP_LOGE(NVS_TAG, "Failed to set blob %s: %s", key, esp_err_to_name(err));

    err = nvs_commit(handle);
    if (err) ESP_LOGE(NVS_TAG, "Commit failed [%s]: %s", key, esp_err_to_name(err));
}

/* ------------------------------------------------------------------ */

/**
 * @brief Read a blob from NVS.
 */
void get_blob_from_nvs_flash(handle_enum_t nvshandle, const char *key, void *out_value, size_t *length)
{
    nvs_handle_t handle;

    switch (nvshandle)
    {
    case GENERAL_HANDLE: handle = general_nvs_handle; break;
    default:             return;
    }

    esp_err_t err = nvs_get_blob(handle, key, out_value, length);
    if (err) ESP_LOGE(NVS_TAG, "Failed to get blob %s: %s", key, esp_err_to_name(err));
}

/* ------------------------------------------------------------------ */

static esp_err_t init_defaults_in_nvs(void)
{
    esp_err_t err;
    err = nvs_set_u8(general_nvs_handle, NVS_NEW_DEVICE_KEY, 0);
    if (err) ESP_LOGE(NVS_TAG, "Failed to init %s: %s", NVS_NEW_DEVICE_KEY, esp_err_to_name(err));
    err = nvs_commit(general_nvs_handle);
    if (err) ESP_LOGE(NVS_TAG, "Commit failed in init_defaults: %s", esp_err_to_name(err));
    return err;
}

static bool is_new_device(void)
{
    esp_err_t err = nvs_get_u8(general_nvs_handle, NVS_NEW_DEVICE_KEY, &newDevice);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI(NVS_TAG, "NVS_NEW_DEVICE_KEY not found — assuming new device");
        return true;
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(NVS_TAG, "Error reading NVS_NEW_DEVICE_KEY: %s", esp_err_to_name(err));
        return false;
    }
    return (bool)newDevice;
}

static void pull_data_from_nvs(void)
{
    size_t size;

    /* Serial number (read-only serialnum partition) */
    size = SERIAL_NO_LEN;
    nvs_get_str(serial_num_nvs_handle, NVS_SERIAL_NO_KEY, serialNoStr, &size);

    /* Device state flags */
    nvs_get_u8(general_nvs_handle, NVS_PROVISIONED_KEY, &provisioned);
    nvs_get_u8(general_nvs_handle, NVS_CONFIGURED_KEY,  &configured);

    /* Heartbeat publish period */
    nvs_get_u16(general_nvs_handle, NVS_PUBPERIOD_KEY, &publishPeriod);

    /* Device location string */
    size = LOCATION_STR_LEN;    nvs_get_str(general_nvs_handle, NVS_DEVICE_LOCATION_KEY, device_location_str, &size);
    /* GPIO config blob is loaded by gpio_subsystem_init(), not here */
}

/* ------------------------------------------------------------------ */

/**
 * @brief Initialise NVS partitions and load persisted data into RAM globals.
 */
void nvs_init(void)
{
    esp_err_t err;
    nvs_stats_t nvs_stats;

    /* ---- GENERAL partition ---- */
    err = nvs_flash_init_partition(GENERAL_NVS_PARTITION_NAME);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES  ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND ||
        err == ESP_ERR_NVS_NOT_ENOUGH_SPACE)
    {
        ESP_ERROR_CHECK(nvs_flash_erase_partition(GENERAL_NVS_PARTITION_NAME));
        err = nvs_flash_init_partition(GENERAL_NVS_PARTITION_NAME);
    }
    if (err) ESP_LOGE(NVS_TAG, "Failed to init GENERAL partition: %s", esp_err_to_name(err));

    /* ---- SERIALNUM partition (read-only) ---- */
    err = nvs_flash_init_partition(SERIALNUM_NVS_PARTITION_NAME);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES  ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND ||
        err == ESP_ERR_NVS_NOT_ENOUGH_SPACE)
    {
        ESP_ERROR_CHECK(nvs_flash_erase_partition(SERIALNUM_NVS_PARTITION_NAME));
        err = nvs_flash_init_partition(SERIALNUM_NVS_PARTITION_NAME);
    }
    if (err) ESP_LOGE(NVS_TAG, "Failed to init SERIALNUM partition: %s", esp_err_to_name(err));

    /* ---- Open namespaces ---- */
    err = nvs_open_from_partition(GENERAL_NVS_PARTITION_NAME, GENERAL_NVS_NAMESPACE,
                                  NVS_READWRITE, &general_nvs_handle);
    if (err)
    {
        ESP_LOGE(NVS_TAG, "Opening GENERAL partition failed: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_open_from_partition(SERIALNUM_NVS_PARTITION_NAME, SERIALNUM_NVS_NAMESPACE,
                                  NVS_READONLY, &serial_num_nvs_handle);
    if (err)
    {
        ESP_LOGE(NVS_TAG, "Opening SERIALNUM partition failed: %s", esp_err_to_name(err));
        return;
    }

    /* ---- First-boot defaults then data pull ---- */
    if (is_new_device())
        ESP_LOGW(NVS_TAG, "New device — initialising NVS defaults: %s",
                 esp_err_to_name(init_defaults_in_nvs()));

    pull_data_from_nvs();

    /* ---- Stats ---- */
    nvs_get_stats(GENERAL_NVS_PARTITION_NAME, &nvs_stats);
    ESP_LOGI(NVS_TAG, "GENERAL NVS — used: %d  free: %d  total: %d",
             nvs_stats.used_entries, nvs_stats.free_entries, nvs_stats.total_entries);
}
