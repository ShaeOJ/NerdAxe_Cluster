# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
#cmake_minimum_required(VERSION 3.5)
cmake_minimum_required(VERSION 3.8)
set(CMAKE_CXX_STANDARD 17)

add_definitions(-DLV_LVGL_H_INCLUDE_SIMPLE)

# Check if the environment variable BOARD is defined
if(DEFINED ENV{BOARD})
    add_compile_definitions($ENV{BOARD})
else()
    message(FATAL_ERROR "Environment variable BOARD is not set")
endif()

message(STATUS "Selected board: $ENV{BOARD}")

set(ENV{SOURCE_DATE_EPOCH} 0)


add_compile_definitions(LV_MEM_CUSTOM=1)
add_compile_definitions(LV_MEM_CUSTOM_INCLUDE=<lvgl_psram.h>)
add_compile_definitions(LV_MEM_CUSTOM_ALLOC=lv_psram_alloc)
add_compile_definitions(LV_MEM_CUSTOM_FREE=lv_psram_free)
add_compile_definitions(LV_MEM_CUSTOM_REALLOC=lv_psram_realloc)

#add_compile_definitions(CONFIG_FREERTOS_USE_TRACE_FACILITY=1)
#add_compile_definitions(CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS=1)

# adds -I./main; needed for components to have
# access to main include files
include_directories("./main")

# this are only header files
include_directories("./components/arduinojson")

# (Not part of the boilerplate)
# This example uses an extra component for common functions such as Wi-Fi and Ethernet connection.
# set(EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/examples/common_components/protocol_examples_common)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)


project(esp-miner)

# Map BOARD env variable to device model name for OTA filename
# (must match frontend normalization in settings.component.ts)
if("$ENV{BOARD}" STREQUAL "NERDAXE")
    set(DEVICE_MODEL "NerdAxe")
elseif("$ENV{BOARD}" STREQUAL "NERDAXEGAMMA")
    set(DEVICE_MODEL "NerdAxeGamma")
elseif("$ENV{BOARD}" STREQUAL "NERDEKO")
    set(DEVICE_MODEL "NerdEKO")
elseif("$ENV{BOARD}" STREQUAL "NERDHAXEGAMMA")
    set(DEVICE_MODEL "NerdHaxe-Gamma")
elseif("$ENV{BOARD}" STREQUAL "NERDOCTAXEGAMMA")
    set(DEVICE_MODEL "NerdOCTAXE-Gamma")
elseif("$ENV{BOARD}" STREQUAL "NERDOCTAXEPLUS")
    set(DEVICE_MODEL "NerdOCTAXE+")
elseif("$ENV{BOARD}" STREQUAL "NERDQAXEPLUS")
    set(DEVICE_MODEL "NerdQAxe+")
elseif("$ENV{BOARD}" STREQUAL "NERDQAXEPLUS2")
    set(DEVICE_MODEL "NerdQAxe++")
elseif("$ENV{BOARD}" STREQUAL "NERDQX")
    set(DEVICE_MODEL "NerdQX")
else()
    set(DEVICE_MODEL "$ENV{BOARD}")
endif()

set(OTA_FILENAME "esp-miner-${DEVICE_MODEL}.bin")

add_custom_target(ota_file ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin"
        "${CMAKE_BINARY_DIR}/${OTA_FILENAME}"
    COMMENT "Creating OTA file: ${OTA_FILENAME}"
)
add_dependencies(ota_file gen_project_binary)

