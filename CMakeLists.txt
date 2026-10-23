# Las Vegas Sphere eyeballs (Waveshare ESP32-S3-Touch-LCD-2.8C).
# This folder builds the MASTER eye (viewer's left). eye_slave/ builds the slave (right) eye from the same code.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

idf_build_set_property(COMPILE_DEFINITIONS "APP_EYE_IS_LEFT=1" APPEND)   # Viewer's left eye
idf_build_set_property(COMPILE_DEFINITIONS "APP_SYNC_ROLE=1" APPEND)     # ESP-NOW master: sends gaze and blinks

project(sphere_eyes)
