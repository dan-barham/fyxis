cmake_minimum_required(VERSION 3.16)
# Components normally fetched from the Espressif registry are vendored in deps/.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/deps")
# Board settings, then the Big Six additions kept at the project root.
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/../sdkconfig.defaults.append")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_options("-Wno-attributes")
project(pyxis_bigsix)
