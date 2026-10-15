# File: CMakeLists.txt
# Description: ESP-IDF project of the Transitorios AIoT sensor node.
# Standards: ESP-IDF v5.5.1

cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS components)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Sensores_AIoT)
