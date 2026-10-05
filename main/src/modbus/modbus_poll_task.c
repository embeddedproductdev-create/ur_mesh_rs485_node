#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"

#include <main.h>
#include <common_types.h>
#include <flash.h>
#include <led.h>
#include <ble_mesh_node.h>
#include <modbus/modbus_master.h>
#include <modbus/modbus_poll_task.h>

/*===========================================================
 * Externals
 *===========================================================*/
extern char     serialNoStr[SERIAL_NO_LEN];
extern uint16_t publishPeriod;
extern uint16_t elemaddr_global;   /* set by attach_elemAddr_to_structures() */

/*===========================================================
 * Module state
 *===========================================================*/
static device_registry_t s_registry;
static uint16_t          s_msgseqno = 0;

/*===========================================================
 * NVS persistence
 *===========================================================*/
static void registry_persist(void)
{
    set_blob_in_nvs_flash(GENERAL_HANDLE, NVS_DEVICE_REGISTRY_KEY,
                          &s_registry, sizeof(device_registry_t));
}

static void registry_load(void)
{
    size_t len = sizeof(device_registry_t);
    get_blob_from_nvs_flash(GENERAL_HANDLE, NVS_DEVICE_REGISTRY_KEY,
                            &s_registry, &len);

    if (len != sizeof(device_registry_t))
    {
        ESP_LOGW(MODBUS_POLL_TAG,
                 "No device registry in NVS — starting empty");
        memset(&s_registry, 0, sizeof(device_registry_t));
        registry_persist();
    }
    else
    {
        ESP_LOGI(MODBUS_POLL_TAG,
                 "Loaded device registry — %d active device(s)",
                 s_registry.count);
    }
}

/*===========================================================
 * Packet population helpers
 *===========================================================*/
static void fill_modbus_pkt(ModbusReadingPkt *pkt, uint16_t packetid,
                             const device_descriptor_t *dev)
{
    pkt->packetid      = packetid;
    pkt->msgseqno      = ++s_msgseqno;
    pkt->elemaddr      = elemaddr_global;
    pkt->node_type     = NODE_TYPE_RS485;
    pkt->slave_addr    = dev->slave_addr;
    strncpy(pkt->label, dev->label, DEVICE_LABEL_LEN - 1);
    pkt->label[DEVICE_LABEL_LEN - 1] = '\0';
    strncpy(pkt->deviceName, serialNoStr, SERIAL_NO_LEN - 1);
    pkt->deviceName[SERIAL_NO_LEN - 1] = '\0';
}

static void poll_single_device(const device_descriptor_t *dev)
{
    if (dev->reg_count == 0)
    {
        ESP_LOGW(MODBUS_POLL_TAG,
                 "slave=0x%02x has no register map — skipping", dev->slave_addr);
        return;
    }

    ModbusReadingPkt pkt = {0};
    fill_modbus_pkt(&pkt, NODE_METER_READING_PACKET, dev);
    pkt.reading_count = dev->reg_count;

    /* Read each register in the device's map */
    for (uint8_t r = 0; r < dev->reg_count; r++)
    {
        const register_entry_t *reg = &dev->regs[r];
        /* Always populate these — even for skipped registers */
        pkt.readings[r].param_idx   = reg->param_idx;
        pkt.readings[r].modbus_addr = reg->modbus_addr;
        pkt.readings[r].available   = reg->available;
        pkt.readings[r].raw         = 0;

        if (!reg->available)
        {
            ESP_LOGD(MODBUS_POLL_TAG,
                     "slave=0x%02x param=%d not available — skipping",
                     dev->slave_addr, reg->param_idx);
            continue;
        }

        uint16_t modbus_addr = reg->modbus_addr - 1;
        uint16_t reg_count   = (reg->data_type == REG_TYPE_UINT16) ? 1 : 2;
        uint8_t  buf[4]      = {0};

        error_codes result = modbus_read_holding_regs(dev->slave_addr,
                                                      modbus_addr,
                                                      reg_count,
                                                      buf);
        if (result != SUCCESS)
        {
            /* Available=1 + Raw=0 = comms error this cycle */
            ESP_LOGW(MODBUS_POLL_TAG,
                     "slave=0x%02x param=%d reg=0x%04x err=%d",
                     dev->slave_addr, reg->param_idx,
                     reg->modbus_addr, result);
            continue;
        }

        /* Pack raw bytes into uint32 based on data type */
        switch (reg->data_type)
        {
        case REG_TYPE_FLOAT:
        case REG_TYPE_UINT32:
        case REG_TYPE_LONG_INVERSE:
            /* 2 registers = 4 bytes, big-endian word order */
            pkt.readings[r].raw = ((uint32_t)buf[0] << 24)
                                | ((uint32_t)buf[1] << 16)
                                | ((uint32_t)buf[2] <<  8)
                                |  (uint32_t)buf[3];
            break;

        case REG_TYPE_UINT16:
            /* 1 register = 2 bytes — store in lower 16 bits */
            pkt.readings[r].raw = ((uint32_t)buf[0] << 8)
                                |  (uint32_t)buf[1];
            break;

        default:
            pkt.readings[r].raw = 0;
            break;
        }
    }

    pkt.errorcode = SUCCESS;
    led_notify_tx_activity();
    ble_send_modbus_reading(&pkt);


    /* Inter-device delay */
    vTaskDelay(pdMS_TO_TICKS(MODBUS_INTER_DEVICE_DELAY_MS));
}

/*===========================================================
 * Poll task — round-robin across all active devices
 *===========================================================*/
static void modbus_poll_task(void *arg)
{
    ESP_LOGI(MODBUS_POLL_TAG, "Poll task started — period=%ds", publishPeriod);

    /* Wait one full period before first poll */
    vTaskDelay(pdMS_TO_TICKS((uint32_t)publishPeriod * 1000));

    for (;;)
    {
        uint32_t slot_ms    = ((uint32_t)publishPeriod * 1000) / MAX_DEVICES_ON_BUS;
        uint32_t cycle_start_ticks = xTaskGetTickCount();

        ESP_LOGI(MODBUS_POLL_TAG,
                 "Poll cycle start — %d active device(s) slot_ms=%lu",
                 s_registry.count, slot_ms);

        for (uint8_t i = 0; i < MAX_DEVICES_ON_BUS; i++)
        {
            if (!s_registry.devices[i].active)
                continue;

            /* Calculate when this slot should fire */
            uint32_t target_ms  = (slot_ms * i);
            uint32_t elapsed_ms = (xTaskGetTickCount() - cycle_start_ticks)
                                  * portTICK_PERIOD_MS;

            if (target_ms > elapsed_ms)
            {
                uint32_t wait_ms = target_ms - elapsed_ms;

                /* Add jitter ±(slot_ms/4) using esp_random() */
                uint32_t jitter_range = slot_ms / 2;
                int32_t  jitter_ms    = (int32_t)(esp_random() % jitter_range)
                                        - (int32_t)(jitter_range / 2);

                int32_t final_wait = (int32_t)wait_ms + jitter_ms;
                if (final_wait > 0)
                    vTaskDelay(pdMS_TO_TICKS((uint32_t)final_wait));
            }

            poll_single_device(&s_registry.devices[i]);
        }

        /* Sleep for remainder of publish period */
        uint32_t elapsed_ms = (xTaskGetTickCount() - cycle_start_ticks)
                              * portTICK_PERIOD_MS;
        uint32_t period_ms  = (uint32_t)publishPeriod * 1000;

        if (elapsed_ms < period_ms)
            vTaskDelay(pdMS_TO_TICKS(period_ms - elapsed_ms));
    }
}

/*===========================================================
 * Public API — registry management
 *===========================================================*/
error_codes modbus_registry_add_device(const MeterConfigCmd *cmd)
{
    if (cmd->slave_addr == 0 || cmd->slave_addr > 247)
        return MODBUS_SLAVE_ADDR_INVALID;

    /* Check if already exists — update in place */
    for (uint8_t i = 0; i < MAX_DEVICES_ON_BUS; i++)
    {
        if (s_registry.devices[i].active &&
            s_registry.devices[i].slave_addr == cmd->slave_addr)
        {
            /* ADD rejected — device already exists, must use UPDATE */
            if (cmd->action == METER_ACTION_ADD)
            {
                ESP_LOGW(MODBUS_POLL_TAG,
                         "slave=0x%02x already exists — use UPDATE action",
                         cmd->slave_addr);
                return MODBUS_DEVICE_ALREADY_EXISTS;
            }

            /* UPDATE — overwrite register map and label */
            s_registry.devices[i].reg_count = cmd->reg_count;
            memcpy(s_registry.devices[i].regs, cmd->regs,
                   sizeof(register_entry_t) * cmd->reg_count);
            strncpy(s_registry.devices[i].label, cmd->label,
                    DEVICE_LABEL_LEN - 1);
            s_registry.devices[i].label[DEVICE_LABEL_LEN - 1] = '\0';
            registry_persist();
            ESP_LOGI(MODBUS_POLL_TAG,
                     "Updated slave=0x%02x regs=%d label='%s'",
                     cmd->slave_addr, cmd->reg_count, cmd->label);
            return SUCCESS;
        }
    }

    /* If no slave addr is found | Find empty slot */
    for (uint8_t i = 0; i < MAX_DEVICES_ON_BUS; i++)
    {
        if (!s_registry.devices[i].active)
        {
            s_registry.devices[i].slave_addr = cmd->slave_addr;
            s_registry.devices[i].active     = true;
            strncpy(s_registry.devices[i].label, cmd->label,
                    DEVICE_LABEL_LEN - 1);
            s_registry.devices[i].label[DEVICE_LABEL_LEN - 1] = '\0';
            s_registry.devices[i].reg_count = cmd->reg_count;
            memcpy(s_registry.devices[i].regs, cmd->regs, sizeof(register_entry_t) * cmd->reg_count); // Copy all the parameter's data to the device regitry entry
            s_registry.count++;

            registry_persist();
            ESP_LOGI(MODBUS_POLL_TAG,
                     "Added device slave=0x%02x label='%s' "
                     "total=%d",
                     cmd->slave_addr, cmd->label,
                     s_registry.count);
            return SUCCESS;
        }
    }

    ESP_LOGE(MODBUS_POLL_TAG, "Registry full — max %d devices",
             MAX_DEVICES_ON_BUS);
    return MODBUS_REGISTRY_FULL;
}

error_codes modbus_registry_remove_device(uint8_t slave_addr)
{
    for (uint8_t i = 0; i < MAX_DEVICES_ON_BUS; i++)
    {
        if (s_registry.devices[i].active &&
            s_registry.devices[i].slave_addr == slave_addr)
        {
            memset(&s_registry.devices[i], 0, sizeof(device_descriptor_t));
            s_registry.count--;
            registry_persist();
            ESP_LOGI(MODBUS_POLL_TAG,
                     "Removed device slave=0x%02x total=%d",
                     slave_addr, s_registry.count);
            return SUCCESS;
        }
    }
    ESP_LOGW(MODBUS_POLL_TAG,
             "Device slave=0x%02x not found in registry", slave_addr);
    return MODBUS_DEVICE_NOT_FOUND;
}

uint8_t modbus_registry_get_count(void)
{
    return s_registry.count;
}

error_codes modbus_registry_add_suman_ac(const SumanAcConfigCmd *cmd)
{
    if (cmd->slave_addr == 0 || cmd->slave_addr > 247)
        return MODBUS_SLAVE_ADDR_INVALID;

    /* Check if already exists */
    for (uint8_t i = 0; i < MAX_DEVICES_ON_BUS; i++)
    {
        if (s_registry.devices[i].active &&
            s_registry.devices[i].slave_addr == cmd->slave_addr)
        {
            if (cmd->action == METER_ACTION_ADD)
            {
                ESP_LOGW(MODBUS_POLL_TAG,
                         "Suman AC slave=0x%02x already exists — use UPDATE",
                         cmd->slave_addr);
                return MODBUS_DEVICE_ALREADY_EXISTS;
            }

            /* UPDATE — overwrite label only, no register map */
            s_registry.devices[i].device_type = DEVICE_TYPE_SUMAN_AC;
            strncpy(s_registry.devices[i].label, cmd->label,
                    DEVICE_LABEL_LEN - 1);
            s_registry.devices[i].label[DEVICE_LABEL_LEN - 1] = '\0';
            s_registry.devices[i].reg_count = 0;
            registry_persist();
            ESP_LOGI(MODBUS_POLL_TAG,
                     "Updated Suman AC slave=0x%02x label='%s'",
                     cmd->slave_addr, cmd->label);
            return SUCCESS;
        }
    }

    /* Find empty slot */
    for (uint8_t i = 0; i < MAX_DEVICES_ON_BUS; i++)
    {
        if (!s_registry.devices[i].active)
        {
            s_registry.devices[i].slave_addr  = cmd->slave_addr;
            s_registry.devices[i].active      = true;
            s_registry.devices[i].device_type = DEVICE_TYPE_SUMAN_AC;
            s_registry.devices[i].reg_count   = 0;
            s_registry.devices[i].last_fault_reset_active = 0;
            strncpy(s_registry.devices[i].label, cmd->label,
                    DEVICE_LABEL_LEN - 1);
            s_registry.devices[i].label[DEVICE_LABEL_LEN - 1] = '\0';
            s_registry.count++;
            registry_persist();
            ESP_LOGI(MODBUS_POLL_TAG,
                     "Added Suman AC slave=0x%02x label='%s' total=%d",
                     cmd->slave_addr, cmd->label, s_registry.count);
            return SUCCESS;
        }
    }

    ESP_LOGE(MODBUS_POLL_TAG, "Registry full — max %d devices",
             MAX_DEVICES_ON_BUS);
    return MODBUS_REGISTRY_FULL;
}

device_descriptor_t *modbus_registry_find_device(uint8_t slave_addr)
{
    for (uint8_t i = 0; i < MAX_DEVICES_ON_BUS; i++)
    {
        if (s_registry.devices[i].active &&
            s_registry.devices[i].slave_addr == slave_addr)
            return &s_registry.devices[i];
    }
    return NULL;
}

/*===========================================================
 * Public API — init
 *===========================================================*/
void modbus_poll_init(void)
{
    registry_load();

    esp_err_t err = modbus_master_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(MODBUS_POLL_TAG,
                 "modbus_master_init failed — poll task not started");
        led_notify_fault(true);
        return;
    }

    BaseType_t ret = xTaskCreate(modbus_poll_task,
                                 "Modbus Poll Task",
                                 MODBUS_TASK_STACK_SIZE,
                                 NULL, 3, NULL);
    if (ret != pdPASS)
    {
        ESP_LOGE(MODBUS_POLL_TAG, "Failed to create poll task");
        led_notify_fault(true);
    }
}
