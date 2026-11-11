# Корневой файл сборки ESP-IDF. Эти строки обязательны и должны идти
# именно в таком порядке: сначала cmake_minimum_required, потом подключение
# проектного скрипта IDF, и только после этого имя проекта.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(gpio_blink)
