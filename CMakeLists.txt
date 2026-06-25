# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following boilerplate has to be in your project's CMakeLists in this
# exact order for cmake to work correctly.
cmake_minimum_required(VERSION 3.5)

# "Trim" the build. Only include the minimal set of components: main and what it depends on.
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(c3_lcdkit_dev)
