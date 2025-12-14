idf_component_register(
    SRCS "main_AIoT.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash Configuracion_AIoT WiFi_AIoT IO_AIoT EEZ_AIoT lvgl
)