// gaze_idle.c - Idle look-around: mostly looks straight ahead, and every few seconds does a slow scan
// center -> left -> right -> center. Slow, eased moves read as a real eye; fast ones look mechanical.
#include "gaze_source.h"

#include "esp_random.h"

// ---------- Timing (ms) ----------
#define REST_MIN_MS         3000        // Time looking ahead between scans
#define REST_MAX_MS         6000
#define CENTER_TO_SIDE_MS   1000        // Center to full left
#define SIDE_TO_SIDE_MS     1500        // Full left to full right
#define SIDE_HOLD_MS        400         // Pause at each side
#define MICRO_MIN_MS        900         // Tiny drifts while resting, so the eye never looks frozen
#define MICRO_MAX_MS        2200
#define MICRO_AMOUNT        0.04f       // Size of those drifts (gaze units)
#define SCAN_X              1.0f        // How far left and right the scan goes (1 = full: pupil touches the white's edge)

// One step of the scan: move to x over moveMs, then hold for holdMs
typedef struct {
    float x;
    uint32_t moveMs;
    uint32_t holdMs;
} scan_step_t;

static const scan_step_t scanSteps[] = {
    {-SCAN_X, CENTER_TO_SIDE_MS, SIDE_HOLD_MS},     // Look left
    {SCAN_X, SIDE_TO_SIDE_MS, SIDE_HOLD_MS},        // Sweep to the right
    {0.0f, CENTER_TO_SIDE_MS, 0},                   // Back to center
};
#define SCAN_STEP_COUNT (int)(sizeof(scanSteps) / sizeof(scanSteps[0]))

static int step = -1;                   // Current scan step, -1 = resting
static uint32_t stepStartMs;            // When the current step (or rest) began
static uint32_t restMs;                 // Length of the current rest
static float fromX, fromY;              // Where the current move started
static float nowX, nowY;                // Gaze reported last time
static float microX, microY;            // Small drift target while resting
static uint32_t nextMicroMs;

static float randRange(float lo, float hi) {
    return lo + (hi - lo) * (esp_random() / 4294967296.0f);
}

// Smooth start and stop (0..1 -> 0..1)
static float easeInOut(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

static void startRest(uint32_t nowMs) {
    step = -1;
    stepStartMs = nowMs;
    restMs = (uint32_t)randRange(REST_MIN_MS, REST_MAX_MS);
    microX = microY = 0.0f;
    nextMicroMs = nowMs + (uint32_t)randRange(MICRO_MIN_MS, MICRO_MAX_MS);
}

static void startStep(int index, uint32_t nowMs) {
    step = index;
    stepStartMs = nowMs;
    fromX = nowX;                                               // Move from wherever the eye is
    fromY = nowY;
}

static bool idlePoll(uint32_t nowMs, gaze_target_t *out) {
    if (stepStartMs == 0) startRest(nowMs);                     // First call
    uint32_t elapsed = nowMs - stepStartMs;

    if (step < 0) {                                             // Resting, looking ahead
        if ((int32_t)(nowMs - nextMicroMs) >= 0) {              // New tiny drift target
            microX = randRange(-MICRO_AMOUNT, MICRO_AMOUNT);
            microY = randRange(-MICRO_AMOUNT, MICRO_AMOUNT);
            nextMicroMs = nowMs + (uint32_t)randRange(MICRO_MIN_MS, MICRO_MAX_MS);
        }
        nowX = microX;                                          // The animator's spring smooths these
        nowY = microY;
        if (elapsed >= restMs) startStep(0, nowMs);             // Time to scan
    } else {
        const scan_step_t *s = &scanSteps[step];
        float k = easeInOut((float)elapsed / s->moveMs);        // 0..1 along this move
        nowX = fromX + (s->x - fromX) * k;
        nowY = fromY * (1.0f - k);                              // Level out while scanning
        if (elapsed >= s->moveMs + s->holdMs) {                 // Move and hold done
            if (step + 1 < SCAN_STEP_COUNT) startStep(step + 1, nowMs);
            else startRest(nowMs);
        }
    }

    out->x = nowX;
    out->y = nowY;
    out->distanceM = 0.0f;
    return true;                                                // Always has something to look at
}

const gaze_source_t gazeSourceIdle = {
    .name = "idle",
    .init = NULL,
    .poll = idlePoll,
};
