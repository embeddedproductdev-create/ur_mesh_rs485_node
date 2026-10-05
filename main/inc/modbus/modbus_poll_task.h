#ifndef MODBUS_POLL_TASK_H
#define MODBUS_POLL_TASK_H

#include <common_types.h>

#define MODBUS_POLL_TAG "MODBUS_POLL"




/*===========================================================
 * Public API
 *===========================================================*/

/**
 * @brief Initialise the device registry from NVS and start the poll task.
 *        Calls modbus_master_init() internally.
 *        Must be called from app_main after nvs_init().
 */
void modbus_poll_init(void);

/**
 * @brief Add or update a device in the registry.
 *        Persists the updated registry to NVS.
 *        The poll task picks up the change on its next iteration.
 *
 * @return SUCCESS, MODBUS_REGISTRY_FULL, or MODBUS_SLAVE_ADDR_INVALID
 */
error_codes modbus_registry_add_device(const MeterConfigCmd *cmd);

/**
 * @brief Remove a device from the registry by slave address.
 *        Persists the updated registry to NVS.
 *
 * @return SUCCESS or MODBUS_DEVICE_NOT_FOUND
 */
error_codes modbus_registry_remove_device(uint8_t slave_addr);

/**
 * @brief Return the current number of active devices in the registry.
 */
uint8_t modbus_registry_get_count(void);

error_codes modbus_registry_add_suman_ac(const SumanAcConfigCmd *cmd);
device_descriptor_t *modbus_registry_find_device(uint8_t slave_addr);

#endif /* MODBUS_POLL_TASK_H */
