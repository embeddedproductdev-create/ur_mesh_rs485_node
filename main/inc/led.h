#ifndef LED_H
#define LED_H

#include <stdint.h>
#include <stdbool.h>

/*===========================================================
 * Blink intervals
 *===========================================================*/
#define FAST_BLINK_INTERVAL_MS  100
#define SLOW_BLINK_INTERVAL_MS  500

/*===========================================================
 * RGB GPIO pin assignments — update to match hardware
 *===========================================================*/
#define LED_PIN_RED    21
#define LED_PIN_GREEN  14
#define LED_PIN_BLUE   13

#define LED_INVERTED   1

#define LED_TAG        "LED"

/*===========================================================
 * LED states — meter node state machine
 *
 * Valid transitions (see design doc):
 *   UNPROVISIONED → PROVISIONING  (prov start callback)
 *   PROVISIONING  → UNCONFIGURED  (prov success callback)
 *   PROVISIONING  → UNPROVISIONED (prov fail / timeout)
 *   UNCONFIGURED  → PROVISIONED_IDLE (valid config saved)
 *   PROVISIONED_IDLE → UNCONFIGURED  (config cleared)
 *   UNCONFIGURED / PROVISIONED_IDLE → UNPROVISIONED (factory reset)
 *   PROVISIONED_IDLE → TX_ACTIVITY   (one-shot, auto-returns)
 *   PROVISIONED_IDLE → MSG_RECVD     (one-shot, auto-returns)
 *   ANY            → FAULT           (fault_active = true)
 *   FAULT          → re-evaluated    (fault_active = false → update_led_status())
 *
 * Illegal: UNPROVISIONED → UNCONFIGURED (never skip provisioning)
 *          UNPROVISIONED → PROVISIONED_IDLE (never skip both steps)
 *          TX_ACTIVITY / MSG_RECVD reachable only from PROVISIONED_IDLE
 *===========================================================*/
typedef enum
{
    LED_STATE_OFF,
    LED_STATE_UNPROVISIONED,     /* Blue,   slow blink  — not provisioned         */
    LED_STATE_PROVISIONING,      /* Blue,   solid       — pairing in progress      */
    LED_STATE_UNCONFIGURED,      /* White,  solid       — provisioned, no config   */
    LED_STATE_PROVISIONED_IDLE,  /* Green,  solid       — normal operating state   */
    LED_STATE_TX_ACTIVITY,       /* Orange, 2x blink    — data pushed to gateway   */
    LED_STATE_MSG_RECVD,         /* Yellow, 1s solid    — command received         */
    LED_STATE_FAULT,             /* Red,    fast blink  — persistent fault, latched */
} led_state_t;

/*===========================================================
 * RGB color struct
 *===========================================================*/
typedef struct
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} led_color_t;

typedef struct colors
{
    led_color_t RED;
    led_color_t BLUE;
    led_color_t GREEN;
    led_color_t YELLOW;
    led_color_t MAGENTA;
    led_color_t ORANGE;
    led_color_t WHITE;
    led_color_t OFF;
} colors_t;

extern colors_t colors;

/*===========================================================
 * Public API
 *
 * Callers must NOT call led_set_state() directly for the
 * one-shot and fault states. Use the guard helpers below
 * to enforce the legal transition table.
 *===========================================================*/
void        led_init(void);
void        led_set_color(led_color_t color);
void        led_set_state(led_state_t state);
const char *get_led_state_string(led_state_t state);

/* Idle-status poll — driven by provisioned / configured / fault_active globals */
void update_led_status(void);

/* One-shot transition helpers — only fire from PROVISIONED_IDLE */
void led_notify_tx_activity(void);
void led_notify_msg_recvd(void);

/* Fault control — any state */
void led_notify_fault(bool active);

#endif /* LED_H */
