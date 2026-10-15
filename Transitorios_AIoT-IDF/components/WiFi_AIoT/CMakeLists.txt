# File: components/WiFi_AIoT/CMakeLists.txt
# Description: Component registration with dependencies.
# Standards: ESP-IDF v5.5.1

idf_component_register(
    SRC_DIRS "src"
    INCLUDE_DIRS "include"
    REQUIRES 
        esp_wifi 
        nvs_flash 
        esp_event 
        lwip 
        esp_timer 
        esp_netif 
        esp_pm
)