# File: components/UARTn_AIoT/CMakeLists.txt
# Description: Component registration with dependencies.
# Standards: ESP-IDF v5.5.1

idf_component_register(
    SRCS 
        src/UARTn_AIoT.cpp
    INCLUDE_DIRS
        include
    PRIV_REQUIRES 
        driver
        log
        esp_common
        freertos
)