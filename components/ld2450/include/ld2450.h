// ld2450.h - HLK-LD2450 24 GHz radar driver (UART, 256000 baud). Tracks up to 3 people.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/uart.h"
#include "gaze_source.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LD2450_MAX_TARGETS 3

// One tracked target, radar coordinates. X is lateral, Y is straight out from the radar face.
typedef struct {
    bool valid;         // false = empty slot
    int16_t xMm;        // Lateral position in mm (sign depends on mounting, see ld2450Config_t.mirrorX)
    int16_t yMm;        // Distance out from the radar in mm
    int16_t speedCmS;   // Speed in cm/s, negative = approaching
    uint16_t resMm;     // Distance resolution in mm
    bool held;          // true = position frozen for heldMs: the radar is repeating a track it lost
    bool parked;        // true = moved under 30 cm in parkedMs: clutter like a fan or wall echo
} ld2450_target_t;

typedef struct {
    uart_port_t uartNum;    // UART peripheral, e.g. UART_NUM_1
    int rxPin;              // ESP RX <- radar TX
    int txPin;              // ESP TX -> radar RX
    bool mirrorX;           // true = flip lateral sign so +x is the viewer's right
    float maxAngleDeg;      // Angle that maps to gaze x = +-1
    float centerDeg;        // Radar bearing (radar +x side positive) of the eyes' straight ahead, for a module mounted turned
    float maxRangeM;        // Ignore targets farther than this
    float minRangeM;        // Ignore targets nearer than this
    uint32_t heldMs;        // Position frozen this long = lost track the radar is repeating, skip it (0 = off)
    uint32_t parkedMs;      // Moved under 30 cm this long = clutter, skip it (0 = off)
    uint32_t lockLostMs;    // Keep looking where the followed person was last seen this long
    uint32_t leadMs;        // Aim this far ahead of the person's sideways motion (0 = off)
    uint32_t smoothMs;      // Smooth the person's position over about this long (0 = off)
    bool trace;             // Print every frame ("RT ms x,y,speed,res ...") for tools/radar_capture.py
    // Settings kept in the radar, sent at start (it remembers them; -1 = leave as they are)
    int targetMode;         // 1 = single target (steadier), 2 = multi target (up to 3, factory default)
    int zoneType;           // 0 = no zone, 1 = only report people inside the rectangle, 2 = ignore people inside it
    int16_t zoneX1Mm, zoneY1Mm, zoneX2Mm, zoneY2Mm;   // Rectangle corners in radar coordinates
} ld2450_config_t;

// Counters for checking the wiring: bytes > 0 means the radar's TX reaches us, goodFrames > 0 means the baud rate is right
typedef struct {
    uint32_t bytes;         // Bytes received
    uint32_t goodFrames;    // Frames with a valid header and tail (the radar sends about 10 a second)
    uint32_t badFrames;     // Frames with a bad tail (noise or wrong baud rate)
} ld2450_stats_t;

esp_err_t ld2450Init(const ld2450_config_t *config);                    // Install UART and start the reader task
int ld2450GetTargets(ld2450_target_t out[LD2450_MAX_TARGETS], uint32_t *ageMs);   // Copy latest frame, returns valid count

void ld2450GetStats(ld2450_stats_t *out);                              // Copy the counters

// Gaze source: follows one moving person (nearest first), mapped to gaze x by bearing angle. Call ld2450Init first.
extern const gaze_source_t gazeSourceLd2450;

#ifdef __cplusplus
}
#endif
