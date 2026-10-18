// gaze_idle.c - Idle look-around: holds a point for a while, then glances somewhere new.
// Mostly small shifts near the last point, sometimes a big look to the side, sometimes back to center.
#include "gaze_source.h"

#include "esp_random.h"

static gaze_target_t current;           // Point being looked at
static uint32_t nextChangeMs;           // When to pick a new point

// Random float in [lo, hi)
static float randRange(float lo, float hi) {
    return lo + (hi - lo) * (esp_random() / 4294967296.0f);
}

static float clampUnit(float v) {
    return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
}

static void pickNext(uint32_t nowMs) {
    float roll = randRange(0.0f, 1.0f);
    if (roll < 0.50f) {                                         // Small shift near the current point
        current.x = clampUnit(current.x + randRange(-0.35f, 0.35f));
        current.y = clampUnit(current.y + randRange(-0.25f, 0.25f));
    } else if (roll < 0.80f) {                                  // Look somewhere new
        current.x = randRange(-0.95f, 0.95f);
        current.y = randRange(-0.7f, 0.7f);
    } else if (roll < 0.92f) {                                  // Glance far to one side
        current.x = (esp_random() & 1) ? randRange(0.8f, 1.0f) : randRange(-1.0f, -0.8f);
        current.y = randRange(-0.3f, 0.3f);
    } else {                                                    // Back to looking straight out
        current.x = 0.0f;
        current.y = 0.0f;
    }
    nextChangeMs = nowMs + (uint32_t)randRange(600.0f, 3200.0f);   // Hold time
}

static bool idlePoll(uint32_t nowMs, gaze_target_t *out) {
    if ((int32_t)(nowMs - nextChangeMs) >= 0) pickNext(nowMs);     // Time for a new point
    *out = current;
    return true;                                                // Always has something to look at
}

const gaze_source_t gazeSourceIdle = {
    .name = "idle",
    .init = NULL,
    .poll = idlePoll,
};
