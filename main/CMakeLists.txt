# File: main/CMakeLists.txt
# Description: Component registration with dependencies.
#              REQUIRES gives main access to the headers of each library.
# Standards: ESP-IDF v5.5.1

idf_component_register(
    SRCS "main_AIoT.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_system
        esp_timer
        lvgl__lvgl
        Configuracion_AIoT
        IO_AIoT
        WiFi_AIoT
        EEZ_AIoT
        UARTn_AIoT
        I2C_AIoT
        SPI_AIoT
        ESPNOW_AIoT
        ADC_AIoT
        PCNT_AIoT
        SD_AIoT
        MQTT_AIoT
)
