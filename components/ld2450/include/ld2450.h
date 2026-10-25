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
    bool held;          // true = position frozen, the radar is holding a track it lost (gaze ignores these)
} ld2450_target_t;

typedef struct {
    uart_port_t uartNum;    // UART peripheral, e.g. UART_NUM_1
    int rxPin;              // ESP RX <- radar TX
    int txPin;              // ESP TX -> radar RX
    bool mirrorX;           // true = flip lateral sign so +x is the viewer's right
    float maxAngleDeg;      // Angle that maps to gaze x = +-1
    float maxRangeM;        // Ignore targets farther than this
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

// Gaze source: nearest valid target, mapped to gaze x by bearing angle. Call ld2450Init first.
extern const gaze_source_t gazeSourceLd2450;

#ifdef __cplusplus
}
#endif
