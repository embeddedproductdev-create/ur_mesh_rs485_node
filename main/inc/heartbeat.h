#ifndef HEARTBEAT_H
#define HEARTBEAT_H

/**
 * @brief Initialise the heartbeat esp_timer (does not start it).
 *        Must be called from app_main after nvs_init() and ble_init().
 *        Starts the timer automatically if node is already provisioned.
 */
void hb_init(void);

/**
 * @brief Start the periodic heartbeat timer using current publishPeriod.
 *        No-op if timer is already running.
 */
void hb_timer_start(void);

/**
 * @brief Stop the heartbeat timer.
 *        No-op if timer is not running.
 */
void hb_timer_stop(void);

/**
 * @brief Restart the heartbeat timer with the current publishPeriod.
 *        Called after publishPeriod is updated from a cloud command.
 *        Falls back to hb_timer_start() if timer was not running.
 */
void hb_timer_restart(void);

/**
 * @brief Handle a NODE_HEARTBEAT_PUB_CONF_PACKET from the gateway.
 *        Updates publishPeriod, persists to NVS, restarts the timer.
 *        Also restarts the modbus poll task interval.
 *
 * @param new_period_sec  New publish period in seconds (must be > 0)
 */
void handle_setting_hb_publish_configuration(uint16_t new_period_sec);

#endif /* HEARTBEAT_H */
