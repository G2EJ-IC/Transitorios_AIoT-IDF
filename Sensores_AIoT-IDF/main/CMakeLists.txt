# File: main/CMakeLists.txt
# Description: Component registration with dependencies.
#              REQUIRES gives main access to the headers it includes.
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
)
