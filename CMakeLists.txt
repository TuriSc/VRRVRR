cmake_minimum_required(VERSION 3.13)
 
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
 
project(VRRVRR C CXX ASM)
 
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
 
pico_sdk_init()

add_subdirectory(lib/RP2040-Keypad-Matrix keypad_matrix)
add_subdirectory(lib/RP2040-Battery-Check battery_check)

add_executable(${PROJECT_NAME}
        main.c
        )

target_include_directories(${PROJECT_NAME}
        PRIVATE
        #lib/pico-debounce/
)

target_link_libraries(${PROJECT_NAME}
        pico_stdlib
        keypad_matrix
        battery_check
        hardware_pwm
        hardware_flash
        hardware_sync
        hardware_adc
        hardware_xosc
        )

pico_add_extra_outputs(${PROJECT_NAME})

pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

