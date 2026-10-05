#ifndef FLASH_H
#define FLASH_H

#include "nvs_flash.h"
#include "nvs.h"
#include <common_types.h>

/*===========================================================
 * NVS partition names (must match partition table CSV)
 *===========================================================*/
#define GENERAL_NVS_PARTITION_NAME   "general"
#define SERIALNUM_NVS_PARTITION_NAME "serialnum"

/*===========================================================
 * NVS namespaces
 *===========================================================*/
#define GENERAL_NVS_NAMESPACE        "GENERAL"
#define SERIALNUM_NVS_NAMESPACE      "serial"

/*===========================================================
 * NVS handles (extern — defined in flash.c)
 *===========================================================*/
extern nvs_handle_t general_nvs_handle;

/*===========================================================
 * NVS key constants (extern — defined in flash.c)
 *===========================================================*/
extern const char *NVS_NEW_DEVICE_KEY;
extern const char *NVS_SERIAL_NO_KEY;
extern const char *NVS_DEVICE_LOCATION_KEY;
extern const char *NVS_CONFIGURED_KEY;
extern const char *NVS_PROVISIONED_KEY;
extern const char *NVS_PUBPERIOD_KEY;
extern const char *NVS_GPIO_CONFIG_KEY;      /* blob — gpio_config_blob_t  */
extern const char *NVS_DEVICE_REGISTRY_KEY; /* blob — device_registry_t   */

/*===========================================================
 * NVS handle selector enum
 *===========================================================*/
typedef enum
{
    GENERAL_HANDLE,
} handle_enum_t;

/*===========================================================
 * Function declarations
 *===========================================================*/
void     nvs_init(void);
error_codes factory_reset_device(void);

int32_t  get_number_from_nvs_flash(handle_enum_t nvshandle, const char *key, sizes_t size);
char    *get_str_from_nvs_flash(handle_enum_t nvshandle, const char *key);
void     set_number_in_nvs_flash(handle_enum_t nvshandle, const char *key, int value, sizes_t size);
void     set_str_in_nvs_flash(handle_enum_t nvshandle, const char *key, char *value);
void     set_blob_in_nvs_flash(handle_enum_t nvshandle, const char *key, const void *value, size_t length);
void     get_blob_from_nvs_flash(handle_enum_t nvshandle, const char *key, void *value, size_t *length);

#endif /* FLASH_H */
