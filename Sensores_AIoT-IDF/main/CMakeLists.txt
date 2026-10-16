# File: main/CMakeLists.txt
# Description: Component registration with dependencies.
#              REQUIRES gives main access to the headers of each library
#              (same list style as the CrowPanel project).
# Standards: ESP-IDF v5.5.1

idf_component_register(
    SRCS "main_AIoT.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_system
        Configuracion_AIoT
        Frame_AIoT
        Adquisicion_AIoT
        I2C_AIoT
        SPI_AIoT
        UARTn_AIoT
        ADC_AIoT
        ADS1115_AIoT
        MPU6050_AIoT
        PCNT_AIoT
        ESPNOW_AIoT
        SD_AIoT
        MQTT_AIoT
        Enlace_AIoT
)
