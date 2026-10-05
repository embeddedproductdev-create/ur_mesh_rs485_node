#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/*===========================================================
 * String / buffer length constants
 *===========================================================*/
#define SERIAL_NO_LEN              16
#define LOCATION_STR_LEN           20
#define DEVICE_LABEL_LEN           24
#define DEFAULT_DEVICE_SER_NO      "DEV99999"
#define DEFAULT_DEVICE_LOCATION    "EARTH :)"
#define DEFAULT_PUBLISH_PERIOD_SEC 120

/*===========================================================
 *BLE Mesh Device UUID layout
 * [0]    = 0x32 — provisioner match byte 0
 * [1]    = 0x10 — provisioner match byte 1
 * [2-7]  = MAC address (6 bytes, filled at runtime)
 * [8]    = Node type identifier
 * [9-15] = Reserved (zeroed)
 *===========================================================*/
#define UUID_BYTE_MATCH_0       0
#define UUID_BYTE_MATCH_1       1
#define UUID_BYTE_MAC_START     2
#define UUID_BYTE_NODE_TYPE     8

#define UUID_MATCH_BYTE_0       0x32
#define UUID_MATCH_BYTE_1       0x10

#define NODE_TYPE_IR_AC         0x00
#define NODE_TYPE_RS485         0x01
#define NOE_TYPE_GATEWAY        0x02

/* Max parameters per device for dynamic register map */
#define MAX_PARAMS_PER_DEVICE   30

/*===========================================================
 * NVS integer-size tag
 *===========================================================*/
typedef enum
{
    INT8_SIZE,
    UINT8_SIZE,
    INT16_SIZE,
    UINT16_SIZE,
    INT32_SIZE,
    UINT32_SIZE,
    INT64_SIZE,
    UINT64_SIZE,
} sizes_t;

/*===========================================================
 * Error codes
 *===========================================================*/
typedef enum
{
    /* Generic */
    FAILURE                              = -1,
    SUCCESS                              = 0,

    /* Packet parsing */
    JSON_PACKET_INVALID,
    MISSING_PACKET_ID,
    PACKET_ID_EXCEEDING_RANGE,
    MISSING_MSG_SEQ_NO,
    MSG_SEQ_NO_EXCEEDING_RANGE,
    MISSING_LOCATION,
    LOCATION_EXCEEDING_RANGE,
    PACKETID_INVALID_FORMAT,
    MSG_SEQ_NO_INVALID_FORMAT,
    LOCATION_INVALID_FORMAT,

    /* BLE Mesh / Provisioning */
    MISSING_ELEMENT_ADDR,
    ELEMENT_ADDR_EXCEEDING_RANGE,
    ELEMENT_ADDR_INVALID_FORMAT,
    MISSING_MACID,
    INVALID_MACID,
    MACID_INVALID_FORMAT,
    NODE_COMM_TIMEOUT,
    NODE_NOT_PROVISIONED,
    NODE_NOT_FOUND_IN_PROVISIONER_DATABASE,

    /* Device lifecycle */
    DEVICE_DATA_ERASURE_SUCCESSFUL,
    DEVICE_DATA_ERASURE_FAILED,
    MISSING_RESET_DEVICE,
    RESET_DEVICE_EXCEEDING_RANGE,
    RESET_INVALID_FORMAT,
    MISSING_RESTART_DEVICE,
    RESTART_DEVICE_EXCEEDING_RANGE,
    RESTART_INVALID_FORMAT,

    /* Publish period */
    MISSING_PUBLISH_PERIOD,
    PUBLISH_PERIOD_EXCEEDING_RANGE,
    PUBLISH_PERIOD_INVALID_FORMAT,

    /* Debug / misc */
    MISSING_ERROR_CHECK,
    ERROR_CHECK_EXCEEDING_RANGE,

    /* GPIO subsystem */
    GPIO_CHANNEL_INDEX_INVALID,
    GPIO_DIRECTION_INVALID_FORMAT,
    GPIO_SET_ON_INPUT_CHANNEL_REJECTED,
    GPIO_PULL_INVALID_FORMAT,

    /* Modbus / RS485 */
    MODBUS_TIMEOUT,
    MODBUS_CRC_ERROR,
    MODBUS_EXCEPTION_RESPONSE,
    MODBUS_INVALID_RESPONSE_LENGTH,
    MODBUS_SLAVE_ADDR_INVALID,
    MODBUS_MODEL_UNKNOWN,
    MODBUS_DEVICE_NOT_FOUND,
    MODBUS_REGISTRY_FULL,
    MODBUS_DEVICE_ALREADY_EXISTS,

} error_codes;



/*===========================================================
 * Device descriptor (one entry per device on the RS485 bus)
 *===========================================================*/
#define MAX_DEVICES_ON_BUS  16

typedef enum
{
    DEVICE_TYPE_ENERGY_METER = 0,
    DEVICE_TYPE_SUMAN_AC     = 1,
} rs485_device_type_t;

/*===========================================================
 * Register data type — how to decode raw register bytes
 *===========================================================*/
typedef enum
{
    REG_TYPE_FLOAT        = 0,  /* 2 regs, IEEE 754 big-endian (ABCD) */
    REG_TYPE_LONG_INVERSE = 1,  /* 2 regs, CDAB word order — Elmeasure run hours */
    REG_TYPE_UINT16       = 2,  /* 1 reg,  16-bit unsigned integer */
    REG_TYPE_UINT32       = 3,  /* 2 regs, big-endian uint32 */
} reg_data_type_t;

/*===========================================================
+ * Meter config action — replaces the old boolean `remove` flag.
+ * Values only — stored on the wire as plain uint8_t (see
+ * register_entry_t.data_type note above for why).
+ *===========================================================*/
typedef enum
{
    METER_ACTION_ADD    = 0,  /* reject with MODBUS_DEVICE_ALREADY_EXISTS if slave_addr already configured */
    METER_ACTION_UPDATE = 1,  /* reject with MODBUS_DEVICE_NOT_FOUND if slave_addr not configured */
    METER_ACTION_REMOVE = 2,  /* reject with MODBUS_DEVICE_NOT_FOUND if slave_addr not configured */
} meter_config_action_t;
/*===========================================================
 * Register entry — one entry per parameter in device map
 *===========================================================*/
typedef struct
{
    uint8_t          param_idx;   /* 0-based index — cloud maps to param name */
    uint8_t          available;   /* 1=read this register, 0=skip             */
    uint16_t         modbus_addr; /* datasheet register address e.g. 40159    */
    uint8_t          data_type;   /* how to read this register                */
} register_entry_t;

typedef struct
{
    uint8_t          slave_addr;
    bool             active;
    rs485_device_type_t device_type;  /* which driver to use */
    char             label[DEVICE_LABEL_LEN];
    uint8_t          reg_count;
    register_entry_t regs[MAX_PARAMS_PER_DEVICE];
    uint8_t          last_fault_reset_active;  /* Suman AC only: cached Reg 10 from last poll */
} device_descriptor_t;

typedef struct
{
    device_descriptor_t devices[MAX_DEVICES_ON_BUS];
    uint8_t             count;
} device_registry_t;   /* Persisted as a single NVS blob */



/*===========================================================
 * Packet IDs — meter node only, no GWY_ mirror
 *===========================================================*/
typedef enum
{
     /* Shared range — matches ble_mesh_ur lte.h exactly */
     NODE_PROV_PACKET                    = 100,
     NODE_CONF_ACK                       = 101,
     NODE_UNPROV_PACKET                  = 102,
     NODE_AC_CONTROL_PACKET              = 103,  /* not used on RS485 node but kept for alignment */
     NODE_MANUAL_AC_CONTROL_ACK          = 104,  /* not used on RS485 node */
     NODE_RECONF_PACKET                  = 105,  /* not used on RS485 node */
     NODE_HEARTBEAT_ACK                  = 106,
     NODE_HEARTBEAT_PUB_CONF_PACKET      = 107,
     NODE_TEACHING_MODE                  = 108,  /* not used on RS485 node */
     NODE_TEACHING_MODE_CMD_SELECTION_PACKET = 109, /* not used on RS485 node */
     NODE_DEBUG_INFO_PACKET              = 110,
     NODE_GENERAL_PACKET                 = 112,
     NODE_AC_CONTROL_ACK                 = 113,  /* not used on RS485 node */
     NODE_GROUP_SUB_PACKET                   = 114,
     NODE_GROUP_UNSUB_PACKET                 = 115,
     NODE_GROUP_SUB_ACK                      = 116,
     NODE_GROUP_UNSUB_ACK                    = 117,
     NODE_GROUP_AC_CONTROL_PACKET            = 118,
     NODE_GROUP_AC_CONTROL_IMMEDIATE_ACK     = 119,
     NODE_GROUP_AC_CONTROL_SUMMARY_ACK       = 120,
     MAX_NODE_PACKET_ID                      = 121,

     /* RS485-specific packets — start at 151, clear of existing range */
     NODE_METER_CONFIG_PACKET            = 151,
     NODE_METER_READING_PACKET           = 152,
     NODE_METER_READING_1P_PACKET        = 153,
     NODE_GPIO_CONFIG_PACKET             = 154,
     NODE_GPIO_SET_PACKET                = 155,
     NODE_GPIO_STATUS_PACKET             = 156,
     NODE_SUMAN_AC_STATUS_PACKET         = 157,
     NODE_SUMAN_AC_CONTROL_PACKET        = 158,
     NODE_SUMAN_AC_CONFIG_PACKET         = 159,

     /* ACKs — all at the end */
     NODE_METER_CONFIG_ACK               = 160,
     NODE_GPIO_CONFIG_ACK                = 161,
     NODE_GPIO_SET_ACK                   = 162,
     NODE_SUMAN_AC_CONTROL_ACK           = 163,
     NODE_SUMAN_AC_CONFIG_ACK           = 164,

     MAX_RS485_PACKET_ID,
     TEST_PACKET                         = 999,
} mqtt_packets;

/*===========================================================
 * GPIO channel count
 *===========================================================*/
#define GPIO_CHANNEL_COUNT 8

typedef struct
{
    uint16_t    packetid;          /* offset 0  — MUST be first */
    uint16_t    msgseqno;          /* offset 2  */
    uint16_t    elemaddr;          /* offset 4  */
    uint8_t     node_type;         /* offset 6  */
    uint16_t    publishPeriodSec;  /* offset 7  */
    int8_t errorcode;         /* offset 9  */
    uint8_t     resetDevice;       /* offset 13 */
    uint8_t     restartDevice;     /* offset 14 */
    uint8_t     majversion;        /* offset 15 */
    uint8_t     minversion;        /* offset 16 */
    uint8_t     patchversion;      /* offset 17 */
    uint8_t     provisioned;       /* offset 18 */
    bool        configured;        /* offset 19 */
    char        deviceName[SERIAL_NO_LEN]; /* offset 20 */
    float       deviceUpTimeHrs;   /* offset 36 */
    esp_err_t   bleErrorCode;      /* offset 40 */
    int8_t      rssi;              /* offset 44 */
} RS485CmdStruct;

/*===========================================================
 * Dynamic register reading entry
 * Cloud sends register map at commissioning.
 * Node reads raw bytes and sends back — cloud decodes.
 * param_idx: cloud maps this to param name.
 * modbus_addr: datasheet register address e.g. 40159.
 * raw: 4 bytes as-is from meter wire (IEEE 754 or uint32).
 *===========================================================*/
typedef struct
{
    uint8_t  param_idx;    /* index into device register map (0-based) */
    uint8_t available;
    uint16_t modbus_addr;
    uint32_t raw;          /* raw 4-byte value from meter wire          */
} meter_reading_entry_t;


typedef struct
{
    uint16_t         packetid;      /* NODE_METER_CONFIG_PACKET = 151 */
    uint16_t         msgseqno;
    uint16_t         elemaddr;
    uint8_t          node_type;     /* NODE_TYPE_RS485 */
    uint8_t          slave_addr;
    uint8_t          device_type;  /* rs485_device_type_t */
    uint8_t          action;        
    char             label[DEVICE_LABEL_LEN];
    uint8_t          reg_count;
    register_entry_t regs[MAX_PARAMS_PER_DEVICE];
    char             deviceName[SERIAL_NO_LEN];
} MeterConfigCmd;

/*===========================================================
 * Modbus reading p acket — dynamic, covers all meter types
 * 3-phase, single-phase: NODE_METER_READING_PACKET (152)
 * Single-phase:          NODE_METER_READING_1P_PACKET (153)
 * ~118 bytes for 11 params — 10 BLE Mesh segments ✅
 *===========================================================*/
typedef struct
{
    uint16_t              packetid;     /* 152=3-phase, 153=single-phase */
    uint16_t              msgseqno;
    uint16_t              elemaddr;
    uint8_t               node_type;
    uint8_t               slave_addr;
    char                  label[DEVICE_LABEL_LEN];
    uint8_t               reading_count;
    meter_reading_entry_t readings[MAX_PARAMS_PER_DEVICE];
    int8_t           errorcode;
    char                  deviceName[SERIAL_NO_LEN];
} ModbusReadingPkt;

/*===========================================================
+ * Suman Electronics AC Controller packets
+ *===========================================================*/
typedef struct
{
    uint16_t    packetid;           /* NODE_SUMAN_AC_STATUS_PACKET = 157 */
    uint16_t    msgseqno;
    uint16_t    elemaddr;
    uint8_t     node_type;          /* NODE_TYPE_RS485 */
    uint8_t     slave_addr;
    char        label[DEVICE_LABEL_LEN];
    bool        is_event_driven;    /* true = fault/clear triggered, false = periodic */

    /* Writable registers (Reg 1-5) */
    uint16_t    pc_set_temp;        /* Reg 1: 19-32°C */
    uint16_t    pc_operating_mode;  /* Reg 2: mode bits */
    uint16_t    control_option;     /* Reg 4: 0=PC, 1=handset */
    uint16_t    handset_ctrl_time;  /* Reg 5: minutes */

    /* Read-only config (Reg 6-7) */
    uint16_t    num_compressors;    /* Reg 6 */
    uint16_t    model_type;         /* Reg 7: machine/SPPR/ODU config bits */

    /* Read-only status (Reg 8-26) */
    uint16_t    ctrl_set_temp;      /* Reg 8 */
    uint16_t    ctrl_operating_mode;/* Reg 9 */
    uint16_t    fault_reset_active; /* Reg 10 */
    uint16_t    return_air_temp;    /* Reg 11: RAT °C */
    uint16_t    comp1_run_hours;    /* Reg 12 */
    uint16_t    comp2_run_hours;    /* Reg 13 */
    uint16_t    fan_run_hours;      /* Reg 14 */
    uint16_t    compressor_status;  /* Reg 15: D0=C1, D1=C2 */
    uint16_t    fan_status;         /* Reg 16: D0=on, D1/D2=speed */
    uint16_t    alarm_status;       /* Reg 17: D0=alarm present */
    uint16_t    system_fault;       /* Reg 18: SPPR/ODU faults */
    uint16_t    sensor_fault;       /* Reg 19: RAT/SAT/Humidity faults */
    uint16_t    comp1_fault;        /* Reg 20: HP/LP/overload */
    uint16_t    comp2_fault;        /* Reg 21 */
    uint16_t    supply_air_temp;    /* Reg 22: SAT °C */
    uint16_t    humidity1;          /* Reg 23: % */
    uint16_t    humidity2;          /* Reg 24: % */
    uint16_t    comp1_current;      /* Reg 25: A */
    uint16_t    comp2_current;      /* Reg 26: A */

    int8_t      errorcode;
    char        deviceName[SERIAL_NO_LEN];
} SumanAcStatusPkt;

typedef struct
{
    uint16_t    packetid;           /* NODE_SUMAN_AC_CONTROL_PACKET = 158 */
    uint16_t    msgseqno;
    uint16_t    elemaddr;
    uint8_t     node_type;          /* NODE_TYPE_RS485 */
    uint8_t     slave_addr;
    uint8_t     write_mask;         /* bitmask: which registers to write
                                       bit 0 = set_temp
                                       bit 1 = operating_mode
                                       bit 2 = reset_fault
                                       bit 3 = control_option
                                       bit 4 = handset_ctrl_time          */
    uint16_t    set_temp;           /* Reg 1: 19-32 */
    uint16_t    operating_mode;     /* Reg 2: mode bits */
    uint16_t    reset_fault;        /* Reg 3: 1=reset */
    uint16_t    control_option;     /* Reg 4: 0=PC, 1=handset */
    uint16_t    handset_ctrl_time;  /* Reg 5: minutes */
    char        deviceName[SERIAL_NO_LEN];
} SumanAcControlCmd;

/*===========================================================
 * Suman AC config command — cloud → node: add/remove device
 * NODE_SUMAN_AC_CONFIG_PACKET = 159
 *===========================================================*/
typedef struct
{
    uint16_t              packetid;    /* NODE_SUMAN_AC_CONFIG_PACKET = 159 */
    uint16_t              msgseqno;
    uint16_t              elemaddr;
    uint8_t               node_type;
    uint8_t               slave_addr;
    meter_config_action_t action;
    char                  label[DEVICE_LABEL_LEN];
    char                  deviceName[SERIAL_NO_LEN];
} SumanAcConfigCmd;

/*===========================================================
 * Suman AC config ACK — node → cloud
 * NODE_SUMAN_AC_CONFIG_ACK_PACKET = 164
 *===========================================================*/
typedef struct
{
    uint16_t              packetid;
    uint16_t              msgseqno;
    uint16_t              elemaddr;
    uint8_t               node_type;
    uint8_t               slave_addr;
    meter_config_action_t action;
    char                  label[DEVICE_LABEL_LEN];
    uint8_t               total_devices;
    int8_t                errorcode;
    char                  deviceName[SERIAL_NO_LEN];
} SumanAcConfigAck;

/*===========================================================
 * Suman AC control ACK — node → cloud: full state after write
 * NODE_SUMAN_AC_CONTROL_ACK_PACKET = 163
 *===========================================================*/
typedef struct
{
    uint16_t    packetid;
    uint16_t    msgseqno;
    uint16_t    elemaddr;
    uint8_t     node_type;
    uint8_t     slave_addr;
    uint8_t     write_mask;

    /* Full current state after write */
    uint16_t    pc_set_temp;
    uint16_t    pc_operating_mode;
    uint16_t    control_option;
    uint16_t    handset_ctrl_time;
    uint16_t    ctrl_set_temp;
    uint16_t    ctrl_operating_mode;
    uint16_t    fault_reset_active;
    uint16_t    return_air_temp;
    uint16_t    compressor_status;
    uint16_t    fan_status;
    uint16_t    alarm_status;
    uint16_t    system_fault;
    uint16_t    sensor_fault;
    uint16_t    comp1_fault;
    uint16_t    comp2_fault;

    int8_t      errorcode;
    char        deviceName[SERIAL_NO_LEN];
} SumanAcControlAck;

/*===========================================================
 * GPIO command structs
 *===========================================================*/
typedef struct
{
    uint16_t packetid;
    uint16_t msgseqno;
    uint16_t elemaddr;
    uint8_t  node_type;
    uint8_t  channel_mask;
    uint8_t  direction_mask;   /* bit N=1 → CH N is OUTPUT, bit N=0 → INPUT */
    uint8_t  pull_up_mask;     /* bit N=1 → CH N has pull-up enabled */
    uint8_t  pull_down_mask;   /* bit N=1 → CH N has pull-down enabled */
    char     deviceName[SERIAL_NO_LEN];
} GpioConfigCmd;

typedef struct
{
    uint16_t packetid;
    uint16_t msgseqno;
    uint16_t elemaddr;
    uint8_t  node_type;
    uint8_t  channel_mask;
    uint8_t  level_mask;     /* bit N=1 → CH N HIGH, bit N=0 → CH N LOW */
    char     deviceName[SERIAL_NO_LEN];
} GpioSetCmd;

typedef struct
{
    uint16_t    packetid;
    uint16_t    msgseqno;
    uint16_t    elemaddr;
    uint8_t     node_type;
    uint8_t     channel_states;
    uint8_t     channel_directions;
    int8_t      errorcode;
    char        deviceName[SERIAL_NO_LEN];
} GpioStatusPkt;

/*===========================================================
 * BLE Mesh publish queue types
 *===========================================================*/
 #define BLE_PUBLISH_QUEUE_DEPTH     24
#define BLE_PUBLISH_TASK_STACK      4096
#define BLE_PUBLISH_RETRY_COUNT     3
#define BLE_PUBLISH_RETRY_DELAY_MS  100
#define BLE_PUBLISH_INTER_PKT_MS    50


typedef enum {
    PUBLISH_PKT_CMD_ACK,
    PUBLISH_PKT_MODBUS_READING,
    PUBLISH_PKT_GPIO_STATUS,
    PUBLISH_PKT_SUMAN_AC,
    PUBLISH_PKT_SUMAN_AC_CONFIG_ACK,
    PUBLISH_PKT_SUMAN_AC_CONTROL_ACK,
} publish_pkt_type_t;

typedef union {
    RS485CmdStruct    cmd_ack;
    ModbusReadingPkt  modbus_reading;
    GpioStatusPkt     gpio_status;
    SumanAcStatusPkt  suman_ac;
    SumanAcConfigAck  suman_ac_config_ack;
    SumanAcControlAck suman_ac_control_ack;
} publish_pkt_payload_t;

typedef struct {
    publish_pkt_type_t    type;
    publish_pkt_payload_t payload;
} publish_queue_item_t;

bool ble_publish_enqueue(publish_pkt_type_t type, const void *pkt);   /* ADD here — after types defined */
#endif /* COMMON_TYPES_H */
