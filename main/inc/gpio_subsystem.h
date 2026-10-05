#ifndef GPIO_SUBSYSTEM_H
#define GPIO_SUBSYSTEM_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include <common_types.h>

#define GPIO_SUBSYSTEM_TAG "GPIO"

/*===========================================================
 * Physical GPIO pin assignments for the 8 channels
 * TODO: Replace with actual hardware pin numbers before flashing
 *===========================================================*/
#warning "GPIO_SUBSYSTEM: Physical pin numbers not yet assigned. Update GPIO_CH_PINS before flashing."
static const gpio_num_t GPIO_CH_PINS[GPIO_CHANNEL_COUNT] = {
    GPIO_NUM_4,   /* Channel 0 — TODO: assign */
    GPIO_NUM_5,   /* Channel 1 — TODO: assign */
    GPIO_NUM_6,   /* Channel 2 — TODO: assign */
    GPIO_NUM_7,   /* Channel 3 — TODO: assign */
    GPIO_NUM_15,  /* Channel 4 — TODO: assign */
    GPIO_NUM_16,  /* Channel 5 — TODO: assign */
    GPIO_NUM_17,  /* Channel 6 — TODO: assign */
    GPIO_NUM_18,  /* Channel 7 — TODO: assign */
};

/*===========================================================
 * Per-channel configuration (NVS-persisted blob)
 *===========================================================*/
#define GPIO_DIRECTION_INPUT  0
#define GPIO_DIRECTION_OUTPUT 1

#define GPIO_PULL_NONE        0
#define GPIO_PULL_UP          1
#define GPIO_PULL_DOWN        2

typedef struct
{
    uint8_t direction;  /* GPIO_DIRECTION_INPUT / GPIO_DIRECTION_OUTPUT */
    uint8_t pull;       /* GPIO_PULL_NONE / GPIO_PULL_UP / GPIO_PULL_DOWN */
    uint8_t state;      /* For outputs: last-set level to restore on boot.
                           For inputs:  last-read level (informational).     */
} gpio_channel_cfg_t;

typedef struct
{
    gpio_channel_cfg_t ch[GPIO_CHANNEL_COUNT];
} gpio_config_blob_t;

/*===========================================================
 * Public API
 *===========================================================*/

/**
 * @brief Initialise the GPIO subsystem.
 *
 * Loads gpio_config_blob_t from NVS (defaults all-input if first boot).
 * Configures each channel via gpio_config(). Installs per-channel ISRs
 * for input-configured pins. Starts the gpio_event_task.
 *
 * Must be called AFTER gpio_install_isr_service(0) in app_main.
 */
void gpio_subsystem_init(void);

/**
 * @brief Apply a GPIO config command received from the gateway.
 *
 * Updates direction and pull for all channels indicated in channel_mask.
 * Re-configures GPIOs live (tears down/re-adds ISRs as needed).
 * Persists updated blob to NVS.
 *
 * @return SUCCESS, or GPIO_CHANNEL_INDEX_INVALID / GPIO_DIRECTION_INVALID_FORMAT
 *         / GPIO_PULL_INVALID_FORMAT on bad input.
 */
error_codes gpio_handle_config_cmd(const GpioConfigCmd *cmd);

/**
 * @brief Apply a GPIO set command received from the gateway.
 *
 * Sets output level for all channels indicated in channel_mask.
 * Rejected with GPIO_SET_ON_INPUT_CHANNEL_REJECTED if any targeted
 * channel is currently configured as input.
 * Persists updated state to NVS.
 *
 * @return SUCCESS or an error_codes value.
 */
error_codes gpio_handle_set_cmd(const GpioSetCmd *cmd);

/**
 * @brief Build a GpioStatusPkt reflecting the current state of all 8 channels.
 *
 * channel_states:     bitmask, bit N = current level of channel N.
 * channel_directions: bitmask, bit N = direction of channel N (1 = output).
 */
void gpio_build_status_pkt(GpioStatusPkt *pkt);

#endif /* GPIO_SUBSYSTEM_H */
