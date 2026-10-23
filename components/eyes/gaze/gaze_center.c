// gaze_center.c - Always looks straight ahead. Fallback for the slave eye while it has no command from the master.
#include "gaze_source.h"

static bool centerPoll(uint32_t nowMs, gaze_target_t *out) {
    out->x = 0.0f;                                              // Straight ahead
    out->y = 0.0f;
    out->distanceM = 0.0f;
    return true;                                                // Always valid
}

const gaze_source_t gazeSourceCenter = {
    .name = "center",
    .init = NULL,
    .poll = centerPoll,
};
