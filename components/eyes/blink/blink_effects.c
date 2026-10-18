// blink_effects.c - Built-in blink animations. Add a new one here and list it in blinkEffectTable.
#include "blink_effect.h"

// Openness for one close-and-open: shut by closeEnd, held until openStart, open again at 1.
// Eased so the lid accelerates down and settles gently on the way up.
static float lidCurve(float t, float closeEnd, float openStart) {
    if (t <= 0.0f || t >= 1.0f) return 1.0f;                       // Outside the blink
    if (t < closeEnd) {                                             // Closing: ease in (speeds up)
        float k = t / closeEnd;
        return 1.0f - k * k;
    }
    if (t < openStart) return 0.0f;                                 // Held shut
    float k = (t - openStart) / (1.0f - openStart);                 // Opening: ease out (slows down)
    return 1.0f - (1.0f - k) * (1.0f - k);
}

static void classicSample(float t, float *l, float *r) {
    *l = *r = lidCurve(t, 0.35f, 0.45f);                           // Fast close, short hold, slower open
}

static void doubleSample(float t, float *l, float *r) {
    float local = t < 0.5f ? t * 2.0f : (t - 0.5f) * 2.0f;          // Two half-length blinks
    *l = *r = lidCurve(local, 0.35f, 0.45f);
}

static void sleepySample(float t, float *l, float *r) {
    *l = *r = lidCurve(t, 0.40f, 0.60f);                           // Long hold, slow everything
}

static void winkLeftSample(float t, float *l, float *r) {
    *l = lidCurve(t, 0.25f, 0.65f);                                // Left eye winks
    *r = 1.0f - 0.15f * (1.0f - lidCurve(t, 0.25f, 0.65f));        // Right eye squints a little
}

static void winkRightSample(float t, float *l, float *r) {
    winkLeftSample(t, r, l);                                        // Mirror of the left wink
}

const blink_effect_t blinkClassic = {"classic", 180, 70, classicSample};
const blink_effect_t blinkDouble = {"double", 380, 15, doubleSample};
const blink_effect_t blinkSleepy = {"sleepy", 650, 8, sleepySample};
const blink_effect_t blinkWinkLeft = {"wink_left", 450, 2, winkLeftSample};
const blink_effect_t blinkWinkRight = {"wink_right", 450, 2, winkRightSample};

// Order matters: the index travels over ESP-NOW. Append only.
const blink_effect_t *const blinkEffectTable[] = {
    &blinkClassic,
    &blinkDouble,
    &blinkSleepy,
    &blinkWinkLeft,
    &blinkWinkRight,
};
const int blinkEffectCount = sizeof(blinkEffectTable) / sizeof(blinkEffectTable[0]);
