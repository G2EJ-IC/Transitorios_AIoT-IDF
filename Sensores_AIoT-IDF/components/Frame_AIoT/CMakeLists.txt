# File: components/Frame_AIoT/CMakeLists.txt
# Description: Header-only component: ESP-NOW frame shared by the sensor node
#              and the CrowPanel. Copy this folder unchanged to both projects.
# Standards: ESP-IDF v5.5.1

idf_component_register(
    INCLUDE_DIRS
        "include"
)
