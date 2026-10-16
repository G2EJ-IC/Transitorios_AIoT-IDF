# File: components/Enlace_AIoT/CMakeLists.txt
# Description: Component registration with dependencies.
#              UART "hello" link test between the two boards (C).
#              Identical in the sensor node and in the CrowPanel project.
# Standards: ESP-IDF v5.5.1

idf_component_register(
    SRCS
        "src/Enlace_AIoT.c"
    INCLUDE_DIRS
        "include"
    PRIV_REQUIRES
        UARTn_AIoT
        freertos
        log
)
