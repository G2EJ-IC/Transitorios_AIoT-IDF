# File: components/ESPNOW_AIoT/CMakeLists.txt
# Description: Component registration with dependencies.
#              C++ class implementation with a C API (extern "C").
# Standards: ESP-IDF v5.5.1

idf_component_register(
    SRCS
        "src/ESPNOW_AIoT.cpp"
    INCLUDE_DIRS
        "include"
    PRIV_REQUIRES
        esp_wifi
        esp_netif
        esp_event
        freertos
        log
)
