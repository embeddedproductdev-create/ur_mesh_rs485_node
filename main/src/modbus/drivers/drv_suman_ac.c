#include <string.h>
#include "esp_log.h"
#include "driver/uart.h"

#include "common_types.h"
#include "modbus/modbus_master.h"
#include "modbus/drivers/drv_suman_ac.h"

#define DRV_SUMAN_TAG  "DRV_SUMAN_AC"

/*===========================================================
 * Internal helper — read one uint16 register value from
 * the FC03 response buffer (big-endian, 0-based index)
 *===========================================================*/
static inline uint16_t buf_u16(const uint8_t *buf, uint8_t reg_num)
{
    uint16_t offset = (reg_num - 1) * 2;  /* reg_num is 1-based */
    return ((uint16_t)buf[offset] << 8) | buf[offset + 1];
}

/*===========================================================
 * Internal helper — FC06 write single holding register
 * Builds the 8-byte RTU frame and sends via modbus_master.
 *===========================================================*/
static error_codes suman_write_single_reg(uint8_t slave_addr,
                                          uint8_t  reg_num,
                                          uint16_t value)
{
    /* modbus_master.c exposes modbus_write_single_register() */
    error_codes result = modbus_write_single_register(slave_addr,
                                                      reg_num - 1,
                                                      value);
    if (result != SUCCESS)
    {
        ESP_LOGE(DRV_SUMAN_TAG,
                 "Write failed slave=0x%02x reg=%d val=%d err=%d",
                 slave_addr, reg_num, value, result);
    }
    else
    {
        ESP_LOGI(DRV_SUMAN_TAG,
                 "Wrote slave=0x%02x reg=%d val=0x%04x",
                 slave_addr, reg_num, value);
    }

    /* 750ms slave response delay — give controller time after each write */
    vTaskDelay(pdMS_TO_TICKS(MODBUS_RESPONSE_TIMEOUT_MS));
    return result;
}

/*===========================================================
 * Public API — Read all 26 registers
 *===========================================================*/
error_codes drv_suman_ac_read(uint8_t slave_addr, SumanAcStatusPkt *out)
{
    uint8_t buf[SUMAN_REG_COUNT * 2] = {0};

    error_codes result = modbus_read_holding_regs(slave_addr,
                                                  SUMAN_REG_START - 1,
                                                  SUMAN_REG_COUNT,
                                                  buf);
    if (result != SUCCESS)
    {
        ESP_LOGE(DRV_SUMAN_TAG,
                 "Read failed slave=0x%02x err=%d", slave_addr, result);
        out->errorcode = (int8_t)result;
        return result;
    }

    /* Writable registers (Reg 1-5) */
    out->pc_set_temp        = buf_u16(buf, SUMAN_REG_PC_SET_TEMP);
    out->pc_operating_mode  = buf_u16(buf, SUMAN_REG_PC_OPERATING_MODE);
    out->control_option     = buf_u16(buf, SUMAN_REG_CONTROL_OPTION);
    out->handset_ctrl_time  = buf_u16(buf, SUMAN_REG_HANDSET_CTRL_TIME);

    /* Read-only config (Reg 6-7) */
    out->num_compressors    = buf_u16(buf, SUMAN_REG_NUM_COMPRESSORS);
    out->model_type         = buf_u16(buf, SUMAN_REG_MODEL_TYPE);

    /* Read-only status (Reg 8-26) */
    out->ctrl_set_temp      = buf_u16(buf, SUMAN_REG_CTRL_SET_TEMP);
    out->ctrl_operating_mode= buf_u16(buf, SUMAN_REG_CTRL_OPERATING_MODE);
    out->fault_reset_active = buf_u16(buf, SUMAN_REG_FAULT_RESET_ACTIVE);
    out->return_air_temp    = buf_u16(buf, SUMAN_REG_RETURN_AIR_TEMP);
    out->comp1_run_hours    = buf_u16(buf, SUMAN_REG_COMP1_RUN_HOURS);
    out->comp2_run_hours    = buf_u16(buf, SUMAN_REG_COMP2_RUN_HOURS);
    out->fan_run_hours      = buf_u16(buf, SUMAN_REG_FAN_RUN_HOURS);
    out->compressor_status  = buf_u16(buf, SUMAN_REG_COMPRESSOR_STATUS);
    out->fan_status         = buf_u16(buf, SUMAN_REG_FAN_STATUS);
    out->alarm_status       = buf_u16(buf, SUMAN_REG_ALARM_STATUS);
    out->system_fault       = buf_u16(buf, SUMAN_REG_SYSTEM_FAULT);
    out->sensor_fault       = buf_u16(buf, SUMAN_REG_SENSOR_FAULT);
    out->comp1_fault        = buf_u16(buf, SUMAN_REG_COMP1_FAULT);
    out->comp2_fault        = buf_u16(buf, SUMAN_REG_COMP2_FAULT);
    out->supply_air_temp    = buf_u16(buf, SUMAN_REG_SUPPLY_AIR_TEMP);
    out->humidity1          = buf_u16(buf, SUMAN_REG_HUMIDITY1);
    out->humidity2          = buf_u16(buf, SUMAN_REG_HUMIDITY2);
    out->comp1_current      = buf_u16(buf, SUMAN_REG_COMP1_CURRENT);
    out->comp2_current      = buf_u16(buf, SUMAN_REG_COMP2_CURRENT);

    out->slave_addr = slave_addr;
    out->node_type  = NODE_TYPE_RS485;
    out->errorcode  = (int8_t)SUCCESS;

    /* Note: temperatures are raw/10 = °C (cloud decodes) */
    /* Note: comp current only valid if SUMAN_MODEL_HAS_ODU(out->model_type) */
    ESP_LOGI(DRV_SUMAN_TAG,
             "slave=0x%02x RAT=%u(×0.1°C) SAT=%u(×0.1°C) SET=%u°C "
             "Mode=0x%04x Fan=0x%04x Comp=0x%04x Alarm=%d "
             "SysFault=0x%04x C1F=0x%04x C2F=0x%04x "
             "H1=%u%% H2=%u%%",
             slave_addr,
             out->return_air_temp, out->supply_air_temp, out->ctrl_set_temp,
             out->ctrl_operating_mode, out->fan_status, out->compressor_status,
             out->alarm_status & 1, out->system_fault,
             out->comp1_fault, out->comp2_fault,
             out->humidity1, out->humidity2);

    return SUCCESS;
}

/*===========================================================
 * Public API — Write writable registers
 *===========================================================*/
error_codes drv_suman_ac_write(uint8_t slave_addr,
                               const SumanAcControlCmd *cmd,
                               uint8_t fault_reset_active)
{
    error_codes result = SUCCESS;

    /*
     * Write order:
     * 1. Control option FIRST — must be in PC mode before writing
     *    mode or temperature, otherwise handset may reject
     * 2. Set temperature (Reg 1)
     * 3. Operating mode (Reg 2) — undefined bits must be 0
     * 4. Reset fault (Reg 3) — ONLY if Reg 10 was 1 in last read
     * 5. Handset control time (Reg 5)
     */

    if (cmd->write_mask & SUMAN_WRITE_CONTROL_OPTION)
    {
        result = suman_write_single_reg(slave_addr,
                                        SUMAN_REG_CONTROL_OPTION,
                                        cmd->control_option);
        if (result != SUCCESS) return result;
    }

    if (cmd->write_mask & SUMAN_WRITE_SET_TEMP)
    {
        if (cmd->set_temp < 19 || cmd->set_temp > 32)
        {
            ESP_LOGE(DRV_SUMAN_TAG,
                     "set_temp=%d out of range 19-32", cmd->set_temp);
            return FAILURE;
        }
        result = suman_write_single_reg(slave_addr,
                                        SUMAN_REG_PC_SET_TEMP,
                                        cmd->set_temp);
        if (result != SUCCESS) return result;
    }

    if (cmd->write_mask & SUMAN_WRITE_OPERATING_MODE)
    {
        /* Undefined bits must be 0 — mask to only defined bits */
        uint16_t safe_mode = cmd->operating_mode & 0x0027;
        /* 0x0027 = 0b0000000000100111 → D0, D1, D2, D5 only */
        if (safe_mode != cmd->operating_mode)
            ESP_LOGW(DRV_SUMAN_TAG,
                     "operating_mode 0x%04x has undefined bits set — masking to 0x%04x",
                     cmd->operating_mode, safe_mode);

        result = suman_write_single_reg(slave_addr,
                                        SUMAN_REG_PC_OPERATING_MODE,
                                        safe_mode);
        if (result != SUCCESS) return result;
    }

    if (cmd->write_mask & SUMAN_WRITE_RESET_FAULT)
    {
        if (!fault_reset_active)
        {
            /* Reg 10 was 0 — controller not ready to reset */
            ESP_LOGW(DRV_SUMAN_TAG,
                     "slave=0x%02x fault reset requested but Reg10=0 — skipping",
                     slave_addr);
        }
        else
        {
            result = suman_write_single_reg(slave_addr,
                                            SUMAN_REG_RESET_COMMAND,
                                            1);
            if (result != SUCCESS) return result;
            /* Controller auto-clears Reg 3 — no need to write 0 back */
            ESP_LOGI(DRV_SUMAN_TAG,
                     "slave=0x%02x fault reset sent", slave_addr);
        }
    }

    if (cmd->write_mask & SUMAN_WRITE_HANDSET_CTRL_TIME)
    {
        if (cmd->handset_ctrl_time == 0)
        {
            ESP_LOGE(DRV_SUMAN_TAG,
                     "handset_ctrl_time=0 invalid, range 1-65535");
            return FAILURE;
        }
        result = suman_write_single_reg(slave_addr,
                                        SUMAN_REG_HANDSET_CTRL_TIME,
                                        cmd->handset_ctrl_time);
        if (result != SUCCESS) return result;
    }

    return SUCCESS;
}