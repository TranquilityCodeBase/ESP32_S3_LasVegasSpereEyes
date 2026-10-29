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
#define APP_RADAR_GAZE          1           // 1 = eyes follow the radar, 0 = radar is only logged (bring-up)
#define APP_RADAR_LOG_MS        1000        // Print radar counters and where the eyes aim this often (0 = never)
#define APP_RADAR_UART          UART_NUM_1
#define APP_RADAR_RX_PIN        BOARD_PIN_HEADER_IO20
#define APP_RADAR_TX_PIN        BOARD_PIN_HEADER_IO19
// Mount the module upright (long edge vertical, large copper pour at the top), antenna side facing the street.
// The settings below were tuned 2026-10-25 with the module lying flat, which cut its view to one side.
// Re-check them with the module upright: tools/radar_capture.py records walks and prints a summary.
#define APP_RADAR_MIRROR_X      true        // Radar +x is the viewer's left (checked flat 2026-10-25: re-check upright)
#define APP_RADAR_MAX_ANGLE     30.0f       // Bearing (degrees) that maps to the edge of the gaze range (smaller = eyes move more)
#define APP_RADAR_MAX_RANGE_M   6.0f        // Ignore people farther than this (the radar's own limit is 6 m)
#define APP_RADAR_MIN_RANGE_M   0.0f        // Ignore anything nearer than this (was 0.5 for a clip in front of the flat module)
#define APP_RADAR_HELD_MS       1500        // Skip a position frozen this long, the radar repeating someone it lost (0 = off)
#define APP_RADAR_PARKED_MS     0           // Skip targets that moved under 30 cm in this long, clutter (0 = off; was 8000)
#define APP_RADAR_LOCK_LOST_MS  1000        // Keep looking where the person was last seen this long
#define APP_RADAR_LEAD_MS       0           // Aim this far ahead of the person (0 = off; was 150, re-tune after the timing test)
#define APP_RADAR_TRACE         1           // 1 = print every radar frame for tools/radar_capture.py (test sessions)
// Settings kept in the radar itself (-1 = leave as they are). The radar has no range or sensitivity setting;
// these two are what it offers. Zone corners are radar coordinates in mm (x sideways, y straight out).
#define APP_RADAR_TARGET_MODE   -1          // 1 = single target (steadier on one person), 2 = multi (up to 3)
#define APP_RADAR_ZONE_TYPE     1           // 0 = off, 1 = only people inside the zone, 2 = ignore people inside it
#define APP_RADAR_ZONE_X1_MM    -3000       // Sidewalk zone: 3 m to each side ...
#define APP_RADAR_ZONE_Y1_MM    0           // ... from the radar ...
#define APP_RADAR_ZONE_X2_MM    3000
#define APP_RADAR_ZONE_Y2_MM    4000        // ... to 4 m out

// ---------- ESP-NOW sync between the two boards ----------
// 0 = off, 1 = master (sends gaze and blinks), 2 = slave (copies the master). Set by the project CMakeLists.txt.
#ifndef APP_SYNC_ROLE
#define APP_SYNC_ROLE           1
#endif
#define APP_SYNC_CHANNEL        1           // Same Wi-Fi channel on both boards
#define APP_SYNC_GROUP_ID       0x51        // Same on both boards; change it if two pairs share a room
#define APP_BLINK_SYNC_LEAD_MS  20          // Master shows each blink this long after sending it, so the slave
                                            // (which starts on its next frame, 0 to 33 ms later) blinks with it.
                                            // Tune from the "Blink start" log times on both boards.
