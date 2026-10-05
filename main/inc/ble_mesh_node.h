#ifndef BLE_MESH_NODE_H
#define BLE_MESH_NODE_H

#include <stdbool.h>
#include "esp_err.h"
#include "esp_ble_mesh_defs.h"
#include <common_types.h>

#define BLE_TAG "BLE"

#define CID_ESP                             0x02E5
#define ESP_BLE_MESH_VND_MODEL_ID_SERVER    0x0001
#define ESP_BLE_MESH_VND_MODEL_OP_SEND      ESP_BLE_MESH_MODEL_OP_3(0x00, CID_ESP)
#define ESP_BLE_MESH_VND_MODEL_OP_STATUS    ESP_BLE_MESH_MODEL_OP_3(0x01, CID_ESP)

#define BLE_MESH_PROVISIONER_ADDR           0x0001
#define BLE_MESH_APP_IDX                    0x0000
#define BLE_MESH_NET_IDX                    0
#define BLE_MESH_SEND_TTL                   3

/*===========================================================
 * Global state
 *===========================================================*/
extern bool ble_initialized;

/*===========================================================
 * Stack init
 *===========================================================*/

/**
 * @brief Initialise Bluetooth controller, Bluedroid, and BLE Mesh stack.
 *        Registers all callbacks and enables node provisioning bearers.
 *        Must be called once from app_main after nvs_init().
 */
void      ble_init(void);
esp_err_t bluetooth_init(void);   /* Implemented in ble_mesh_node.c */

/*===========================================================
 * Unicast address attachment
 * Called from prov_complete() after provisioning succeeds.
 *===========================================================*/
void attach_elemAddr_to_structures(uint16_t elemAddr);

/* Enqueue any packet for BLE Mesh publish */
bool ble_publish_enqueue(publish_pkt_type_t type, const void *pkt);

/* Convenience wrappers — call sites stay clean */
#define ble_enqueue_cmd_ack(pkt)       ble_publish_enqueue(PUBLISH_PKT_CMD_ACK,        (pkt))
#define ble_enqueue_modbus(pkt)        ble_publish_enqueue(PUBLISH_PKT_MODBUS_READING,  (pkt))
#define ble_enqueue_gpio_status(pkt)   ble_publish_enqueue(PUBLISH_PKT_GPIO_STATUS,     (pkt))
#define ble_enqueue_suman_ac(pkt)      ble_publish_enqueue(PUBLISH_PKT_SUMAN_AC,        (pkt))
/*===========================================================
 * Raw mesh send functions
 * These only handle the transport layer — no packet logic here.
 * Packet fields must be populated by the caller before calling.````
 *===========================================================*/

/**
 * @brief Send a CommandStruct ACK to the provisioner/gateway.
 */
void ble_send_command_ack(RS485CmdStruct *ack);

/**
 * @brief Send a GPIO status packet to the provisioner/gateway.
 */
void ble_send_gpio_status(GpioStatusPkt *pkt);

void ble_send_modbus_reading(ModbusReadingPkt *pkt);

void ble_send_suman_ac_status(SumanAcStatusPkt *pkt);

void ble_send_suman_ac_config_ack(SumanAcConfigAck *pkt);

void ble_send_suman_ac_control_ack(SumanAcControlAck *pkt);
#endif /* BLE_MESH_NODE_H */
