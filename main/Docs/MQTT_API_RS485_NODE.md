# MQTT API Documentation — RS485 Energy Meter Node
**Project:** Unimation BLE Mesh IoT System
**Version:** 1.0
**Date:** 2026-08-27
**Audience:** Full-Stack Developer

---

## 1. Overview

The system consists of two types of BLE Mesh nodes connected to a central Gateway:

| Node Type | Description | Node Type ID |
|---|---|---|
| **IR AC Node** | Controls split AC units via infrared | `1` |
| **RS485 Meter Node** | Reads energy meters via Modbus RS485 | `2` |

Both node types communicate with the cloud through the **same Gateway** over **the same MQTT topics**. The `NodeType` field in every packet payload identifies which type of node generated the message.

---

## 2. MQTT Topics

| Topic | Direction | Description |
|---|---|---|
| `{GwySerNo}/command` | Cloud → Gateway | Cloud sends commands to gateway (for any node) |
| `{GwySerNo}/message` | Gateway → Cloud | Gateway publishes all messages to cloud |
| `{GwySerNo}/alive` | Gateway → Cloud | Gateway keepalive heartbeat |
| `GWYS/will` | Gateway → Broker | Last Will Testament — published on gateway disconnect |

**`{GwySerNo}`** is the serial number of the gateway unit e.g. `GWY00012`.

All RS485 meter node packets are published on `{GwySerNo}/message`. The cloud differentiates them from IR AC node packets using `JsonPacketID` and `NodeType` fields inside the payload.

---

## 3. Common Fields

Every MQTT JSON payload (both directions) contains these common fields:

| Field | Type | Description |
|---|---|---|
| `JsonPacketID` | integer | Packet type identifier (see Section 4) |
| `MsgSeqNo` | integer | Monotonically incrementing sequence number per node |
| `GwySerNo` | string | Gateway serial number e.g. `"GWY00012"` |
| `NodeSerNo` | string | Node serial number e.g. `"NODE00001"` |
| `ElementAddr` | integer | BLE Mesh element address assigned to this node |
| `NodeType` | integer | `1` = IR AC Node, `2` = RS485 Meter Node |
| `ErrorCode` | integer | `0` = SUCCESS, non-zero = error (see Section 9) |
| `ErrorMsg` | string | Human-readable error description |

---

## 4. Packet ID Reference

### IR AC Node Packets (existing — unchanged)

| JsonPacketID | Name | Direction |
|---|---|---|
| 0 | GWY_REG_PACKET | Cloud → GW |
| 1 | GWY_CONF_ACK | GW → Cloud |
| 2 | GWY_UNREG_PACKET | Cloud → GW |
| 3 | GWY_AC_CONTROL_PACKET | Cloud → GW |
| 6 | GWY_HEARTBEAT_ACK | GW → Cloud |
| 7 | GWY_HEARTBEAT_PUB_CONF_PACKET | Cloud → GW |
| 10 | GWY_DEBUG_INFO_PACKET | Cloud → GW |
| 11 | GWY_GENERAL_PACKET | GW → Cloud |
| 13 | GWY_AC_CONTROL_ACK | GW → Cloud |
| 14 | GWY_GROUP_SUB_PACKET | Cloud → GW |
| 15 | GWY_GROUP_UNSUB_PACKET | Cloud → GW |
| 16 | GWY_GROUP_SUB_ACK | GW → Cloud |
| 17 | GWY_GROUP_UNSUB_ACK | GW → Cloud |
| 18 | GWY_GROUP_TABLE_PACKET | Cloud → GW |
| 100 | NODE_PROV_PACKET | GW → Cloud |
| 101 | NODE_CONF_ACK | GW → Cloud |
| 102 | NODE_UNPROV_PACKET | GW → Cloud |
| 103 | NODE_AC_CONTROL_PACKET | Cloud → GW |
| 104 | NODE_MANUAL_AC_CONTROL_ACK | GW → Cloud |
| 105 | NODE_RECONF_PACKET | Cloud → GW |
| 106 | NODE_HEARTBEAT_ACK | GW → Cloud |
| 107 | NODE_HEARTBEAT_PUB_CONF_PACKET | Cloud → GW |
| 108 | NODE_TEACHING_MODE | Cloud → GW |
| 109 | NODE_TEACHING_MODE_CMD_SELECTION_PACKET | Cloud → GW |
| 110 | NODE_DEBUG_INFO_PACKET | Cloud → GW |
| 111 | NODE_GENERAL_PACKET | GW → Cloud |
| 113 | NODE_AC_CONTROL_ACK | GW → Cloud |
| 114 | NODE_GROUP_SUB_PACKET | Cloud → GW |
| 115 | NODE_GROUP_UNSUB_PACKET | Cloud → GW |
| 116 | NODE_GROUP_SUB_ACK | GW → Cloud |
| 117 | NODE_GROUP_UNSUB_ACK | GW → Cloud |
| 118 | NODE_GROUP_AC_CONTROL_PACKET | Cloud → GW |
| 119 | NODE_GROUP_AC_CONTROL_IMMEDIATE_ACK | GW → Cloud |
| 120 | NODE_GROUP_AC_CONTROL_SUMMARY_ACK | GW → Cloud |

### RS485 Meter Node Packets (new)

> **Note:** IDs 121–150 are reserved. RS485-specific packets start at 151 to avoid collision with any future IR AC node packets in the 121–150 range.

| JsonPacketID | Name | Direction | Description |
|---|---|---|---|
| 151 | NODE_METER_CONFIG_PACKET | Cloud → GW → Node | Add / remove energy meter from bus |
| 152 | NODE_METER_READING_PACKET | Node → GW → Cloud | 3-phase meter reading (periodic push) |
| 153 | NODE_METER_READING_1P_PACKET | Node → GW → Cloud | Single-phase meter reading (periodic push) |
| 154 | NODE_GPIO_CONFIG_PACKET | Cloud → GW → Node | Configure GPIO channel direction/pull |
| 155 | NODE_GPIO_SET_PACKET | Cloud → GW → Node | Set GPIO output level |
| 156 | NODE_GPIO_STATUS_PACKET | Node → GW → Cloud | GPIO state change push |

---

## 5. Node Provisioning

### 5.1 How Node Type is Determined

When a node is provisioned into the BLE Mesh network, the gateway reads a **node type identifier embedded in the device UUID** broadcast by the node during unprovisioned advertising. The gateway includes `NodeType` in the provisioning ACK sent to the cloud.

The cloud **must store the mapping** of `ElementAddr → NodeType` at provisioning time. All future commands to that element address must match the node type.

### 5.2 Provisioning ACK — IR AC Node

**Topic:** `{GwySerNo}/message`
**JsonPacketID:** `100`

```json
{
    "JsonPacketID": 100,
    "MsgSeqNo": 1,
    "GwySerNo": "GWY00012",
    "NodeSerNo": "NODE00001",
    "ElementAddr": 3,
    "NodeType": 1,
    "ErrorCode": 0,
    "ErrorMsg": "SUCCESS"
}
```

**Cloud action on receipt:**
- Store: `ElementAddr 3 → NodeType 1 (IR AC Node)`
- Proceed with IR AC node configuration (AC remote teaching, heartbeat config etc.)

### 5.3 Provisioning ACK — RS485 Meter Node

**Topic:** `{GwySerNo}/message`
**JsonPacketID:** `100`

```json
{
    "JsonPacketID": 100,
    "MsgSeqNo": 1,
    "GwySerNo": "GWY00012",
    "NodeSerNo": "NODE00001",
    "ElementAddr": 5,
    "NodeType": 2,
    "ErrorCode": 0,
    "ErrorMsg": "SUCCESS"
}
```

**Cloud action on receipt:**
- Store: `ElementAddr 5 → NodeType 2 (RS485 Meter Node)`
- Proceed to configure energy meters on this node using `NODE_METER_CONFIG_PACKET` (see Section 6)

### 5.4 NodeType Routing Rules

| NodeType | Cloud must send | Cloud must NOT send |
|---|---|---|
| `1` (IR AC) | AC control, teaching mode, reconfig, group sub/unsub/control packets | Meter config, GPIO packets |
| `2` (RS485) | Meter config, GPIO packets | AC control, teaching mode, group packets |

Sending a wrong packet type to the wrong node results in `ErrorCode: PACKET_ID_EXCEEDING_RANGE`.

---

## 6. Energy Meter Configuration (Add / Remove)

### 6.1 Overview

The RS485 Meter Node supports up to **16 energy meters** on a single RS485 bus. Each meter is identified by its **Modbus slave address** (set physically on the device by the technician, range 1–247).

The cloud configures what registers to read from each meter by sending a **register map** — a list of up to 20 register entries. The node stores this in persistent memory and polls the meters autonomously every publish period (default 60 seconds).

This is a **fully dynamic approach** — the node does not have hardcoded knowledge of any meter model. The cloud defines exactly which Modbus registers to read and the node returns the raw values. The cloud is responsible for decoding and scaling the values.

### 6.2 Register Entry Format

Each register entry in the configuration tells the node:

| Field | Type | Description |
|---|---|---|
| `ParamIdx` | integer (0–19) | Index used to identify this parameter in reading packets. Cloud maps this to a parameter name. |
| `ModbusAddr` | integer | Modbus register address as per meter datasheet (e.g. `40101` for Watts Total on Elmeasure LG series) |
| `DataType` | integer | How to decode the raw register value (see table below) |

**DataType values:**

| Value | Name | Description |
|---|---|---|
| `0` | `FLOAT` | 2 registers, IEEE 754 32-bit float, big-endian word order (ABCD). Most energy parameters. |
| `1` | `LONG_INVERSE` | 2 registers, 32-bit unsigned integer, CDAB word order (low word first). Used for Elmeasure run hours register. |
| `2` | `UINT16` | 1 register, 16-bit unsigned integer |
| `3` | `UINT32` | 2 registers, 32-bit unsigned integer, big-endian |

**Raw value decoding:**

The node sends raw register bytes as a `uint32`. The cloud decodes based on `DataType`:

```
DataType = FLOAT:
    raw uint32 → reinterpret bits as IEEE 754 float
    e.g. raw = 1150631936 (0x449A4000) → 1234.0 (kWh after ÷1000)

DataType = LONG_INVERSE:
    raw uint32 → swap high and low 16-bit words → read as uint32 seconds
    e.g. raw = 0x0E100000 → swap → 0x00000E10 = 3600 seconds = 1 hour

DataType = UINT16:
    raw uint32 → lower 16 bits only
    e.g. raw = 50 → 50 Hz

DataType = UINT32:
    raw uint32 → value directly
```

### 6.3 Add Energy Meter

**Topic:** `{GwySerNo}/command`
**JsonPacketID:** `151`

**Payload:**
```json
{
    "JsonPacketID": 151,
    "MsgSeqNo": 1,
    "GwySerNo": "GWY00012",
    "NodeSerNo": "NODE00001",
    "ElementAddr": 5,
    "NodeType": 2,
    "SlaveAddr": 1,
    "Remove": 0,
    "Label": "DB1-INCOMER",
    "RegCount": 10,
    "RegisterMap": [
        {"ParamIdx": 0, "ModbusAddr": 40159, "DataType": 0},
        {"ParamIdx": 1, "ModbusAddr": 40101, "DataType": 0},
        {"ParamIdx": 2, "ModbusAddr": 40103, "DataType": 0},
        {"ParamIdx": 3, "ModbusAddr": 40105, "DataType": 0},
        {"ParamIdx": 4, "ModbusAddr": 40107, "DataType": 0},
        {"ParamIdx": 5, "ModbusAddr": 40353, "DataType": 0},
        {"ParamIdx": 6, "ModbusAddr": 40151, "DataType": 0},
        {"ParamIdx": 7, "ModbusAddr": 40153, "DataType": 0},
        {"ParamIdx": 8, "ModbusAddr": 40155, "DataType": 0},
        {"ParamIdx": 9, "ModbusAddr": 40227, "DataType": 1}
    ]
}
```

**Field descriptions:**

| Field | Type | Required | Description |
|---|---|---|---|
| `SlaveAddr` | integer | Yes | Modbus slave address set on the physical meter (1–247) |
| `Remove` | integer | Yes | `0` = add/update, `1` = remove |
| `Label` | string | Yes | Human-readable label, max 11 chars e.g. `"DB1-INCOMER"` |
| `RegCount` | integer | Yes | Number of entries in `RegisterMap` (1–20) |
| `RegisterMap` | array | Yes | Array of register entries (see 6.2) |

**Notes:**
- If a meter with the same `SlaveAddr` already exists in the node registry, this command **updates** it (register map replaced)
- `Label` is stored in the node and echoed back in reading packets — useful for identifying which meter a reading belongs to
- `ParamIdx` values must be unique within a device's register map
- `ParamIdx` order in the array does not need to be sequential, but must be consistent — the cloud uses `ParamIdx` to map readings back to parameter names

**Example Elmeasure LG Series register map** (Watts Total, Watts R/Y/B, True PF, Current R/Y/B, Running Hours):

| ParamIdx | Parameter | ModbusAddr | DataType | Cloud scale |
|---|---|---|---|---|
| 0 | Import Energy | 40159 | FLOAT (0) | ÷1000 → kWh |
| 1 | Active Power Total | 40101 | FLOAT (0) | direct → kW |
| 2 | Active Power R | 40103 | FLOAT (0) | direct → kW |
| 3 | Active Power Y | 40105 | FLOAT (0) | direct → kW |
| 4 | Active Power B | 40107 | FLOAT (0) | direct → kW |
| 5 | True PF Average | 40353 | FLOAT (0) | direct |
| 6 | Current R | 40151 | FLOAT (0) | direct → A |
| 7 | Current Y | 40153 | FLOAT (0) | direct → A |
| 8 | Current B | 40155 | FLOAT (0) | direct → A |
| 9 | Running Hours | 40227 | LONG_INVERSE (1) | ÷3600 → hours |

### 6.4 Add Meter — ACK

**Topic:** `{GwySerNo}/message`
**JsonPacketID:** `151`

**Success:**
```json
{
    "JsonPacketID": 151,
    "MsgSeqNo": 1,
    "GwySerNo": "GWY00012",
    "NodeSerNo": "NODE00001",
    "ElementAddr": 5,
    "NodeType": 2,
    "SlaveAddr": 1,
    "Action": "ADD",
    "Label": "DB1-INCOMER",
    "RegCount": 10,
    "TotalDevices": 1,
    "ErrorCode": 0,
    "ErrorMsg": "SUCCESS"
}
```

**Failure — registry full (16 devices already configured):**
```json
{
    "JsonPacketID": 151,
    "MsgSeqNo": 1,
    "GwySerNo": "GWY00012",
    "NodeSerNo": "NODE00001",
    "ElementAddr": 5,
    "NodeType": 2,
    "SlaveAddr": 1,
    "Action": "ADD",
    "Label": "DB1-INCOMER",
    "RegCount": 10,
    "TotalDevices": 16,
    "ErrorCode": 36,
    "ErrorMsg": "MODBUS_REGISTRY_FULL"
}
```

**Failure — invalid slave address:**
```json
{
    "JsonPacketID": 151,
    "MsgSeqNo": 1,
    "GwySerNo": "GWY00012",
    "NodeSerNo": "NODE00001",
    "ElementAddr": 5,
    "NodeType": 2,
    "SlaveAddr": 0,
    "Action": "ADD",
    "Label": "",
    "RegCount": 0,
    "TotalDevices": 1,
    "ErrorCode": 34,
    "ErrorMsg": "MODBUS_SLAVE_ADDR_INVALID"
}
```

### 6.5 Remove Energy Meter

**Topic:** `{GwySerNo}/command`
**JsonPacketID:** `151`

```json
{
    "JsonPacketID": 151,
    "MsgSeqNo": 2,
    "GwySerNo": "GWY00012",
    "NodeSerNo": "NODE00001",
    "ElementAddr": 5,
    "NodeType": 2,
    "SlaveAddr": 1,
    "Remove": 1,
    "Label": "",
    "RegCount": 0,
    "RegisterMap": []
}
```

When `Remove = 1`, `Label`, `RegCount`, and `RegisterMap` are ignored by the node.

### 6.6 Remove Meter — ACK

```json
{
    "JsonPacketID": 151,
    "MsgSeqNo": 2,
    "GwySerNo": "GWY00012",
    "NodeSerNo": "NODE00001",
    "ElementAddr": 5,
    "NodeType": 2,
    "SlaveAddr": 1,
    "Action": "REMOVE",
    "Label": "",
    "RegCount": 0,
    "TotalDevices": 0,
    "ErrorCode": 0,
    "ErrorMsg": "SUCCESS"
}
```

**Failure — device not found:**
```json
{
    "JsonPacketID": 151,
    "MsgSeqNo": 2,
    "GwySerNo": "GWY00012",
    "NodeSerNo": "NODE00001",
    "ElementAddr": 5,
    "NodeType": 2,
    "SlaveAddr": 99,
    "Action": "REMOVE",
    "Label": "",
    "RegCount": 0,
    "TotalDevices": 1,
    "ErrorCode": 35,
    "ErrorMsg": "MODBUS_DEVICE_NOT_FOUND"
}
```

---

## 7. Energy Meter Reading (Periodic Push)

### 7.1 Overview

Once configured, the node autonomously polls each energy meter on the RS485 bus every **publish period** (default 60 seconds, configurable via heartbeat config packet). Each meter gets its own reading packet — if 4 meters are configured, the cloud receives 4 separate reading packets per cycle.

**No command from the cloud is required** — readings are pushed automatically after commissioning.

### 7.2 3-Phase Meter Reading

**Topic:** `{GwySerNo}/message`
**JsonPacketID:** `152`

```json
{
    "JsonPacketID": 152,
    "MsgSeqNo": 45,
    "GwySerNo": "GWY00012",
    "NodeSerNo": "NODE00001",
    "ElementAddr": 5,
    "NodeType": 2,
    "SlaveAddr": 1,
    "Label": "DB1-INCOMER",
    "Readings": [
        {"Idx": 0, "Raw": 1150631936},
        {"Idx": 1, "Raw": 1095037018},
        {"Idx": 2, "Raw": 1064374681},
        {"Idx": 3, "Raw": 1050669850},
        {"Idx": 4, "Raw": 1056964608},
        {"Idx": 5, "Raw": 1060320051},
        {"Idx": 6, "Raw": 1098907648},
        {"Idx": 7, "Raw": 1098383360},
        {"Idx": 8, "Raw": 1097859072},
        {"Idx": 9, "Raw": 14400}
    ],
    "ErrorCode": 0,
    "ErrorMsg": "SUCCESS"
}
```

**Readings array fields:**

| Field | Description |
|---|---|
| `Idx` | `ParamIdx` from the register map configured at commissioning |
| `Raw` | Raw uint32 value read from meter. Decode using `DataType` from register map. |

**Cloud decoding example** using the Elmeasure register map from Section 6.3:

```
Idx 0 → Import Energy:     Raw=1150631936 → IEEE 754 float = 1523450.0 Wh → ÷1000 = 1523.45 kWh
Idx 1 → Active Power Total: Raw=1095037018 → IEEE 754 float = 12.34 kW
Idx 5 → True PF Average:    Raw=1060320051 → IEEE 754 float = 0.921
Idx 9 → Running Hours:      Raw=14400 → LONG_INVERSE swap → 14400 sec ÷3600 = 4.0 hours
```

### 7.3 Single-Phase Meter Reading

**Topic:** `{GwySerNo}/message`
**JsonPacketID:** `153`

Identical structure to 3-phase reading. `JsonPacketID = 153` indicates single-phase. The cloud maps `Idx` values to single-phase parameters (Voltage, Current, Power, PF, Frequency, Energy) using the register map it configured.

### 7.4 Reading Error — Modbus Timeout

When the node cannot communicate with a meter (device off, cable fault, wrong baud rate), it publishes an error packet with empty `Readings`:

```json
{
    "JsonPacketID": 152,
    "MsgSeqNo": 46,
    "GwySerNo": "GWY00012",
    "NodeSerNo": "NODE00001",
    "ElementAddr": 5,
    "NodeType": 2,
    "SlaveAddr": 1,
    "Label": "DB1-INCOMER",
    "Readings": [],
    "ErrorCode": 28,
    "ErrorMsg": "MODBUS_TIMEOUT"
}
```

**Cloud must handle this gracefully** — display last known values with a "communication lost" indicator and timestamp of last successful reading.

### 7.5 Reading Error — Modbus CRC Error

```json
{
    "JsonPacketID": 152,
    "MsgSeqNo": 47,
    "GwySerNo": "GWY00012",
    "NodeSerNo": "NODE00001",
    "ElementAddr": 5,
    "NodeType": 2,
    "SlaveAddr": 1,
    "Label": "DB1-INCOMER",
    "Readings": [],
    "ErrorCode": 29,
    "ErrorMsg": "MODBUS_CRC_ERROR"
}
```

---

## 8. Heartbeat — RS485 Node

The RS485 node sends a heartbeat every publish period (same interval as meter readings).

**Topic:** `{GwySerNo}/message`
**JsonPacketID:** `106`

```json
{
    "JsonPacketID": 106,
    "MsgSeqNo": 12,
    "GwySerNo": "GWY00012",
    "NodeSerNo": "NODE00001",
    "ElementAddr": 5,
    "NodeType": 2,
    "MajVersion": 1,
    "MinVersion": 0,
    "PatchVersion": 0,
    "UptimeHrs": 72.5,
    "PublishPeriodSec": 60,
    "Provisioned": 1,
    "ErrorCode": 0,
    "ErrorMsg": "SUCCESS"
}
```

### Configure Heartbeat Period

**Topic:** `{GwySerNo}/command`
**JsonPacketID:** `107`

```json
{
    "JsonPacketID": 107,
    "MsgSeqNo": 3,
    "GwySerNo": "GWY00012",
    "NodeSerNo": "NODE00001",
    "ElementAddr": 5,
    "NodeType": 2,
    "PublishPeriodSec": 60
}
```

> **Note:** `PublishPeriodSec` controls both the heartbeat interval AND the meter reading interval — they share the same period. Minimum value: `10` seconds. Default: `60` seconds.

**ACK:**
```json
{
    "JsonPacketID": 107,
    "MsgSeqNo": 3,
    "GwySerNo": "GWY00012",
    "NodeSerNo": "NODE00001",
    "ElementAddr": 5,
    "NodeType": 2,
    "PublishPeriodSec": 60,
    "ErrorCode": 0,
    "ErrorMsg": "SUCCESS"
}
```

---

## 9. Error Codes Reference

| ErrorCode | Name | Description |
|---|---|---|
| `-1` | `FAILURE` | Generic failure |
| `0` | `SUCCESS` | Operation successful |
| `1` | `JSON_PACKET_INVALID` | Malformed JSON payload |
| `2` | `MISSING_PACKET_ID` | `JsonPacketID` field missing |
| `3` | `PACKET_ID_EXCEEDING_RANGE` | Unknown or out-of-range packet ID |
| `28` | `MODBUS_TIMEOUT` | No response from RS485 device within timeout |
| `29` | `MODBUS_CRC_ERROR` | CRC mismatch in Modbus response |
| `30` | `MODBUS_EXCEPTION_RESPONSE` | Meter returned Modbus exception code |
| `31` | `MODBUS_INVALID_RESPONSE_LENGTH` | Response length does not match expected |
| `32` | `MODBUS_SLAVE_ADDR_INVALID` | Slave address out of range (must be 1–247) |
| `33` | `MODBUS_MODEL_UNKNOWN` | Meter model not supported |
| `34` | `MODBUS_DEVICE_NOT_FOUND` | Slave address not in registry |
| `35` | `MODBUS_REGISTRY_FULL` | Maximum 16 devices already configured |
| `36` | `MODBUS_DEVICE_ALREADY_EXISTS` | Slave address already exists (use update) |

---

## 10. Multi-Meter Example Flow

### Complete commissioning and operation flow for a node with 2 meters:

```
1. RS485 Node powers on → advertises on BLE Mesh

2. Gateway provisions node → publishes to GWY00012/message:
   { JsonPacketID:100, NodeSerNo:"NODE00001", ElementAddr:5, NodeType:2, ErrorCode:0 }
   Cloud stores: ElementAddr 5 = RS485 Node

3. Cloud configures Meter 1 (Elmeasure LG, slave addr 1):
   → publishes to GWY00012/command: { JsonPacketID:151, SlaveAddr:1, Remove:0, ... }
   ← node ACKs: { JsonPacketID:151, Action:"ADD", TotalDevices:1, ErrorCode:0 }

4. Cloud configures Meter 2 (Schneider EM, slave addr 2):
   → publishes to GWY00012/command: { JsonPacketID:151, SlaveAddr:2, Remove:0, ... }
   ← node ACKs: { JsonPacketID:151, Action:"ADD", TotalDevices:2, ErrorCode:0 }

5. Every 60 seconds thereafter, node publishes automatically:
   → { JsonPacketID:152, SlaveAddr:1, Label:"DB1-INCOMER", Readings:[...] }
   → { JsonPacketID:152, SlaveAddr:2, Label:"DB2-RISER",   Readings:[...] }
   → { JsonPacketID:106, NodeSerNo:"NODE00001", UptimeHrs:0.02, ... }

6. Node restarts (power cycle) — polls resume automatically from NVS.
   No re-configuration needed.
```

---

## 11. Important Notes for Implementation

1. **NodeType must be stored at provisioning time** — every `ElementAddr → NodeType` mapping must be persisted in the cloud database. Do not rely on reading it from every packet.

2. **Register map must be stored by cloud** — the cloud must persist the `ParamIdx → parameter name` mapping for each meter. The node does not store or echo parameter names — only `ParamIdx`.

3. **Raw value decoding is cloud's responsibility** — the node sends raw IEEE 754 bits as uint32. The cloud must reinterpret bits using `DataType` from the stored register map. Do NOT treat `Raw` as a plain integer for `DataType = FLOAT`.

4. **Meter readings are not ACKed by cloud** — `JsonPacketID 152/153` are fire-and-forget pushes. Do not send a response.

5. **Slave address 0 is invalid** — Modbus broadcast address, rejected by node with `MODBUS_SLAVE_ADDR_INVALID`.

6. **Readings arrive per device, not batched** — if 16 meters are configured, the cloud receives 16 separate `JsonPacketID:152` packets per cycle (plus 1 heartbeat). Design the MQTT subscriber to handle burst arrival.

7. **Empty Readings array means communication failure** — always check `ErrorCode` before processing `Readings`. If `ErrorCode != 0`, readings are unavailable for that cycle.

8. **Node-RED filter** — filter on both `JsonPacketID` AND `NodeType` to separate RS485 node traffic from IR AC node traffic on the same `GWY00012/message` topic.
