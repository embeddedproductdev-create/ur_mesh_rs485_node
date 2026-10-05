#ifndef DRV_SUMAN_AC_H
#define DRV_SUMAN_AC_H

#include <stdint.h>
#include "common_types.h"

/*===========================================================
 * Suman Electronics AC Controller — Modbus RTU Driver
 * Document: MODBUS DATA FORMAT Version 01/02 (28/08/2026)
 *
 * Communication: 9600 baud, 8N2, RTU
 * BMS Port: RS485 A/B/GND on INPUT BOX
 * Slave response delay: 750ms minimum
 * All registers: Holding Registers (FC03 read, FC06 write)
 * Register count: 26
 *===========================================================*/

/*===========================================================
 * Register addresses (1-based, per datasheet)
 *===========================================================*/
#define SUMAN_REG_PC_SET_TEMP           1  /* R/W: 19-32°C */
#define SUMAN_REG_PC_OPERATING_MODE     2  /* R/W: mode bits */
#define SUMAN_REG_RESET_COMMAND         3  /* R/W: 1=reset, write only if Reg10=1 */
#define SUMAN_REG_CONTROL_OPTION        4  /* R/W: 0=PC, else handset */
#define SUMAN_REG_HANDSET_CTRL_TIME     5  /* R/W: 1-65535 minutes */
#define SUMAN_REG_NUM_COMPRESSORS       6  /* R only: D0=0→1comp, D0=1→2comp */
#define SUMAN_REG_MODEL_TYPE            7  /* R only: D0=machine, D1=SPPR, D2=ODU */
#define SUMAN_REG_CTRL_SET_TEMP         8  /* R only: 19-32°C (handset value) */
#define SUMAN_REG_CTRL_OPERATING_MODE   9  /* R only: actual mode bits */
#define SUMAN_REG_FAULT_RESET_ACTIVE    10 /* R only: 1=fault can be reset */
#define SUMAN_REG_RETURN_AIR_TEMP       11 /* R only: raw/10 = °C; 0 on sensor fault */
#define SUMAN_REG_COMP1_RUN_HOURS       12 /* R only: 16-bit hours, EEPROM, rolls@65535 */
#define SUMAN_REG_COMP2_RUN_HOURS       13 /* R only: 16-bit hours */
#define SUMAN_REG_FAN_RUN_HOURS         14 /* R only: 16-bit hours */
#define SUMAN_REG_COMPRESSOR_STATUS     15 /* R only: D0=C1, D1=C2, D2=C3(Package only) */
#define SUMAN_REG_FAN_STATUS            16 /* R only: D0=Package fan; IDU: D1/D2=speed */
#define SUMAN_REG_ALARM_STATUS          17 /* R only: D0=alarm; D1-D15 always 0 */
#define SUMAN_REG_SYSTEM_FAULT          18 /* R only: D0=SPPR, D1=BF_SPPR, D2=ODU1, D3=ODU2 */
#define SUMAN_REG_SENSOR_FAULT          19 /* R only: D0=RAT, D1=SAT, D2=HUM1, D3=HUM2 */
#define SUMAN_REG_COMP1_FAULT           20 /* R only: D0=ODU1_SPPR, D1=HP/LP1, D2=HP1, D3=LP1, D4=OVL, D5=UDL */
#define SUMAN_REG_COMP2_FAULT           21 /* R only: same pattern for comp2 */
#define SUMAN_REG_SUPPLY_AIR_TEMP       22 /* R only: raw/10 = °C; 0 on sensor fault */
#define SUMAN_REG_HUMIDITY1             23 /* R only: raw integer % (52=52%) */
#define SUMAN_REG_HUMIDITY2             24 /* R only: raw integer % */
#define SUMAN_REG_COMP1_CURRENT         25 /* R only: valid only with ODU; raw value */
#define SUMAN_REG_COMP2_CURRENT         26 /* R only: valid only with ODU; raw value */

#define SUMAN_REG_START                 1
#define SUMAN_REG_COUNT                 26

/*===========================================================
 * Operating Mode register bit definitions (Reg 2 / Reg 9)
 * Undefined bits (D3, D4, D6-D15) MUST be written as 0 —
 * non-zero on undefined bits causes controller to ignore command
 *===========================================================*/
#define SUMAN_MODE_BIT_ON_OFF           0   /* D0: 0=OFF, 1=ON */
#define SUMAN_MODE_BIT_COOL_FAN         5   /* D5: 0=Fan, 1=Cool */
/* Fan speed: D2:D1 = 00=Low, 01=Medium, 10=High */

/* Helper macros — build complete mode register value */
#define SUMAN_MODE_OFF                  0x0000
#define SUMAN_MODE_FAN_LOW              0x0001  /* ON | Fan | Low  (D0=1, D5=0, D2:D1=00) */
#define SUMAN_MODE_FAN_MED              0x0003  /* ON | Fan | Med  (D0=1, D5=0, D2:D1=01) */
#define SUMAN_MODE_FAN_HIGH             0x0005  /* ON | Fan | High (D0=1, D5=0, D2:D1=10) */
#define SUMAN_MODE_COOL_LOW             0x0021  /* ON | Cool | Low  */
#define SUMAN_MODE_COOL_MED             0x0023  /* ON | Cool | Med  */
#define SUMAN_MODE_COOL_HIGH            0x0025  /* ON | Cool | High */

/*===========================================================
 * write_mask bit definitions for SumanAcControlCmd
 *===========================================================*/
#define SUMAN_WRITE_SET_TEMP            (1 << 0)  /* write Reg 1 */
#define SUMAN_WRITE_OPERATING_MODE      (1 << 1)  /* write Reg 2 */
#define SUMAN_WRITE_RESET_FAULT         (1 << 2)  /* write Reg 3 */
#define SUMAN_WRITE_CONTROL_OPTION      (1 << 3)  /* write Reg 4 */
#define SUMAN_WRITE_HANDSET_CTRL_TIME   (1 << 4)  /* write Reg 5 */

/*===========================================================
 * Control option values
 *===========================================================*/
#define SUMAN_CTRL_PC                   0   /* Reg 4: PC controls */
#define SUMAN_CTRL_HANDSET              1   /* Reg 4: Handset controls */

/*===========================================================
 * Model type helper macros (Reg 7 bit decode)
 *===========================================================*/
#define SUMAN_MODEL_IS_CARRIER(reg7)    ((reg7) & (1 << 0))  /* 1=Carrier, 0=Bluestar */
#define SUMAN_MODEL_EXT_SPPR(reg7)      ((reg7) & (1 << 1))  /* 1=External SPPR */
#define SUMAN_MODEL_HAS_ODU(reg7)       ((reg7) & (1 << 2))  /* 1=Has ODU */

/*===========================================================
 * Number of compressors helper (Reg 6)
 *===========================================================*/
#define SUMAN_NUM_COMP(reg6)            (((reg6) & 1) ? 2 : 1)

/*===========================================================
 * Fan status helper macros (Reg 16)
 * D0 valid on Package machines only
 * IDU machines: read D2:D1 for speed
 *===========================================================*/
#define SUMAN_FAN_ON_PACKAGE(reg16)     ((reg16) & (1 << 0))
#define SUMAN_FAN_SPEED_IDU(reg16)      (((reg16) >> 1) & 0x03) /* 0=Low,1=Med,2=High */

/*===========================================================
 * Public API
 *===========================================================*/

/**
 * @brief Read all 26 registers from the Suman AC controller.
 *
 * Single FC03 sequential read (Reg 1-26).
 * Populates all fields of SumanAcStatusPkt.
 * is_event_driven flag must be set by the caller (poll task).
 *
 * @param slave_addr  Modbus slave address (1-247)
 * @param out         Caller-allocated SumanAcStatusPkt, zeroed before call
 * @return SUCCESS or MODBUS_* error code
 */
error_codes drv_suman_ac_read(uint8_t slave_addr, SumanAcStatusPkt *out);

/**
 * @brief Write one or more writable registers to the Suman AC controller.
 *
 * Uses FC06 (Write Single Register) per flagged register.
 * Write order: Reg 4 (control_option) first if flagged, then 1, 2, 3, 5.
 *
 * IMPORTANT: Reg 3 (reset_fault) is only written if Reg 10
 * (fault_reset_active) was 1 in the last read — the caller must
 * ensure this. The driver validates and skips if write_mask has
 * SUMAN_WRITE_RESET_FAULT set but the controller is not ready.
 *
 * Writing Reg 1 or Reg 2 resets compressor/fan run hours on the
 * controller — the cloud must be aware of this side effect.
 *
 * @param slave_addr         Modbus slave address
 * @param cmd                Control command with write_mask and values
 * @param fault_reset_active Value of Reg 10 from last read (0 or 1)
 * @return SUCCESS or MODBUS_* error code on first failed write
 */
error_codes drv_suman_ac_write(uint8_t slave_addr,
                               const SumanAcControlCmd *cmd,
                               uint8_t fault_reset_active);

#endif /* DRV_SUMAN_AC_H */