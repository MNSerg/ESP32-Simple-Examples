cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(u8g2_status)
# В u8g2_esp32_hal пропущен break в switch. На GCC 15 это ошибка сборки.
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=implicit-fallthrough" APPEND)
