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