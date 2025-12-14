idf_component_register(
    SRC_DIRS "." "src" "ui"
    INCLUDE_DIRS "." "src" "ui"
    REQUIRES lvgl__lvgl esp_timer driver nvs_flash WiFi_AIoT IO_AIoT
)

file(GLOB_RECURSE SOURCES_EEZ "*.c")
target_sources(${COMPONENT_LIB} PRIVATE ${SOURCES_EEZ})