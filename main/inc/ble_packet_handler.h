#ifndef BLE_PACKET_HANDLER_H
#define BLE_PACKET_HANDLER_H

#include <common_types.h>

/*===========================================================
 * Entry point — called from ble_mesh_node.c on every
 * incoming VND_MODEL_OP_SEND message from the provisioner.
 *===========================================================*/

/**
 * @brief Dispatch an incoming CommandStruct from the gateway.
 *
 * Validates the packet, routes to the correct handler by packetid,
 * and sends the appropriate ACK back via ble_mesh_node.c send helpers.
 */
void handle_cmds_from_provisioner(uint8_t *msg, uint16_t len);

/*===========================================================
 * Provisioning lifecycle callbacks
 * Called from ble_mesh_node.c provisioning/config-server callbacks.
 *===========================================================*/

/**
 * @brief Called when app-key binding completes (provisioning done).
 *        Persists provisioned flag, updates LED, sends prov ACK.
 */
void provision_success_cb(void);

/**
 * @brief Called when an unprovision command is successfully processed.
 *        Resets BLE mesh node, clears flash, updates LED.
 */
void unprovision_success_cb(void);

#endif /* BLE_PACKET_HANDLER_H */
