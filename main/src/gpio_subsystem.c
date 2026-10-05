#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include <main.h>
#include <common_types.h>
#include <flash.h>
#include <gpio_subsystem.h>
#include <led.h>

#include <ble_mesh_node.h>
#include "esp_ble_mesh_local_data_operation_api.h"

/*===========================================================
 * Internal types
 *===========================================================*/
typedef struct
{
    uint8_t channel;   /* 0–7 */
    uint8_t level;     /* 0 or 1 */
} gpio_event_t;

/*===========================================================
 * Module-private state
 *===========================================================*/
static gpio_config_blob_t  s_cfg;               /* Current config, mirrored from NVS   */
static QueueHandle_t       s_gpio_event_queue;  /* ISR → gpio_event_task               */

/* Module-private sequence number for GPIO status packets */
static uint16_t s_gpio_seqno = 0;

/*===========================================================
 * Forward declarations
 *===========================================================*/
static void     gpio_event_task(void *arg);
static void     configure_single_channel(uint8_t ch);
static void     remove_isr_if_input(uint8_t ch);
static esp_err_t install_isr_if_input(uint8_t ch);
static void     persist_config(void);
static void IRAM_ATTR  gpio_isr_handler(void *arg);

/* Externals for status packet population */
extern char serialNoStr[SERIAL_NO_LEN];

/*===========================================================
 * ISR — runs in IRAM, must be minimal
 *===========================================================*/
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint8_t ch    = (uint8_t)(uintptr_t)arg;
    gpio_event_t evt = {
        .channel = ch,
        .level   = (uint8_t)gpio_get_level(GPIO_CH_PINS[ch]),
    };
    BaseType_t high_task_woken = pdFALSE;
    xQueueSendFromISR(s_gpio_event_queue, &evt, &high_task_woken);
    if (high_task_woken) portYIELD_FROM_ISR();
}

/*===========================================================
 * Event task — handles input state changes outside ISR context
 *===========================================================*/
static void gpio_event_task(void *arg)
{
    gpio_event_t evt;
    for (;;)
    {
        if (xQueueReceive(s_gpio_event_queue, &evt, portMAX_DELAY))
        {
            ESP_LOGI(GPIO_SUBSYSTEM_TAG, "CH%d input changed -> %d", evt.channel, evt.level);

            /* Update shadow state */
            s_cfg.ch[evt.channel].state = evt.level;

            /* Persist new input level */
            persist_config();

            /* Notify LED (only from PROVISIONED_IDLE) */
            led_notify_tx_activity();

            /* Build and enqueue GPIO status packet */
            GpioStatusPkt pkt = {0};
            gpio_build_status_pkt(&pkt);
            pkt.node_type = NODE_TYPE_RS485;
            pkt.elemaddr  = esp_ble_mesh_get_primary_element_address();
            pkt.msgseqno  = ++s_gpio_seqno;
            ble_send_gpio_status(&pkt);
        }
    }
}

/*===========================================================
 * Internal helpers
 *===========================================================*/

static void remove_isr_if_input(uint8_t ch)
{
    if (s_cfg.ch[ch].direction == GPIO_DIRECTION_INPUT)
        gpio_isr_handler_remove(GPIO_CH_PINS[ch]);
}

static esp_err_t install_isr_if_input(uint8_t ch)
{
    if (s_cfg.ch[ch].direction == GPIO_DIRECTION_INPUT)
    {
        return gpio_isr_handler_add(GPIO_CH_PINS[ch], gpio_isr_handler,
                                    (void *)(uintptr_t)ch);
    }
    return ESP_OK;
}

static void configure_single_channel(uint8_t ch)
{
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << GPIO_CH_PINS[ch]),
        .mode         = (s_cfg.ch[ch].direction == GPIO_DIRECTION_OUTPUT)
                            ? GPIO_MODE_OUTPUT
                            : GPIO_MODE_INPUT,
        .pull_up_en   = (s_cfg.ch[ch].pull == GPIO_PULL_UP)
                            ? GPIO_PULLUP_ENABLE
                            : GPIO_PULLUP_DISABLE,
        .pull_down_en = (s_cfg.ch[ch].pull == GPIO_PULL_DOWN)
                            ? GPIO_PULLDOWN_ENABLE
                            : GPIO_PULLDOWN_DISABLE,
        .intr_type    = (s_cfg.ch[ch].direction == GPIO_DIRECTION_INPUT)
                            ? GPIO_INTR_ANYEDGE
                            : GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(GPIO_SUBSYSTEM_TAG, "gpio_config failed for CH%d (GPIO%d): %s",
                 ch, GPIO_CH_PINS[ch], esp_err_to_name(err));
        return;
    }

    /* For outputs: restore last persisted level immediately */
    if (s_cfg.ch[ch].direction == GPIO_DIRECTION_OUTPUT)
        gpio_set_level(GPIO_CH_PINS[ch], s_cfg.ch[ch].state);
}

static void persist_config(void)
{
    set_blob_in_nvs_flash(GENERAL_HANDLE, NVS_GPIO_CONFIG_KEY,
                          &s_cfg, sizeof(gpio_config_blob_t));
}

static void load_or_default_config(void)
{
    size_t len = sizeof(gpio_config_blob_t);
    get_blob_from_nvs_flash(GENERAL_HANDLE, NVS_GPIO_CONFIG_KEY, &s_cfg, &len);

    /* If blob not found (first boot) len stays 0 — default all to input, no pull */
    if (len != sizeof(gpio_config_blob_t))
    {
        ESP_LOGW(GPIO_SUBSYSTEM_TAG, "No GPIO config in NVS — defaulting all channels to input");
        memset(&s_cfg, 0, sizeof(gpio_config_blob_t));
        /* direction=0 (INPUT), pull=0 (NONE), state=0 — already zero from memset */
        persist_config();
    }
}

/*===========================================================
 * Public API
 *===========================================================*/

void gpio_subsystem_init(void)
{
    /* Load or create default config */
    load_or_default_config();

    /* Create event queue — ISR will push into this */
    s_gpio_event_queue = xQueueCreate(16, sizeof(gpio_event_t));
    if (s_gpio_event_queue == NULL)
    {
        ESP_LOGE(GPIO_SUBSYSTEM_TAG, "Failed to create GPIO event queue");
        led_notify_fault(true);
        return;
    }

    /* Configure all channels and install ISRs for inputs */
    for (uint8_t ch = 0; ch < GPIO_CHANNEL_COUNT; ch++)
    {
        configure_single_channel(ch);
        esp_err_t err = install_isr_if_input(ch);
        if (err != ESP_OK)
            ESP_LOGE(GPIO_SUBSYSTEM_TAG, "ISR install failed for CH%d: %s",
                     ch, esp_err_to_name(err));
    }

    /* Start the event handler task */
    BaseType_t ret = xTaskCreate(gpio_event_task, "GPIO Event Task",
                                 GPIO_EVENT_TASK_STACK_SIZE, NULL, 5, NULL);
    if (ret != pdPASS)
    {
        ESP_LOGE(GPIO_SUBSYSTEM_TAG, "Failed to create GPIO event task");
        led_notify_fault(true);
    }

    ESP_LOGI(GPIO_SUBSYSTEM_TAG, "GPIO subsystem initialised — %d channels", GPIO_CHANNEL_COUNT);
}

/* ------------------------------------------------------------------ */

error_codes gpio_handle_config_cmd(const GpioConfigCmd *cmd)
{
    if (cmd->channel_mask == 0)
        return GPIO_CHANNEL_INDEX_INVALID;

    for (uint8_t ch = 0; ch < GPIO_CHANNEL_COUNT; ch++)
    {
        if (!(cmd->channel_mask & (1 << ch)))
            continue;

        /* Tear down ISR if channel was previously input */
        remove_isr_if_input(ch);

        /* Direction — per channel from direction_mask */
        s_cfg.ch[ch].direction = (cmd->direction_mask & (1 << ch))
                                 ? GPIO_DIRECTION_OUTPUT
                                 : GPIO_DIRECTION_INPUT;

        /* Pull — pull_up takes priority over pull_down */
        if (cmd->pull_up_mask & (1 << ch))
            s_cfg.ch[ch].pull = GPIO_PULL_UP;
        else if (cmd->pull_down_mask & (1 << ch))
            s_cfg.ch[ch].pull = GPIO_PULL_DOWN;
        else
            s_cfg.ch[ch].pull = GPIO_PULL_NONE;

        /* Reset state on direction change */
        s_cfg.ch[ch].state = 0;

        /* Reconfigure GPIO hardware */
        configure_single_channel(ch);

        /* Re-install ISR if new direction is input */
        esp_err_t err = install_isr_if_input(ch);
        if (err != ESP_OK)
            ESP_LOGE(GPIO_SUBSYSTEM_TAG, "ISR re-install failed for CH%d: %s",
                     ch, esp_err_to_name(err));

        ESP_LOGI(GPIO_SUBSYSTEM_TAG, 
                    "CH%d reconfigured: dir=%s pull_up=%d pull_down=%d", 
                    ch,
                    (s_cfg.ch[ch].direction == GPIO_DIRECTION_OUTPUT) ? "OUTPUT" : "INPUT",
                    (s_cfg.ch[ch].pull == GPIO_PULL_UP),
                    (s_cfg.ch[ch].pull == GPIO_PULL_DOWN));
    }

    persist_config();
    return SUCCESS;
}

/* ------------------------------------------------------------------ */

error_codes gpio_handle_set_cmd(const GpioSetCmd *cmd)
{
    if (cmd->channel_mask == 0)
        return GPIO_CHANNEL_INDEX_INVALID;

    /* Validate: reject if any targeted channel is currently input */
    for (uint8_t ch = 0; ch < GPIO_CHANNEL_COUNT; ch++)
    {
        if (!(cmd->channel_mask & (1 << ch)))
            continue;
        if (s_cfg.ch[ch].direction == GPIO_DIRECTION_INPUT)
        {
            ESP_LOGE(GPIO_SUBSYSTEM_TAG,
                     "CH%d is configured as INPUT — SET command rejected", ch);
            return GPIO_SET_ON_INPUT_CHANNEL_REJECTED;
        }
    }

     /* Apply level_mask per channel and persist */
    for (uint8_t ch = 0; ch < GPIO_CHANNEL_COUNT; ch++)
    {
        if (!(cmd->channel_mask & (1 << ch)))
            continue;

        uint8_t level = (cmd->level_mask & (1 << ch)) ? 1 : 0;
        gpio_set_level(GPIO_CH_PINS[ch], level);
        s_cfg.ch[ch].state = level;

        ESP_LOGI(GPIO_SUBSYSTEM_TAG, "CH%d set to %d", ch, level);
    }

    persist_config();
    return SUCCESS;
}

/* ------------------------------------------------------------------ */

void gpio_build_status_pkt(GpioStatusPkt *pkt)
{
    pkt->packetid           = NODE_GPIO_STATUS_PACKET;
    pkt->node_type          = NODE_TYPE_RS485;

    pkt->channel_states     = 0;
    pkt->channel_directions = 0;
    pkt->errorcode          = SUCCESS;

    for (uint8_t ch = 0; ch < GPIO_CHANNEL_COUNT; ch++)
    {
        uint8_t bit = (1 << ch);
        if (s_cfg.ch[ch].state)
            pkt->channel_states |= bit;
        if (s_cfg.ch[ch].direction == GPIO_DIRECTION_OUTPUT)
            pkt->channel_directions |= bit;
    }

    strncpy(pkt->deviceName, serialNoStr, SERIAL_NO_LEN - 1);
    pkt->deviceName[SERIAL_NO_LEN - 1] = '\0';
}
