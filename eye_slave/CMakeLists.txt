# Las Vegas Sphere eyeballs: SLAVE eye (viewer's right).
# Same code as the master in the folder above; only the eye side and ESP-NOW role differ.
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../components" "${CMAKE_CURRENT_LIST_DIR}/../main")
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/../sdkconfig.defaults")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

idf_build_set_property(COMPILE_DEFINITIONS "APP_EYE_IS_LEFT=0" APPEND)   # Viewer's right eye
idf_build_set_property(COMPILE_DEFINITIONS "APP_SYNC_ROLE=2" APPEND)     # ESP-NOW slave: copies the master
# Color trim so this screen matches the master's (screens vary). 1.0 = unchanged.
idf_build_set_property(COMPILE_DEFINITIONS "APP_COLOR_TRIM_R=0.95f" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "APP_COLOR_TRIM_G=1.20f" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "APP_COLOR_TRIM_B=1.0f" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "APP_WHITE_B=0.88f" APPEND)     # This panel's white is bluer
idf_build_set_property(COMPILE_DEFINITIONS "APP_GRAY_GAMMA=2.2f" APPEND)  # This panel shows mid grays darker

project(sphere_eyes_slave)
