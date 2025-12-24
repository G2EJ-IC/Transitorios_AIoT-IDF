file(GLOB_RECURSE SOURCES "src/*.c")

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "include"
    REQUIRES 
        driver 
        esp_timer 
        esp_lcd 
        lvgl__lvgl
)