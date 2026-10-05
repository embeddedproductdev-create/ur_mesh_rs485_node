#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"

#include <main.h>
#include <common_types.h>
#include <ble_mesh_node.h>
#include <ble_packet_handler.h>
#include <led.h>

/*===========================================================
 * Module state
 *===========================================================*/
bool ble_initialized = false;

static uint8_t dev_uuid[ESP_BLE_MESH_OCTET16_LEN] = { 0x32, 0x10 };  /* [2-7]=MAC filled at runtime, [8]=node type */

static QueueHandle_t s_publish_queue = NULL;

static void ble_publish_task(void *arg);

/*===========================================================
 * BLE Mesh model and composition definitions
 *===========================================================*/
static esp_ble_mesh_cfg_srv_t config_server = {
    .relay        = ESP_BLE_MESH_RELAY_DISABLED,
    .beacon       = ESP_BLE_MESH_BEACON_ENABLED,
#if defined(CONFIG_BLE_MESH_FRIEND)
    .friend_state = ESP_BLE_MESH_FRIEND_ENABLED,
#else
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
#endif
#if defined(CONFIG_BLE_MESH_GATT_PROXY_SERVER)
    .gatt_proxy   = ESP_BLE_MESH_GATT_PROXY_ENABLED,
#else
    .gatt_proxy   = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
#endif
    .default_ttl       = 7,
    .net_transmit      = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay_retransmit  = ESP_BLE_MESH_TRANSMIT(2, 20),
};

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
};

static esp_ble_mesh_model_op_t vnd_op[] = {
    ESP_BLE_MESH_MODEL_OP(ESP_BLE_MESH_VND_MODEL_OP_SEND, 2),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_t vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(CID_ESP, ESP_BLE_MESH_VND_MODEL_ID_SERVER,
                              vnd_op, NULL, NULL),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, vnd_models),
};

static esp_ble_mesh_comp_t composition = {
    .cid           = CID_ESP,
    .elements      = elements,
    .element_count = ARRAY_SIZE(elements),
};

static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,
};

/*===========================================================
 * Internal helpers — mesh send context
 *===========================================================*/
static esp_ble_mesh_msg_ctx_t make_ctx(void)
{
    esp_ble_mesh_msg_ctx_t ctx = {
        .addr     = BLE_MESH_PROVISIONER_ADDR,
        .app_idx  = BLE_MESH_APP_IDX,
        .net_idx  = BLE_MESH_NET_IDX,
        .send_ttl = BLE_MESH_SEND_TTL,
        .send_rel = false,
    };
    return ctx;
}

/*===========================================================
 * BLE Mesh callbacks
 *===========================================================*/
static void prov_complete(uint16_t net_idx, uint16_t addr,
                          uint8_t flags, uint32_t iv_index)
{
    ESP_LOGI(BLE_TAG, "Prov complete — net_idx 0x%03x addr 0x%04x", net_idx, addr);
    ESP_LOGI(BLE_TAG, "flags 0x%02x iv_index 0x%08" PRIx32, flags, iv_index);
    attach_elemAddr_to_structures(addr);
}

static void ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                                     esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event)
    {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(BLE_TAG, "PROV_REGISTER_COMP err=%d",
                 param->prov_register_comp.err_code);
        break;

    case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(BLE_TAG, "NODE_PROV_ENABLE_COMP err=%d",
                 param->node_prov_enable_comp.err_code);
        /* Advertising — signal LED */
        led_set_state(LED_STATE_UNPROVISIONED);
        break;

    case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
        ESP_LOGI(BLE_TAG, "NODE_PROV_LINK_OPEN bearer=%s",
                 param->node_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV
                     ? "PB-ADV" : "PB-GATT");
        led_set_state(LED_STATE_PROVISIONING);
        break;

    case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT:
        ESP_LOGI(BLE_TAG, "NODE_PROV_LINK_CLOSE bearer=%s",
                 param->node_prov_link_close.bearer == ESP_BLE_MESH_PROV_ADV
                     ? "PB-ADV" : "PB-GATT");
        /* Link closed without completion → back to advertising */
        if (!ble_initialized)
            led_set_state(LED_STATE_UNPROVISIONED);
        break;

    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        ESP_LOGI(BLE_TAG, "NODE_PROV_COMPLETE");
        prov_complete(param->node_prov_complete.net_idx,
                      param->node_prov_complete.addr,
                      param->node_prov_complete.flags,
                      param->node_prov_complete.iv_index);
        break;

    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        ESP_LOGI(BLE_TAG, "NODE_PROV_RESET");
        led_set_state(LED_STATE_UNPROVISIONED);
        break;

    case ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT:
        ESP_LOGI(BLE_TAG, "SET_UNPROV_DEV_NAME_COMP err=%d",
                 param->node_set_unprov_dev_name_comp.err_code);
        break;

    default:
        break;
    }
}

static void ble_mesh_config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                                      esp_ble_mesh_cfg_server_cb_param_t *param)
{
    if (event != ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT)
        return;

    switch (param->ctx.recv_op)
    {
    case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
        ESP_LOGI(BLE_TAG, "APP_KEY_ADD net_idx=0x%04x app_idx=0x%04x",
                 param->value.state_change.appkey_add.net_idx,
                 param->value.state_change.appkey_add.app_idx);
        ESP_LOG_BUFFER_HEX("AppKey",
                 param->value.state_change.appkey_add.app_key, 16);
        break;

    case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
        ESP_LOGI(BLE_TAG, "MODEL_APP_BIND elem=0x%04x app_idx=0x%04x "
                           "cid=0x%04x mod_id=0x%04x",
                 param->value.state_change.mod_app_bind.element_addr,
                 param->value.state_change.mod_app_bind.app_idx,
                 param->value.state_change.mod_app_bind.company_id,
                 param->value.state_change.mod_app_bind.model_id);
        /* App-key binding complete = provisioning fully done */
        provision_success_cb();
        break;

    default:
        break;
    }
}

static void ble_mesh_custom_model_cb(esp_ble_mesh_model_cb_event_t event,
                                     esp_ble_mesh_model_cb_param_t *param)
{
    switch (event)
    {
    case ESP_BLE_MESH_MODEL_OPERATION_EVT:
        if (param->model_operation.opcode == ESP_BLE_MESH_VND_MODEL_OP_SEND)
        {
            ESP_LOGI(BLE_TAG, "Recv opcode=0x%06" PRIx32,
                     param->model_operation.opcode);
            /* Hand raw message bytes up to the packet handler layer */
            handle_cmds_from_provisioner(param->model_operation.msg,
                              param->model_operation.length);
        }
        break;

    case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:
        if (param->model_send_comp.err_code)
            ESP_LOGE(BLE_TAG, "Send failed opcode=0x%06" PRIx32,
                     param->model_send_comp.opcode);
        break;

    case ESP_BLE_MESH_CLIENT_MODEL_RECV_PUBLISH_MSG_EVT:
        ESP_LOGI(BLE_TAG, "Publish msg opcode=0x%06" PRIx32,
                 param->client_recv_publish_msg.opcode);
        break;

    default:
        break;
    }
}

/*===========================================================
 * Stack init
 *===========================================================*/
static esp_err_t ble_mesh_init(void)
{
    esp_ble_mesh_register_prov_callback(ble_mesh_provisioning_cb);
    esp_ble_mesh_register_config_server_callback(ble_mesh_config_server_cb);
    esp_ble_mesh_register_custom_model_callback(ble_mesh_custom_model_cb);

    esp_err_t err = esp_ble_mesh_init(&provision, &composition);
    if (err != ESP_OK)
    {
        ESP_LOGE(BLE_TAG, "Mesh stack init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_ble_mesh_node_prov_enable(
              ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
    if (err != ESP_OK)
    {
        ESP_LOGE(BLE_TAG, "Node prov enable failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(BLE_TAG, "BLE Mesh node initialised");
    return ESP_OK;
}


void ble_init(void)
{
    esp_err_t err;

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    err = bluetooth_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(BLE_TAG, "bluetooth_init failed: %s", esp_err_to_name(err));
        return;
    }

    memcpy(&dev_uuid[2], esp_bt_dev_get_address(), BD_ADDR_LEN);  /* MAC at bytes 2-7 */
    dev_uuid[8] = NODE_TYPE_RS485;  

    s_publish_queue = xQueueCreate(BLE_PUBLISH_QUEUE_DEPTH,
                                sizeof(publish_queue_item_t));
    xTaskCreate(ble_publish_task, "BLE Publish",
                BLE_PUBLISH_TASK_STACK, NULL, 5, NULL);

    err = ble_mesh_init();
    if (err != ESP_OK)
        ESP_LOGE(BLE_TAG, "ble_mesh_init failed: %s", esp_err_to_name(err));
    else
        ble_initialized = true;
}

/*===========================================================
 * Unicast address attachment
 *===========================================================*/
void attach_elemAddr_to_structures(uint16_t elemAddr)
{
    extern RS485CmdStruct last_command;
    extern uint16_t      elemaddr_global;
    last_command.elemaddr = elemAddr;
    elemaddr_global       = elemAddr;
    ESP_LOGI(BLE_TAG, "Element address set: 0x%04x", elemAddr);
}

static void ble_publish_task(void *arg)
{
    publish_queue_item_t item;

    for (;;)
    {
        if (xQueueReceive(s_publish_queue, &item, portMAX_DELAY) != pdTRUE)
            continue;

        esp_ble_mesh_msg_ctx_t ctx = make_ctx();
        uint8_t  *data = NULL;
        uint16_t  len  = 0;

        switch (item.type)
        {
        case PUBLISH_PKT_CMD_ACK:
            data = (uint8_t *)&item.payload.cmd_ack;
            len  = sizeof(RS485CmdStruct);
            break;
        case PUBLISH_PKT_MODBUS_READING:
            data = (uint8_t *)&item.payload.modbus_reading;
            len  = sizeof(ModbusReadingPkt);
            break;
        case PUBLISH_PKT_GPIO_STATUS:
            data = (uint8_t *)&item.payload.gpio_status;
            len  = sizeof(GpioStatusPkt);
            break;
        case PUBLISH_PKT_SUMAN_AC:
            data = (uint8_t *)&item.payload.suman_ac;
            len  = sizeof(SumanAcStatusPkt);
            break;
        case PUBLISH_PKT_SUMAN_AC_CONFIG_ACK:
            data = (uint8_t *)&item.payload.suman_ac_config_ack;
            len  = sizeof(SumanAcConfigAck);
            break;
        case PUBLISH_PKT_SUMAN_AC_CONTROL_ACK:
            data = (uint8_t *)&item.payload.suman_ac_control_ack;
            len  = sizeof(SumanAcControlAck);
            break;
        default:
            ESP_LOGW(BLE_TAG, "Unknown publish pkt type %d", item.type);
            continue;
        }

        /* Retry loop */
        esp_err_t err      = ESP_FAIL;
        uint8_t   attempts = 0;

        while (attempts < BLE_PUBLISH_RETRY_COUNT)
        {
            err = esp_ble_mesh_server_model_send_msg(
                      &vnd_models[0], &ctx,
                      ESP_BLE_MESH_VND_MODEL_OP_STATUS,
                      len, data);

            if (err == ESP_OK)
            {
                ESP_LOGD(BLE_TAG, "Sent pkt type=%d attempt=%d",
                         item.type, attempts + 1);
                break;
            }

            /* Permanent failure — no point retrying */
            if (err == ESP_ERR_INVALID_ARG)
            {
                ESP_LOGE(BLE_TAG,
                         "Pkt type=%d invalid arg — dropping",
                         item.type);
                break;
            }

            attempts++;
            ESP_LOGW(BLE_TAG,
                     "Pkt type=%d send failed err=%s attempt=%d/%d — retrying",
                     item.type, esp_err_to_name(err),
                     attempts, BLE_PUBLISH_RETRY_COUNT);

            vTaskDelay(pdMS_TO_TICKS(BLE_PUBLISH_RETRY_DELAY_MS));
        }

        if (err != ESP_OK && err != ESP_ERR_INVALID_ARG)
            ESP_LOGE(BLE_TAG,
                     "Pkt type=%d dropped after %d attempts err=%s",
                     item.type, BLE_PUBLISH_RETRY_COUNT,
                     esp_err_to_name(err));

        /* Inter-packet delay — give BLE stack time to process segments */
        vTaskDelay(pdMS_TO_TICKS(BLE_PUBLISH_INTER_PKT_MS));
    }
}

bool ble_publish_enqueue(publish_pkt_type_t type, const void *pkt)
{
    publish_queue_item_t item = {0};
    item.type = type;

    switch (type)
    {
    case PUBLISH_PKT_CMD_ACK:
        memcpy(&item.payload.cmd_ack, pkt, sizeof(RS485CmdStruct));
        break;
    case PUBLISH_PKT_MODBUS_READING:
        memcpy(&item.payload.modbus_reading, pkt, sizeof(ModbusReadingPkt));
        break;
    case PUBLISH_PKT_GPIO_STATUS:
        memcpy(&item.payload.gpio_status, pkt, sizeof(GpioStatusPkt));
        break;
    case PUBLISH_PKT_SUMAN_AC:
        memcpy(&item.payload.suman_ac, pkt, sizeof(SumanAcStatusPkt));
        break;
    case PUBLISH_PKT_SUMAN_AC_CONFIG_ACK:
        memcpy(&item.payload.suman_ac_config_ack, pkt, sizeof(SumanAcConfigAck));
        break;
    case PUBLISH_PKT_SUMAN_AC_CONTROL_ACK:
        memcpy(&item.payload.suman_ac_control_ack, pkt, sizeof(SumanAcControlAck));
        break;
    default:
        return false;
    }

    if (xQueueSend(s_publish_queue, &item, 0) != pdTRUE)
    {
        ESP_LOGW(BLE_TAG, "Publish queue full — dropping pkt type=%d", type);
        return false;
    }
    return true;
}

void ble_send_command_ack(RS485CmdStruct *ack)
{
    ble_publish_enqueue(PUBLISH_PKT_CMD_ACK, ack);
}

void ble_send_modbus_reading(ModbusReadingPkt *pkt)
{
    ble_publish_enqueue(PUBLISH_PKT_MODBUS_READING, pkt);
}

void ble_send_gpio_status(GpioStatusPkt *pkt)
{
    ble_publish_enqueue(PUBLISH_PKT_GPIO_STATUS, pkt);
}

void ble_send_suman_ac_status(SumanAcStatusPkt *pkt)
{
    ble_publish_enqueue(PUBLISH_PKT_SUMAN_AC, pkt);
}

void ble_send_suman_ac_config_ack(SumanAcConfigAck *pkt)
{
    ble_publish_enqueue(PUBLISH_PKT_SUMAN_AC_CONFIG_ACK, pkt);
}

void ble_send_suman_ac_control_ack(SumanAcControlAck *pkt)
{
    ble_publish_enqueue(PUBLISH_PKT_SUMAN_AC_CONTROL_ACK, pkt);
}