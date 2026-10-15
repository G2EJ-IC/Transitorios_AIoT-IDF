# File: components/MPU6050_AIoT/CMakeLists.txt
# Description: Component registration with dependencies.
#              C++ class implementation with a C API (extern "C").
#              I2C_AIoT is a public dependency: its handle type is used in
#              the public header.
# Standards: ESP-IDF v5.5.1

idf_component_register(
    SRCS
        "src/MPU6050_AIoT.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        I2C_AIoT
    PRIV_REQUIRES
        freertos
        log
)
