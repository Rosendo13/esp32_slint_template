cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    ${CMAKE_SOURCE_DIR}/extern/esp-bsp/components/lcd_touch/esp_lcd_touch
    ${CMAKE_SOURCE_DIR}/extern/esp-bsp/components/lcd_touch/esp_lcd_touch_gt911
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# CXX is required: QP/C++, the application layer and Slint are all C++.
project(esp32_slint_template LANGUAGES C CXX)
