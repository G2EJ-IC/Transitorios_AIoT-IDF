# File: components/System_Control_AIoT/CMakeLists.txt
# Description: Component registration with dependencies.
# Standards: ESP-IDF v5.5.1

idf_component_register(
    SRC_DIRS "src"
    INCLUDE_DIRS "include"
    REQUIRES 
        driver 
        esp_timer
)