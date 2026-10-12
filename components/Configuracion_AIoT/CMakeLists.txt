# File: components/Configuracion_AIoT/CMakeLists.txt
# Description: Component registration with dependencies.
# Standards: ESP-IDF v5.5.1

idf_component_register(
    SRCS
        "src/Configuracion_AIoT.c"
        "src/xpt2046_lvgl9.c"
    INCLUDE_DIRS "include"
    REQUIRES
        driver
        esp_timer
        esp_lcd
        lvgl__lvgl
)
