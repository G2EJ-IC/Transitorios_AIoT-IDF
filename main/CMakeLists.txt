# File: main/CMakeLists.txt
# Description: Component registration with dependencies.
# Standards: ESP-IDF v5.5.1

idf_component_register(
    SRCS "main_AIoT.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_system
        lvgl__lvgl
        Configuracion_AIoT
        IO_AIoT
        WiFi_AIoT
        UARTn_AIoT
        EEZ_AIoT
)