set(CUSTOM_SRCS
    main.c
    src/general.c
    src/led.c
    src/flash.c
    src/gpio_subsystem.c
    src/ble_mesh_node.c
    src/ble_packet_handler.c
    src/json_maker.c
    src/modbus/modbus_master.c
    src/modbus/modbus_poll_task.c
    src/modbus/drivers/drv_suman_ac.c
    src/heartbeat.c
)

set(includeDirs
    inc
    inc/modbus
    inc/modbus/drivers
)

# bluetooth_init() implemented in ble_mesh_node.c
# BLE Mesh headers resolved via EXTRA_COMPONENT_DIRS in top-level CMakeLists.txt
idf_component_register(
    SRCS          ${CUSTOM_SRCS}
    INCLUDE_DIRS  ${includeDirs}
    REQUIRES      driver esp_timer nvs_flash esp_common  esp_modbus bt
)
