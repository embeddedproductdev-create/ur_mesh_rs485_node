#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"

#include <main.h>
#include <common_types.h>
#include <flash.h>
#include <led.h>
#include <gpio_subsystem.h>
#include <ble_mesh_node.h>
#include <ble_packet_handler.h>
#include <heartbeat.h>
#include <modbus/modbus_poll_task.h>
#include <modbus/drivers/drv_suman_ac.h>

#define PKT_TAG "PKT"

/*===========================================================
 * Externals from main.c
 *===========================================================*/
extern char     serialNoStr[SERIAL_NO_LEN];
extern uint8_t  provisioned;
extern uint8_t  configured;
extern uint16_t publishPeriod;
extern bool     fault_active;

/*===========================================================
 * Shared last-command state (also used by heartbeat, future modules)
 *===========================================================*/
RS485CmdStruct last_command = {0};

/*===========================================================
 * Internal helpers
 *===========================================================*/

/** Populate the common fields every outgoing ACK needs. */
static void fill_ack_common(RS485CmdStruct *ack, uint16_t packetid,
                              error_codes errorcode)
 {
     ack->packetid    = packetid;
     ack->errorcode   = errorcode;
    ack->node_type   = NODE_TYPE_RS485;
     strncpy(ack->deviceName, serialNoStr, SERIAL_NO_LEN - 1);
     ack->deviceName[SERIAL_NO_LEN - 1] = '\0';
 }

/*===========================================================
 * Individual packet handlers
 * Each handler owns exactly one packet type's logic:
 *   - validate beyond the common error check
 *   - act (NVS, GPIO, restart, etc.)
 *   - build and send ACK
 *===========================================================*/

static void handle_unprovision(RS485CmdStruct *cmd)
{
    ESP_LOGI(PKT_TAG, "Handling NODE_UNPROV_PACKET");
    fill_ack_common(cmd, NODE_UNPROV_PACKET, SUCCESS);
    cmd->elemaddr = last_command.elemaddr;
    ble_send_command_ack(cmd);
    unprovision_success_cb();
}

static void handle_heartbeat_pub_conf(RS485CmdStruct *cmd)
{
    ESP_LOGI(PKT_TAG, "Handling NODE_HEARTBEAT_PUB_CONF_PACKET — period=%ds",
             cmd->publishPeriodSec);

    if (cmd->publishPeriodSec == 0)
    {
        cmd->errorcode = PUBLISH_PERIOD_EXCEEDING_RANGE;
        fill_ack_common(cmd, NODE_HEARTBEAT_PUB_CONF_PACKET, cmd->errorcode);
        ble_send_command_ack(cmd);
        return;
    }

    /* Delegate to heartbeat module — updates publishPeriod, NVS, timer */
    handle_setting_hb_publish_configuration(cmd->publishPeriodSec);

    fill_ack_common(cmd, NODE_HEARTBEAT_PUB_CONF_PACKET, SUCCESS);
    ble_send_command_ack(cmd);
}

static void handle_debug_info(RS485CmdStruct *cmd)
{
    ESP_LOGI(PKT_TAG, "Handling NODE_DEBUG_INFO_PACKET");

    if (cmd->resetDevice)
    {
        cmd->errorcode = factory_reset_device();
        fill_ack_common(cmd, NODE_DEBUG_INFO_PACKET, cmd->errorcode);
        cmd->majversion   = MAJ_VERSION;
        cmd->minversion   = MIN_VERSION;
        cmd->patchversion = PATCH_VERSION;
        ble_send_command_ack(cmd);
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
        return;
    }

    if (cmd->restartDevice)
    {
        fill_ack_common(cmd, NODE_DEBUG_INFO_PACKET, SUCCESS);
        ble_send_command_ack(cmd);
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
        return;
    }

    /* Debug info read */
    fill_ack_common(cmd, NODE_DEBUG_INFO_PACKET, SUCCESS);
    cmd->majversion       = MAJ_VERSION;
    cmd->minversion       = MIN_VERSION;
    cmd->patchversion     = PATCH_VERSION;
    cmd->publishPeriodSec = publishPeriod;
    cmd->deviceUpTimeHrs  = (xTaskGetTickCount() * portTICK_PERIOD_MS)
                            / 3600000.0f;
    cmd->provisioned      = provisioned;
    cmd->configured       = configured;
    ble_send_command_ack(cmd);
}

static void handle_meter_config(uint8_t *msg)
 {
    ESP_LOGI(PKT_TAG, "Handling NODE_METER_CONFIG_PACKET");
    MeterConfigCmd *meter_cmd = (MeterConfigCmd *)msg;
    error_codes result;

    switch (meter_cmd->action)
    {
    case METER_ACTION_ADD:
    case METER_ACTION_UPDATE:
        result = modbus_registry_add_device(meter_cmd);
        break;
    case METER_ACTION_REMOVE:
        result = modbus_registry_remove_device(meter_cmd->slave_addr);
        break;
    default:
        result = FAILURE;
        break;
    }

    RS485CmdStruct ack = {0};
    fill_ack_common(&ack, NODE_METER_CONFIG_ACK, result);
    ack.elemaddr = last_command.elemaddr;
    ack.msgseqno = meter_cmd->msgseqno;
    led_notify_msg_recvd();
    ble_send_command_ack(&ack);
 }

static void handle_gpio_config(uint8_t *msg)
 {
    GpioConfigCmd *gpio_cmd = (GpioConfigCmd *)msg;
    error_codes result = gpio_handle_config_cmd(gpio_cmd);

    RS485CmdStruct ack = {0};
    fill_ack_common(&ack, NODE_GPIO_CONFIG_ACK, result);
    ack.elemaddr  = last_command.elemaddr;
    ack.msgseqno  = gpio_cmd->msgseqno;
    led_notify_msg_recvd();
    ble_send_command_ack(&ack);
 }

static void handle_gpio_set(uint8_t *msg)
 {
    GpioSetCmd *gpio_cmd = (GpioSetCmd *)msg;
    error_codes result = gpio_handle_set_cmd(gpio_cmd);

    RS485CmdStruct ack = {0};
    fill_ack_common(&ack, NODE_GPIO_SET_ACK, result);
    ack.elemaddr = last_command.elemaddr;
    ack.msgseqno = gpio_cmd->msgseqno;

    if (result == SUCCESS)
    {
        /* Push updated GPIO status after a successful set */
        GpioStatusPkt status = {0};
        gpio_build_status_pkt(&status);
        status.elemaddr = last_command.elemaddr;
        status.msgseqno = gpio_cmd->msgseqno;
        led_notify_tx_activity();
        ble_send_gpio_status(&status);
    }
    else
    {
        led_notify_msg_recvd();
        ble_send_command_ack(&ack);
    }
}

static void handle_suman_ac_config(uint8_t *msg)
{
    SumanAcConfigCmd *cmd = (SumanAcConfigCmd *)msg;
    error_codes result;

    switch (cmd->action)
    {
    case METER_ACTION_ADD:
    case METER_ACTION_UPDATE:
        result = modbus_registry_add_suman_ac(cmd);
        break;
    case METER_ACTION_REMOVE:
        result = modbus_registry_remove_device(cmd->slave_addr);
        break;
    default:
        result = FAILURE;
        break;
    }

    SumanAcConfigAck ack = {0};
    ack.packetid     = NODE_SUMAN_AC_CONFIG_ACK;
    ack.msgseqno     = cmd->msgseqno;
    ack.elemaddr     = esp_ble_mesh_get_primary_element_address();
    ack.node_type    = NODE_TYPE_RS485;
    ack.slave_addr   = cmd->slave_addr;
    ack.action       = cmd->action;
    ack.total_devices = modbus_registry_get_count();
    ack.errorcode    = (int8_t)result;
    strncpy(ack.label, cmd->label, DEVICE_LABEL_LEN - 1);
    ack.label[DEVICE_LABEL_LEN - 1] = '\0';
    strncpy(ack.deviceName, serialNoStr, SERIAL_NO_LEN - 1);
    ack.deviceName[SERIAL_NO_LEN - 1] = '\0';

    led_notify_msg_recvd();
    ble_send_suman_ac_config_ack(&ack);
}

static void handle_suman_ac_control(uint8_t *msg)
{
    SumanAcControlCmd *cmd = (SumanAcControlCmd *)msg;

    /* Find device in registry */
    device_descriptor_t *dev = modbus_registry_find_device(cmd->slave_addr);

    SumanAcControlAck ack = {0};
    ack.packetid   = NODE_SUMAN_AC_CONTROL_ACK;
    ack.msgseqno   = cmd->msgseqno;
    ack.elemaddr   = esp_ble_mesh_get_primary_element_address();
    ack.node_type  = NODE_TYPE_RS485;
    ack.slave_addr = cmd->slave_addr;
    ack.write_mask = cmd->write_mask;
    strncpy(ack.deviceName, serialNoStr, SERIAL_NO_LEN - 1);
    ack.deviceName[SERIAL_NO_LEN - 1] = '\0';

    if (dev == NULL || dev->device_type != DEVICE_TYPE_SUMAN_AC)
    {
        ESP_LOGE(PKT_TAG, "Suman AC slave=0x%02x not found",
                 cmd->slave_addr);
        ack.errorcode = (int8_t)MODBUS_DEVICE_NOT_FOUND;
        ble_send_suman_ac_control_ack(&ack);
        return;
    }

    /* Execute write */
    error_codes result = drv_suman_ac_write(cmd->slave_addr,
                                            cmd,
                                            dev->last_fault_reset_active);

    /* Read back full state after write */
    SumanAcStatusPkt status = {0};
    if (result == SUCCESS)
        drv_suman_ac_read(cmd->slave_addr, &status);

    /* Populate ACK with current state */
    ack.errorcode           = (int8_t)result;
    ack.pc_set_temp         = status.pc_set_temp;
    ack.pc_operating_mode   = status.pc_operating_mode;
    ack.control_option      = status.control_option;
    ack.handset_ctrl_time   = status.handset_ctrl_time;
    ack.ctrl_set_temp       = status.ctrl_set_temp;
    ack.ctrl_operating_mode = status.ctrl_operating_mode;
    ack.fault_reset_active  = status.fault_reset_active;
    ack.return_air_temp     = status.return_air_temp;
    ack.compressor_status   = status.compressor_status;
    ack.fan_status          = status.fan_status;
    ack.alarm_status        = status.alarm_status;
    ack.system_fault        = status.system_fault;
    ack.sensor_fault        = status.sensor_fault;
    ack.comp1_fault         = status.comp1_fault;
    ack.comp2_fault         = status.comp2_fault;

    /* Update cached fault reset active */
    if (result == SUCCESS)
        dev->last_fault_reset_active = status.fault_reset_active;

    led_notify_msg_recvd();
    ble_send_suman_ac_control_ack(&ack);
}


/*===========================================================
 * Top-level dispatcher — called from ble_mesh_node.c
 *===========================================================*/
void handle_cmds_from_provisioner(uint8_t *msg, uint16_t len)
{
    if (msg == NULL || len < 2)
    {
        ESP_LOGE(PKT_TAG, "Invalid message");
        return;
    }

    /* Read packetid from offset 0 — valid for all RS485 packet structs */
    uint16_t packetid = (uint16_t)msg[0] | ((uint16_t)msg[1] << 8);
    ESP_LOGI(PKT_TAG, "Received packetid=%d len=%d", packetid, len);

    /* Valid ranges: shared (100-120) and RS485-specific (151-158) */
    bool valid = (packetid >= NODE_PROV_PACKET && packetid < MAX_NODE_PACKET_ID) ||
                 (packetid >= NODE_METER_CONFIG_PACKET && packetid < MAX_RS485_PACKET_ID);

    if (!valid)
    {
        ESP_LOGE(PKT_TAG, "packetid %d out of range", packetid);
        RS485CmdStruct ack = {0};
        fill_ack_common(&ack, NODE_GENERAL_PACKET, PACKET_ID_EXCEEDING_RANGE);
        ack.elemaddr = last_command.elemaddr;
        ble_send_command_ack(&ack);
        return;
    }

    led_notify_msg_recvd();

    /* For shared range — cast to RS485CmdStruct */
    RS485CmdStruct *cmd = (RS485CmdStruct *)msg;

    switch (packetid)
    {
    case NODE_UNPROV_PACKET:
        handle_unprovision(cmd);
        break;

    case NODE_HEARTBEAT_PUB_CONF_PACKET:
        handle_heartbeat_pub_conf(cmd);
        break;

    case NODE_DEBUG_INFO_PACKET:
        handle_debug_info(cmd);
        break;

    /* RS485-specific — pass raw bytes, each handler casts to correct struct */
    case NODE_METER_CONFIG_PACKET:
        handle_meter_config(msg);
        break;

    case NODE_GPIO_CONFIG_PACKET:
        handle_gpio_config(msg);
        break;
    case NODE_GPIO_SET_PACKET:
        handle_gpio_set(msg);
        break;

    case NODE_SUMAN_AC_CONFIG_PACKET:
        handle_suman_ac_config(msg);
        break;
    case NODE_SUMAN_AC_CONTROL_PACKET:
        handle_suman_ac_control(msg);
        break;

    default:
        ESP_LOGW(PKT_TAG, "Unhandled packetid=%d", cmd->packetid);
        RS485CmdStruct ack = {0};
        fill_ack_common(&ack, NODE_GENERAL_PACKET, MISSING_PACKET_ID);
        ack.elemaddr = last_command.elemaddr;
        ble_send_command_ack(&ack);
        break;
    }
}

/*===========================================================
 * Provisioning lifecycle
 *===========================================================*/
void provision_success_cb(void)
{
    ESP_LOGI(PKT_TAG, "Provision success");

    provisioned = 1;
    set_number_in_nvs_flash(GENERAL_HANDLE, NVS_PROVISIONED_KEY, 1, UINT8_SIZE);
    update_led_status();

    /* Send prov ACK to gateway */
    RS485CmdStruct ack = {0};
    fill_ack_common(&ack, NODE_PROV_PACKET, SUCCESS);
    ack.elemaddr = last_command.elemaddr;
    ble_send_command_ack(&ack);

    /* Start heartbeat timer now that node is provisioned */
    hb_timer_start();
}

void unprovision_success_cb(void)
{
    ESP_LOGW(PKT_TAG, "Unprovisioning node");

    provisioned = 0;
    configured  = 0;

    /* Stop BLE advertising and reset mesh state */
    esp_ble_mesh_node_local_reset();
    esp_ble_mesh_node_prov_disable(
        ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);

    /* Erase persisted data */
    factory_reset_device();

    /* Stop heartbeat timer */
    hb_timer_stop();

    update_led_status();
}
