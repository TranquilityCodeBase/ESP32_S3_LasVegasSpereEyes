// app_config.h - Settings for this build. Board pins and the backlight level live in components/board/include/board_config.h.
#pragma once

#include "board_config.h"

// ---------- Which eye this board is ----------
// Each board shows one eye. Mount the boards side by side as seen by the viewer.
// The project CMakeLists.txt sets these: the root folder builds the master (left) eye,
// eye_slave/ builds the slave (right) eye. The values here are the fallback.
#ifndef APP_EYE_IS_LEFT
#define APP_EYE_IS_LEFT         1           // 1 = viewer's left eye, 0 = viewer's right eye
#endif

// ---------- Color trim (per board) ----------
// Red, green, blue gains to make two screens match (1.0 = unchanged). Set per folder in CMakeLists.txt.
#ifndef APP_COLOR_TRIM_R
#define APP_COLOR_TRIM_R        1.0f
#endif
#ifndef APP_COLOR_TRIM_G
#define APP_COLOR_TRIM_G        1.0f
#endif
#ifndef APP_COLOR_TRIM_B
#define APP_COLOR_TRIM_B        1.0f
#endif
#ifndef APP_WHITE_R                         // White balance for grays and white (1.0 = unchanged)
#define APP_WHITE_R             1.0f
#endif
#ifndef APP_WHITE_G
#define APP_WHITE_G             1.0f
#endif
#ifndef APP_WHITE_B
#define APP_WHITE_B             1.0f
#endif
#ifndef APP_BACKLIGHT_PERCENT
#define APP_BACKLIGHT_PERCENT   BOARD_BACKLIGHT_DEFAULT_PERCENT   // Per-board backlight (dimmer panels can go higher)
#endif
#ifndef APP_GRAY_GAMMA
#define APP_GRAY_GAMMA          1.0f        // > 1 lightens mid grays (eye white shading)
#endif

// ---------- Rendering ----------
#define APP_TARGET_FPS          30          // Animation frame rate (panel refreshes at about 64 Hz)
#define APP_RENDER_CORE         1           // CPU core for the render task (Wi-Fi runs on core 0)
#define APP_RENDER_PRIORITY     5
#define APP_DEBUG_FRAME_DUMP    0           // 1 = print the on-screen frame over the console every 3 s (debug only)

// ---------- Design rotation ----------
// The master steps through the designs listed in stylePlaylist (app_main.c) and tells the slave which one to show.
#define APP_STYLE_SECONDS       15          // Time on each design (0 = stay on the first one)

// ---------- LD2450 radar (master board only) ----------
// Wire radar RX -> GPIO19 (D-) and radar TX -> GPIO20 (D+) on the 12-pin header, 5 V from USB_5V on the same header.
// GPIO43/44 are not used because they reach the header only with the USB-C port unplugged.
// GPIO19/20 are the chip's native USB pins: sdkconfig.defaults turns native USB off so the UART gets them.
#define APP_RADAR_ENABLED       (APP_SYNC_ROLE != 2)   // The slave has no radar, it gets the gaze over ESP-NOW
#define APP_RADAR_GAZE          0           // 1 = eyes follow the radar, 0 = radar is only logged (bring-up)
#define APP_RADAR_LOG_MS        1000        // Print radar counters and targets this often (0 = never)
#define APP_RADAR_UART          UART_NUM_1
#define APP_RADAR_RX_PIN        BOARD_PIN_HEADER_IO20
#define APP_RADAR_TX_PIN        BOARD_PIN_HEADER_IO19
#define APP_RADAR_MIRROR_X      false       // Flip if the eyes look the wrong way
#define APP_RADAR_MAX_ANGLE     60.0f       // Bearing (degrees) that maps to the edge of the gaze range
#define APP_RADAR_MAX_RANGE_M   5.0f        // Ignore people farther than this

// ---------- ESP-NOW sync between the two boards ----------
// 0 = off, 1 = master (sends gaze and blinks), 2 = slave (copies the master). Set by the project CMakeLists.txt.
#ifndef APP_SYNC_ROLE
#define APP_SYNC_ROLE           1
#endif
#define APP_SYNC_CHANNEL        1           // Same Wi-Fi channel on both boards
#define APP_SYNC_GROUP_ID       0x51        // Same on both boards; change it if two pairs share a room
