// eye_animator.c - Gaze arbitration, spring-smoothed eye motion, and the random blink scheduler.
#include "eye_animator.h"

#include <math.h>
#include "blink_effect.h"
#include "esp_log.h"
#include "esp_random.h"

#define GAZE_OMEGA          28.0f       // Spring speed (rad/s): quick saccade, no overshoot
#define MAX_DT_S            0.05f       // Clamp long frame gaps so the spring stays stable
#define BLINK_MIN_MS        2200        // Shortest pause between random blinks
#define BLINK_MAX_MS        6500        // Longest pause
#define SACCADE_BLINK_JUMP  0.8f        // Gaze jump that may trigger a blink
#define SACCADE_BLINK_PCT   30          // Chance of that blink, percent

static const char *TAG = "animator";

static const gaze_source_t *const *gazeSources;    // Priority list
static int gazeSourceCount;
static int activeSource = -1;           // Index of the source driving the gaze
static gaze_target_t lastTarget;        // Its latest target

static float posX, posY, velX, velY;    // Spring state for gaze
static uint32_t lastMs;                 // Previous update time

static bool autoBlink = true;           // Random blinks on
static const blink_effect_t *blink;     // Blink in progress, or NULL
static uint32_t blinkStartMs;
static uint32_t nextBlinkMs;            // When the next random blink starts
static uint32_t blinkSeq;               // Blinks started so far
static uint8_t blinkIndex;              // Last effect index
static int pendingBlink = -1;           // Blink requested from outside, started on the next update

static uint32_t randBetween(uint32_t lo, uint32_t hi) {
    return lo + esp_random() % (hi - lo + 1);
}

// Pick an effect by weight
static int pickBlinkIndex(void) {
    int total = 0;
    for (int i = 0; i < blinkEffectCount; i++) total += blinkEffectTable[i]->weight;
    int roll = (int)(esp_random() % (uint32_t)total);
    for (int i = 0; i < blinkEffectCount; i++) {
        roll -= blinkEffectTable[i]->weight;
        if (roll < 0) return i;
    }
    return 0;
}

// Advance a critically damped spring by dt using its closed-form solution.
// (A simple Euler step blows up once dt > 1 / omega, about 36 ms, which a slow frame can hit.)
static void springStep(float *pos, float *vel, float target, float dt) {
    float err = *pos - target;                                      // Distance from the target
    float decay = expf(-GAZE_OMEGA * dt);
    float tmp = (*vel + GAZE_OMEGA * err) * dt;
    *pos = target + (err + tmp) * decay;
    *vel = (*vel - GAZE_OMEGA * tmp) * decay;
}

static void beginBlink(int index, uint32_t nowMs) {
    if (index < 0 || index >= blinkEffectCount) return;
    blink = blinkEffectTable[index];
    blinkStartMs = nowMs;
    blinkIndex = (uint8_t)index;
    blinkSeq++;                                                     // Lets the sync leader tell the follower
    nextBlinkMs = nowMs + blink->durationMs + randBetween(BLINK_MIN_MS, BLINK_MAX_MS);
}

void eyeAnimatorInit(const gaze_source_t *const *sources, int count) {
    gazeSources = sources;
    gazeSourceCount = count;
    for (int i = 0; i < count; i++) {
        if (sources[i]->init && sources[i]->init() != ESP_OK) {     // A failed source just never wins
            ESP_LOGW(TAG, "Gaze source %s failed to start", sources[i]->name);
        }
    }
    nextBlinkMs = randBetween(800, 2000);                          // First blink soon after boot
}

void eyeAnimatorStartBlink(int effectIndex) {
    pendingBlink = effectIndex;
}

void eyeAnimatorSetAutoBlink(bool enabled) {
    autoBlink = enabled;
}

uint32_t eyeAnimatorBlinkSeq(uint8_t *lastIndex) {
    if (lastIndex) *lastIndex = blinkIndex;
    return blinkSeq;
}

bool eyeAnimatorTracking(gaze_target_t *target) {
    if (target) *target = lastTarget;
    return activeSource >= 0 && gazeSources[activeSource] != &gazeSourceIdle;
}

void eyeAnimatorUpdate(uint32_t nowMs, face_state_t *out) {
    float dt = (lastMs == 0) ? 0.0f : (nowMs - lastMs) / 1000.0f;
    if (dt > MAX_DT_S) dt = MAX_DT_S;
    lastMs = nowMs;

    // Gaze: first source with a target wins
    gaze_target_t target = {0};
    int winner = -1;
    for (int i = 0; i < gazeSourceCount; i++) {
        if (gazeSources[i]->poll(nowMs, &target)) { winner = i; break; }
    }
    if (winner != activeSource && winner >= 0) {
        ESP_LOGI(TAG, "Gaze source: %s", gazeSources[winner]->name);
    }
    activeSource = winner;
    if (winner >= 0) {
        float jump = fabsf(target.x - lastTarget.x) + fabsf(target.y - lastTarget.y);
        if (jump > SACCADE_BLINK_JUMP && autoBlink && !blink && (int)(esp_random() % 100) < SACCADE_BLINK_PCT) {
            pendingBlink = 0;                                       // Big look-away: blink like a person does
        }
        lastTarget = target;
    }

    // Critically damped spring toward the target, exact solution: stable at any frame time
    springStep(&posX, &velX, lastTarget.x, dt);
    springStep(&posY, &velY, lastTarget.y, dt);

    // Blinks
    if (pendingBlink >= 0) {
        beginBlink(pendingBlink, nowMs);
        pendingBlink = -1;
    } else if (autoBlink && !blink && (int32_t)(nowMs - nextBlinkMs) >= 0) {
        beginBlink(pickBlinkIndex(), nowMs);
    }
    float openL = 1.0f, openR = 1.0f;
    if (blink) {
        float t = (float)(nowMs - blinkStartMs) / blink->durationMs;
        if (t >= 1.0f) blink = NULL;                                // Done
        else blink->sample(t, &openL, &openR);
    }

    // Pupils breathe a little, and open up when someone is close
    float pupil = 1.0f + 0.03f * sinf(nowMs * 0.0011f);
    bool tracking = eyeAnimatorTracking(NULL);
    if (tracking && lastTarget.distanceM > 0.0f && lastTarget.distanceM < 2.0f) {
        pupil += 0.10f * (2.0f - lastTarget.distanceM) / 2.0f;
    }

    out->gazeX = fmaxf(-1.0f, fminf(1.0f, posX));                   // Never past the edge of the range
    out->gazeY = fmaxf(-1.0f, fminf(1.0f, posY));
    out->openLeft = openL;
    out->openRight = openR;
    out->pupilScale = pupil;
    out->timeMs = nowMs;
}
