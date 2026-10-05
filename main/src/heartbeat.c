#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"

#include <main.h>
#include <common_types.h>
#include <flash.h>
#include <ble_mesh_node.h>
#include <heartbeat.h>
#include <led.h>

#include "esp_ble_mesh_local_data_operation_api.h"

#define HB_TAG              "HEARTBEAT"
#define ONE_SEC_IN_US       1000000ULL

/*===========================================================
 * Externals
 *===========================================================*/
extern uint16_t publishPeriod;
extern uint8_t  provisioned;
extern char     serialNoStr[SERIAL_NO_LEN];
extern uint16_t elemaddr_global;
extern float    deviceUpTimeHrs;

/*===========================================================
 * Module state
 *===========================================================*/
static esp_timer_handle_t s_hb_timer = NULL;
static uint16_t           s_msgseqno = 0;

/*===========================================================
 * Timer callback — fires every publishPeriod seconds
 * Sends NODE_HEARTBEAT_ACK to gateway via BLE Mesh
 *===========================================================*/
static void hb_callback(void *arg)
{
    RS485CmdStruct hb = {0};

    hb.packetid        = NODE_HEARTBEAT_ACK;
    hb.msgseqno        = ++s_msgseqno;
    hb.elemaddr        = esp_ble_mesh_get_primary_element_address();
    hb.node_type        = NODE_TYPE_RS485;
    hb.errorcode       = SUCCESS;
    hb.majversion      = MAJ_VERSION;
    hb.minversion      = MIN_VERSION;
    hb.patchversion    = PATCH_VERSION;
    hb.publishPeriodSec = publishPeriod;
    hb.provisioned     = provisioned;
    hb.deviceUpTimeHrs = (xTaskGetTickCount() * portTICK_PERIOD_MS) / 3600000.0f;

    strncpy(hb.deviceName, serialNoStr, SERIAL_NO_LEN - 1);
    hb.deviceName[SERIAL_NO_LEN - 1] = '\0';

    ESP_LOGI(HB_TAG, "Sending heartbeat — uptime=%.2fh period=%ds",
             hb.deviceUpTimeHrs, publishPeriod);

    ble_send_command_ack(&hb);
    led_notify_tx_activity();
}

/*===========================================================
 * Public API
 *===========================================================*/
void hb_timer_start(void)
{
    if (esp_timer_is_active(s_hb_timer))
    {
        ESP_LOGW(HB_TAG, "Timer already running — ignoring start");
        return;
    }
    ESP_LOGI(HB_TAG, "Starting heartbeat — period=%ds", publishPeriod);
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_hb_timer,
                    (uint64_t)publishPeriod * ONE_SEC_IN_US));
}

void hb_timer_stop(void)
{
    if (!esp_timer_is_active(s_hb_timer))
    {
        ESP_LOGW(HB_TAG, "Timer not running — ignoring stop");
        return;
    }
    ESP_LOGI(HB_TAG, "Stopping heartbeat");
    ESP_ERROR_CHECK(esp_timer_stop(s_hb_timer));
}

void hb_timer_restart(void)
{
    if (!esp_timer_is_active(s_hb_timer))
    {
        ESP_LOGW(HB_TAG, "Timer not active — starting instead");
        hb_timer_start();
        return;
    }
    ESP_LOGI(HB_TAG, "Restarting heartbeat — new period=%ds", publishPeriod);
    ESP_ERROR_CHECK(esp_timer_restart(s_hb_timer,
                    (uint64_t)publishPeriod * ONE_SEC_IN_US));
}

void hb_init(void)
{
    esp_timer_create_args_t args = {
        .callback        = hb_callback,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "HB Timer",
    };

    ESP_ERROR_CHECK(esp_timer_create(&args, &s_hb_timer));
    ESP_LOGI(HB_TAG, "Heartbeat timer created — period=%ds", publishPeriod);

    /* Auto-start if already provisioned (e.g. warm boot after power cycle) */
    if (provisioned)
        hb_timer_start();
}

void handle_setting_hb_publish_configuration(uint16_t new_period_sec)
{
    if (new_period_sec == 0)
    {
        ESP_LOGE(HB_TAG, "Invalid publish period 0 — ignoring");
        return;
    }

    if (publishPeriod == new_period_sec)
    {
        ESP_LOGI(HB_TAG, "Publish period unchanged (%ds) — ignoring", publishPeriod);
        return;
    }

    ESP_LOGI(HB_TAG, "Publish period %ds → %ds", publishPeriod, new_period_sec);

    publishPeriod = new_period_sec;

    /* Persist to NVS */
    set_number_in_nvs_flash(GENERAL_HANDLE, NVS_PUBPERIOD_KEY,
                            publishPeriod, UINT16_SIZE);

    /* Restart heartbeat timer with new period */
    hb_timer_restart();

    /*
     * publishPeriod is read directly by modbus_poll_task on each
     * iteration via vTaskDelay — no explicit restart needed there.
     * The poll task loop reads publishPeriod at the top of every
     * cycle so the new value takes effect on the next wakeup.
     */
    ESP_LOGI(HB_TAG, "Modbus poll interval will update on next cycle");
}
