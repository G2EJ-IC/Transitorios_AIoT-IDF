# File: components/SD_AIoT/CMakeLists.txt
# Description: Component registration with dependencies.
#              C++ class implementation with a C API (extern "C").
# Standards: ESP-IDF v5.5.1

idf_component_register(
    SRCS
        "src/SD_AIoT.cpp"
    INCLUDE_DIRS
        "include"
    PRIV_REQUIRES
        fatfs
        sdmmc
        esp_driver_sdspi
        esp_driver_spi
        freertos
        log
)
