# File: components/EEZ_AIoT/CMakeLists.txt
# Description: Component registration with dependencies.
# Standards: ESP-IDF v5.5.1

idf_component_register(
    SRC_DIRS "." "src" "ui"
    INCLUDE_DIRS "." "src" "ui"
    REQUIRES 
        lvgl__lvgl 
        esp_timer 
        driver 
        nvs_flash 
        WiFi_AIoT 
        IO_AIoT
)

file(GLOB_RECURSE SOURCES_EEZ "*.c")
target_sources(${COMPONENT_LIB} PRIVATE ${SOURCES_EEZ})