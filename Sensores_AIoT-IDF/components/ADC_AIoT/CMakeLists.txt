# File: components/ADC_AIoT/CMakeLists.txt
# Description: Component registration with dependencies.
#              C++ class implementation with a C API (extern "C").
# Standards: ESP-IDF v5.5.1

idf_component_register(
    SRCS
        "src/ADC_AIoT.cpp"
    INCLUDE_DIRS
        "include"
    PRIV_REQUIRES
        esp_adc
        freertos
        heap
        log
)
