# SPDX-License-Identifier: Apache-2.0
# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(hobbsless_v001)

# Also emit the image as build/<version>.bin - the version comes from
# version.txt, so the file uploaded to the OTA server is named for the version
# the firmware inside it reports. The project-named .bin is still produced and
# is what idf.py flash uses.
idf_build_get_property(_project_ver PROJECT_VER)
add_custom_command(TARGET app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin"
            "${CMAKE_BINARY_DIR}/${_project_ver}.bin"
    COMMENT "Emitting OTA image as ${_project_ver}.bin")
