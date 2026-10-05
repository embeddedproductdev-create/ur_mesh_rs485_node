#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include <common_types.h>
#include <modbus/modbus_master.h>
#include "esp_modbus_master.h"
#include "esp_modbus_common.h"
#include "mbcontroller.h"

static const char *TAG = "MODBUS_MASTER";


/*===========================================================
 * Internal state
 *===========================================================*/
static void *s_master_handle = NULL;
static bool s_initialized = false;

/*===========================================================
 * Public API — init
 *===========================================================*/
 esp_err_t modbus_master_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    /* 1. Configure communication parameters */
    mb_communication_info_t comm_info = {
        .ser_opts.port             = MODBUS_UART_PORT,
        .ser_opts.baudrate         = MODBUS_BAUD_RATE,
        .ser_opts.parity           = MB_PARITY_NONE,
        .ser_opts.data_bits        = UART_DATA_8_BITS,
        .ser_opts.stop_bits        = UART_STOP_BITS_1,
        .ser_opts.mode             = MB_RTU,
        .ser_opts.response_tout_ms = MB_RESPONSE_TOUT_MS,
    };

    /* 2. Create the Modbus serial master instance */
    esp_err_t err = mbc_master_create_serial(&comm_info, &s_master_handle);
    MB_RETURN_ON_FALSE(err == ESP_OK && s_master_handle != NULL, err, TAG,
                        "mbc_master_create_serial failed (0x%x)", (unsigned int)err);

    /* 3. Configure physical UART pins (TX, RX, RTS/DE) */
    err = uart_set_pin(MODBUS_UART_PORT, MODBUS_TX_PIN, MODBUS_RX_PIN,
                       MODBUS_DE_RE_PIN, UART_PIN_NO_CHANGE);
    MB_RETURN_ON_FALSE(err == ESP_OK, err, TAG,
                        "uart_set_pin failed (0x%x)", (unsigned int)err);

    /* 4. Set UART driver to RS485 Half-Duplex mode for automatic DE/RE control */
    err = uart_set_mode(MODBUS_UART_PORT, UART_MODE_RS485_HALF_DUPLEX);
    MB_RETURN_ON_FALSE(err == ESP_OK, err, TAG,
                        "uart_set_mode failed (0x%x)", (unsigned int)err);

    /* 5. Start the Modbus Master controller */
    err = mbc_master_start(s_master_handle);
    MB_RETURN_ON_FALSE(err == ESP_OK, err, TAG,
                        "mbc_master_start failed (0x%x)", (unsigned int)err);

    s_initialized = true;
    ESP_LOGI(TAG, "esp_modbus master started on UART%d @ %d baud",
             MODBUS_UART_PORT, MODBUS_BAUD_RATE);

    return ESP_OK;
}

/*===========================================================
 * Public API — read functions
 *===========================================================*/
esp_err_t modbus_read_holding_regs(uint8_t slave_addr,
                                     uint16_t reg_addr,
                                     uint16_t reg_count,
                                     uint8_t *out_buf)
{    mb_param_request_t req = {
        .slave_addr = slave_addr,
        .command    = 0x03,
        .reg_start  = reg_addr,
        .reg_size   = reg_count,
    };

    esp_err_t err = mbc_master_send_request(s_master_handle, &req, (void *)out_buf);
    if (err != ESP_OK)
    {
        ESP_LOGE(MODBUS_TAG,
                 "Read failed slave=0x%02x reg=0x%04x count=%d err=%s",
                 slave_addr, reg_addr, reg_count, esp_err_to_name(err));
        if (err == ESP_ERR_TIMEOUT)          return MODBUS_TIMEOUT;
        if (err == ESP_ERR_INVALID_RESPONSE) return MODBUS_EXCEPTION_RESPONSE;
        return MODBUS_INVALID_RESPONSE_LENGTH;
    }
    return ESP_OK;
}

esp_err_t modbus_read_input_regs(uint8_t slave_addr, uint16_t reg_addr, 
                                 uint16_t reg_count, uint16_t *out_buf)
{
    if (!s_initialized || s_master_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    mb_param_request_t req = {
        .slave_addr = slave_addr,
        .command    = 0x04, // Function Code 0x04
        .reg_start  = reg_addr,
        .reg_size   = reg_count
    };

    return mbc_master_send_request(s_master_handle, &req, (void *)out_buf);
}

error_codes modbus_write_single_register(uint8_t  slave_addr,
                                         uint16_t reg_addr,
                                         uint16_t value)
{
    mb_param_request_t req = {
        .slave_addr = slave_addr,
        .command    = 0x06,   /* FC06 */
        .reg_start  = reg_addr,
        .reg_size   = 1,
    };

    esp_err_t err = mbc_master_send_request(s_master_handle, &req, (void *)&value);
    if (err != ESP_OK)
    {
        ESP_LOGE(MODBUS_TAG,
                 "FC06 write failed slave=0x%02x reg=0x%04x val=0x%04x err=%s",
                 slave_addr, reg_addr, value, esp_err_to_name(err));
        if (err == ESP_ERR_TIMEOUT)          return MODBUS_TIMEOUT;
        if (err == ESP_ERR_INVALID_RESPONSE) return MODBUS_EXCEPTION_RESPONSE;
        return err == ESP_ERR_INVALID_ARG ? MODBUS_SLAVE_ADDR_INVALID : MODBUS_INVALID_RESPONSE_LENGTH;
    }
    return err;
}

/*===========================================================
 * Register decode helpers
 *===========================================================*/

/**
 * @brief IEEE 754 float from two consecutive 16-bit registers.
 *        Big-endian word order (ABCD): reg[0]=MSW, reg[1]=LSW.
 */
float modbus_regs_to_float(const uint8_t *buf, uint16_t offset)
{
    uint32_t raw = ((uint32_t)buf[offset    ] << 24) |
                   ((uint32_t)buf[offset + 1] << 16) |
                   ((uint32_t)buf[offset + 2] <<  8) |
                   ((uint32_t)buf[offset + 3]);
    float val;
    memcpy(&val, &raw, sizeof(float));
    return val;
}

uint32_t modbus_regs_to_uint32(const uint8_t *buf, uint16_t offset)
{
    return ((uint32_t)buf[offset    ] << 24) |
           ((uint32_t)buf[offset + 1] << 16) |
           ((uint32_t)buf[offset + 2] <<  8) |
           ((uint32_t)buf[offset + 3]);
}

uint32_t modbus_regs_to_uint32_long_inverse(const uint8_t *buf, uint16_t offset)
{
    /* Elmeasure Long Inverse: low word (LSW) stored first in memory (CDAB order)
     * Wire bytes: [LSW_hi][LSW_lo][MSW_hi][MSW_lo]
     * Reconstruct: MSW = buf[offset+2..3], LSW = buf[offset+0..1]           */
    uint16_t lsw = ((uint16_t)buf[offset    ] << 8) | buf[offset + 1];
    uint16_t msw = ((uint16_t)buf[offset + 2] << 8) | buf[offset + 3];
    return ((uint32_t)msw << 16) | lsw;
}

uint16_t modbus_reg_to_uint16(const uint8_t *buf, uint16_t offset)
{
    return ((uint16_t)buf[offset] << 8) | buf[offset + 1];
}
