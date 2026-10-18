// app_config.h - Settings for this build. Board pins and the backlight level live in components/board/include/board_config.h.
#pragma once

#include "board_config.h"

// ---------- Which eye this board is ----------
// Each board shows one eye. Mount the boards side by side as seen by the viewer.
// 1 = viewer's left eye, 0 = viewer's right eye. Matters for winks and the ESP-NOW pair.
#define APP_EYE_IS_LEFT         1

// ---------- Rendering ----------
#define APP_TARGET_FPS          30          // Animation frame rate (panel refreshes at about 64 Hz)
#define APP_RENDER_CORE         1           // CPU core for the render task (Wi-Fi runs on core 0)
#define APP_RENDER_PRIORITY     5
#define APP_DEBUG_FRAME_DUMP    0           // 1 = print the on-screen frame over the console every 3 s (debug only)

// ---------- LD2450 radar (off until the radar is wired) ----------
// Wire radar TX -> GPIO19 and radar RX -> GPIO20 on the 12-pin header, 5 V from USB_5V on the same header.
// GPIO43/44 are not used because they reach the header only with the USB-C port unplugged.
#define APP_RADAR_ENABLED       0
#define APP_RADAR_UART          UART_NUM_1
#define APP_RADAR_RX_PIN        BOARD_PIN_HEADER_IO19
#define APP_RADAR_TX_PIN        BOARD_PIN_HEADER_IO20
#define APP_RADAR_MIRROR_X      false       // Flip if the eyes look the wrong way
#define APP_RADAR_MAX_ANGLE     60.0f       // Bearing (degrees) that maps to the edge of the gaze range
#define APP_RADAR_MAX_RANGE_M   5.0f        // Ignore people farther than this

// ---------- ESP-NOW sync between the two boards (off by default) ----------
// 0 = off, 1 = leader (sends gaze and blinks), 2 = follower (copies the leader). Flash one board of each.
#define APP_SYNC_ROLE           0
#define APP_SYNC_CHANNEL        1           // Same Wi-Fi channel on both boards
#define APP_SYNC_GROUP_ID       0x51        // Same on both boards; change it if two pairs share a room
