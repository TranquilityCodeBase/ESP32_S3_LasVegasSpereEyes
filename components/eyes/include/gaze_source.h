// gaze_source.h - Pluggable input that tells the eyes where to look (idle wander, LD2450 radar, ESP-NOW peer).
// The animator polls sources in priority order. The first one that returns true wins this frame.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Where to look, normalized to the face. Screen axes as the viewer sees them.
typedef struct {
    float x;            // -1 = far left .. +1 = far right
    float y;            // -1 = up .. +1 = down
    float distanceM;    // Distance to the target in meters, 0 = unknown
} gaze_target_t;

// One gaze input. init may be NULL. poll is called every frame from the render task.
typedef struct gaze_source {
    const char *name;                                       // Short name for logs
    esp_err_t (*init)(void);                                // Start the source (UART, timers). NULL = nothing to do
    bool (*poll)(uint32_t nowMs, gaze_target_t *out);       // true = has a target now, out filled
} gaze_source_t;

// Built-in sources
extern const gaze_source_t gazeSourceIdle;     // Always valid: wanders, glances around, returns to center (gaze/gaze_idle.c)

#ifdef __cplusplus
}
#endif
