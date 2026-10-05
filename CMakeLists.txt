cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    $ENV{IDF_PATH}/examples/bluetooth/esp_ble_mesh/common_components/example_init
    $ENV{IDF_PATH}/examples/bluetooth/esp_ble_mesh/common_components/example_nvs
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

option(CMAKE_BUILD_TYPE Release)

project(ble_mesh_rs485)
