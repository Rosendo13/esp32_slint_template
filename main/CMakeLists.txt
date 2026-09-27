cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRCS "main.cpp" "display.cpp" "ui_hal.cpp"
    INCLUDE_DIRS "."
    REQUIRES slint qpcpp qp_app qp_fw_glue
             esp_lcd esp_lcd_touch esp_lcd_touch_gt911 driver esp_timer
    WHOLE_ARCHIVE
)

# Compiles ui/printerdemo.slint into printerdemo.h and adds the generated
# include dir to ${COMPONENT_LIB}. Imported .slint files, fonts and images are
# picked up as dependencies automatically, so touching any of them re-runs the
# Slint compiler.
slint_target_sources(${COMPONENT_LIB} ui/printerdemo.slint)
