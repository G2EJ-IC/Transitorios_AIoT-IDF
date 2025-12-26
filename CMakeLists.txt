# File: CMakeLists.txt
# Description: Component registration with dependencies.
# Standards: ESP-IDF v5.5.1

cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS components)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(CrowPanel_HMI)