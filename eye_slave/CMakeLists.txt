# Las Vegas Sphere eyeballs: SLAVE eye (viewer's right).
# Same code as the master in the folder above; only the eye side and ESP-NOW role differ.
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components" "${CMAKE_CURRENT_LIST_DIR}/../main")
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/../sdkconfig.defaults")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

idf_build_set_property(COMPILE_DEFINITIONS "APP_EYE_IS_LEFT=0" APPEND)   # Viewer's right eye
idf_build_set_property(COMPILE_DEFINITIONS "APP_SYNC_ROLE=2" APPEND)     # ESP-NOW slave: copies the master
# ---- Panel correction for THIS slave screen only ----
# This board's screen (serial 5CF7115307, COM7) is dimmer, greener and bluer than the master's.
# These values are tuned to that one panel. If the screen or board is replaced, delete this block
# (all values back to 1.0 / 50 %) and re-tune against the master.
idf_build_set_property(COMPILE_DEFINITIONS "APP_COLOR_TRIM_R=0.95f" APPEND)  # Ball color: less red...
idf_build_set_property(COMPILE_DEFINITIONS "APP_COLOR_TRIM_G=1.20f" APPEND)  # ...more green (orange -> yellow)
idf_build_set_property(COMPILE_DEFINITIONS "APP_COLOR_TRIM_B=1.0f" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "APP_WHITE_G=0.92f" APPEND)       # Eye white: less green...
idf_build_set_property(COMPILE_DEFINITIONS "APP_WHITE_B=0.90f" APPEND)       # ...and less blue
idf_build_set_property(COMPILE_DEFINITIONS "APP_GRAY_GAMMA=2.2f" APPEND)     # Lighter mid grays (lid shadow)
idf_build_set_property(COMPILE_DEFINITIONS "APP_BACKLIGHT_PERCENT=60" APPEND) # Brighter backlight (master: 50)

project(sphere_eyes_slave)
