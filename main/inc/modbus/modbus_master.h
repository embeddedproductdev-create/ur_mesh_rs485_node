#ifndef MODBUS_MASTER_H
#define MODBUS_MASTER_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include <common_types.h>

#define MODBUS_TAG "MODBUS"

/*===========================================================
 * UART / RS485 hardware config
 * TODO: Replace pin numbers with actual hardware assignments
 *===========================================================*/

#define MODBUS_UART_PORT     UART_NUM_2
#define MODBUS_TX_PIN        17      /* TODO: assign */
#define MODBUS_RX_PIN        16      /* TODO: assign */
#define MODBUS_DE_RE_PIN     18      /* TODO: assign — MAX485 DE/RE tied together */
#define MODBUS_BAUD_RATE     9600
#define MODBUS_UART_BUF_SIZE 256
#define MB_RESPONSE_TOUT_MS  2000

/*===========================================================
 * Modbus RTU timing
 *===========================================================*/
#define MODBUS_RESPONSE_TIMEOUT_MS  1000
#define MODBUS_TURNAROUND_DELAY_MS  10   /* DE deassertion gap between frames */
#define MODBUS_INTER_DEVICE_DELAY_MS 50  /* Delay between polling two devices */


/*===========================================================
 * Public API
 *===========================================================*/

/**
 * @brief Initialise UART2 in RS485 half-duplex mode.
 *        Configures TX, RX, DE/RE pins and installs driver.
 *        Must be called once from app_main or modbus_poll_task init.
 */
esp_err_t modbus_master_init(void);

/**
 * @brief Read one or more holding registers from a Modbus slave.
 *
 * @param slave_addr   Modbus slave address (1–247)
 * @param reg_addr     Starting register address
 * @param reg_count    Number of registers to read (each 16-bit)
 * @param out_buf      Caller-allocated buffer, must hold reg_count * 2 bytes
 * @return SUCCESS, MODBUS_TIMEOUT, MODBUS_CRC_ERROR,
 *         MODBUS_EXCEPTION_RESPONSE, or MODBUS_INVALID_RESPONSE_LENGTH
 */
esp_err_t modbus_read_holding_regs(uint8_t slave_addr,
                                     uint16_t reg_addr,
                                     uint16_t reg_count,
                                     uint8_t *out_buf);

/**
 * @brief Read one or more input registers from a Modbus slave.
 *        Same semantics as modbus_read_holding_regs.
 */
esp_err_t modbus_read_input_regs(uint8_t slave_addr,
                                   uint16_t reg_addr,
                                   uint16_t reg_count,
                                   uint16_t *out_buf);

error_codes modbus_write_single_register(uint8_t  slave_addr,
                                         uint16_t reg_addr,
                                         uint16_t value);

/**
 * @brief Combine two consecutive 16-bit register values into a float.
 *        Interprets as IEEE 754 float, big-endian word order (ABCD).
 */
float modbus_regs_to_float(const uint8_t *buf, uint16_t offset);

/**
 * @brief Combine two consecutive 16-bit register values into uint32_t.
 *        Big-endian word order (ABCD).
 */
uint32_t modbus_regs_to_uint32(const uint8_t *buf, uint16_t offset);

/**
 * @brief Combine two consecutive 16-bit register values into uint32_t.
 *        Elmeasure Long Inverse word order (CDAB) — low word first.
 *        Use for: Running Hours, Load Hours, No. of Interruptions.
 */
uint32_t modbus_regs_to_uint32_long_inverse(const uint8_t *buf, uint16_t offset);

/**
 * @brief Combine one 16-bit register value into uint16_t.
 */
uint16_t modbus_reg_to_uint16(const uint8_t *buf, uint16_t offset);

#endif /* MODBUS_MASTER_H */
