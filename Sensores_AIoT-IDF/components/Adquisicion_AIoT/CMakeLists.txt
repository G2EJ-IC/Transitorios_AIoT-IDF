# File: components/Adquisicion_AIoT/CMakeLists.txt
# Description: Component registration with dependencies.
#              Acquisition engine of the sensor node (C).
# Standards: ESP-IDF v5.5.1

idf_component_register(
    SRCS
        "src/Adquisicion_AIoT.c"
    INCLUDE_DIRS
        "include"
    PRIV_REQUIRES
        Configuracion_AIoT
        Frame_AIoT
        I2C_AIoT
        ADC_AIoT
        ADS1115_AIoT
        MPU6050_AIoT
        PCNT_AIoT
        ESPNOW_AIoT
        esp_timer
        esp_wifi
        freertos
        log
)
