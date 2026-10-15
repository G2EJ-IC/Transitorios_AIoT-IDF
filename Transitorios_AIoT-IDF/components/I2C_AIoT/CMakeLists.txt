# File: components/I2C_AIoT/CMakeLists.txt
# Description: Component registration with dependencies.
#              C++ class implementation with a C API (extern "C").
# Standards: ESP-IDF v5.5.1

idf_component_register(
    SRCS
        "src/I2C_AIoT.cpp"
    INCLUDE_DIRS
        "include"
    PRIV_REQUIRES
        esp_driver_i2c
        esp_driver_gpio
        log
)
