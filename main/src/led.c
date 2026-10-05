#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#include <main.h>
#include <led.h>

#define LOW  0
#define HIGH 1

/* Externals from main.c */
extern uint8_t provisioned;
extern uint8_t configured;
extern bool    fault_active;

static led_color_t  current_color = {LOW, LOW, LOW};
static led_state_t  current_state = LED_STATE_UNPROVISIONED;
static esp_timer_handle_t blink_timer;

colors_t colors;

static void apply_color(led_color_t color);
static void led_blink_callback(void *arg);

/* ------------------------------------------------------------------ */

void led_init(void)
{
    colors.RED     = (led_color_t){HIGH, LOW,  LOW };
    colors.BLUE    = (led_color_t){LOW,  LOW,  HIGH};
    colors.GREEN   = (led_color_t){LOW,  HIGH, LOW };
    colors.YELLOW  = (led_color_t){LOW,  HIGH, HIGH};
    colors.MAGENTA = (led_color_t){HIGH, LOW,  HIGH};
    colors.ORANGE  = (led_color_t){HIGH, HIGH, LOW };
    colors.WHITE   = (led_color_t){HIGH, HIGH, HIGH};
    colors.OFF     = (led_color_t){LOW,  LOW,  LOW };

    gpio_set_direction(LED_PIN_RED,   GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_PIN_GREEN, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_PIN_BLUE,  GPIO_MODE_OUTPUT);

    esp_timer_create_args_t blink_timer_args = {
        .callback        = led_blink_callback,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "LED Blink Timer",
    };
    esp_err_t err = esp_timer_create(&blink_timer_args, &blink_timer);
    if (err != ESP_OK)
        ESP_LOGE(LED_TAG, "Blink timer creation failed: %s", esp_err_to_name(err));
}

/* ------------------------------------------------------------------ */

const char *get_led_state_string(led_state_t state)
{
    switch (state)
    {
    case LED_STATE_OFF:              return "LED_STATE_OFF";
    case LED_STATE_UNPROVISIONED:    return "LED_STATE_UNPROVISIONED";
    case LED_STATE_PROVISIONING:     return "LED_STATE_PROVISIONING";
    case LED_STATE_UNCONFIGURED:     return "LED_STATE_UNCONFIGURED";
    case LED_STATE_PROVISIONED_IDLE: return "LED_STATE_PROVISIONED_IDLE";
    case LED_STATE_TX_ACTIVITY:      return "LED_STATE_TX_ACTIVITY";
    case LED_STATE_MSG_RECVD:        return "LED_STATE_MSG_RECVD";
    case LED_STATE_FAULT:            return "LED_STATE_FAULT";
    default:                         return "LED_STATE_UNKNOWN";
    }
}

/* ------------------------------------------------------------------ */

void led_set_color(led_color_t color)
{
    current_color = color;
    if (LED_INVERTED)
    {
        color.red   = !color.red;
        color.green = !color.green;
        color.blue  = !color.blue;
    }
    apply_color(color);
}

static void apply_color(led_color_t color)
{
    gpio_set_level(LED_PIN_RED,   color.red);
    gpio_set_level(LED_PIN_GREEN, color.green);
    gpio_set_level(LED_PIN_BLUE,  color.blue);
}

/* ------------------------------------------------------------------ */

void led_set_state(led_state_t state)
{
    current_state = state;
    esp_timer_stop(blink_timer);

    switch (state)
    {
    case LED_STATE_OFF:
        led_set_color(colors.OFF);
        break;

    case LED_STATE_UNPROVISIONED:
        /* Blue, slow blink */
        led_set_color(colors.BLUE);
        esp_timer_start_periodic(blink_timer, SLOW_BLINK_INTERVAL_MS * 1000);
        break;

    case LED_STATE_PROVISIONING:
        /* Blue, solid — actively pairing */
        led_set_color(colors.BLUE);
        break;

    case LED_STATE_UNCONFIGURED:
        /* White, solid — provisioned but no config yet */
        led_set_color(colors.WHITE);
        break;

    case LED_STATE_PROVISIONED_IDLE:
        /* Green, solid — normal operating state */
        led_set_color(colors.GREEN);
        break;

    case LED_STATE_TX_ACTIVITY:
        /* Orange, 2x fast blink, then auto-return to idle */
        for (uint8_t i = 0; i < 2; i++)
        {
            led_set_color(colors.ORANGE);
            vTaskDelay(pdMS_TO_TICKS(FAST_BLINK_INTERVAL_MS));
            led_set_color(colors.OFF);
            vTaskDelay(pdMS_TO_TICKS(FAST_BLINK_INTERVAL_MS));
        }
        update_led_status();
        break;

    case LED_STATE_MSG_RECVD:
        /* Yellow, 1 second solid, then auto-return to idle */
        led_set_color(colors.YELLOW);
        vTaskDelay(pdMS_TO_TICKS(1000));
        update_led_status();
        break;

    case LED_STATE_FAULT:
        /* Red, fast blink, persistent — stays until led_notify_fault(false) */
        led_set_color(colors.RED);
        esp_timer_start_periodic(blink_timer, FAST_BLINK_INTERVAL_MS * 1000);
        break;

    default:
        led_set_color(colors.OFF);
        break;
    }
}

/* ------------------------------------------------------------------ */

/**
 * @brief Re-evaluate and apply the correct steady-state LED from global flags.
 *
 * Priority: fault > unprovisioned > unconfigured > idle.
 * One-shot transient states (TX_ACTIVITY, MSG_RECVD) call this themselves
 * when their blink/delay completes, so callers never need to "restore" state.
 */
void update_led_status(void)
{
    if (fault_active)
        led_set_state(LED_STATE_FAULT);
    else if (!provisioned)
        led_set_state(LED_STATE_UNPROVISIONED);
    else if (!configured)
        led_set_state(LED_STATE_UNCONFIGURED);
    else
        led_set_state(LED_STATE_PROVISIONED_IDLE);
}

/* ------------------------------------------------------------------ */

/**
 * @brief One-shot TX activity blink. Only fires from PROVISIONED_IDLE.
 */
void led_notify_tx_activity(void)
{
    if (current_state != LED_STATE_PROVISIONED_IDLE)
        return;
    led_set_state(LED_STATE_TX_ACTIVITY);
}

/**
 * @brief One-shot message-received blink. Only fires from PROVISIONED_IDLE.
 */
void led_notify_msg_recvd(void)
{
    if (current_state != LED_STATE_PROVISIONED_IDLE)
        return;
    led_set_state(LED_STATE_MSG_RECVD);
}

/**
 * @brief Activate or clear the fault LED state.
 *
 * Setting active=true latches FAULT from any state.
 * Setting active=false clears the fault and re-evaluates via update_led_status().
 */
void led_notify_fault(bool active)
{
    fault_active = active;
    if (active)
        led_set_state(LED_STATE_FAULT);
    else
        update_led_status();
}

/* ------------------------------------------------------------------ */

static void led_blink_callback(void *arg)
{
    switch (current_state)
    {
    case LED_STATE_UNPROVISIONED:
        /* Alternate Blue ↔ OFF for a gentle slow blink */
        led_set_color(current_color.blue ? colors.OFF : colors.BLUE);
        break;

    case LED_STATE_FAULT:
        /* Alternate Red ↔ OFF for urgent fast blink */
        led_set_color(current_color.red ? colors.OFF : colors.RED);
        break;

    default:
        break;
    }
}
