// blink_effect.h - Pluggable blink animations. Each effect maps time 0..1 to how open each eye is.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// One blink animation. sample() is pure: same t, same result, so two boards stay in step.
typedef struct blink_effect {
    const char *name;                                              // Short name for logs and sync
    uint32_t durationMs;                                           // Total length of the effect
    uint8_t weight;                                                // Relative chance in the random scheduler, 0 = never random
    void (*sample)(float t, float *openLeft, float *openRight);    // t 0..1 -> openness 0 (shut) .. 1 (open)
} blink_effect_t;

// Built-in effects (blink/blink_effects.c)
extern const blink_effect_t blinkClassic;      // Quick close, slower open
extern const blink_effect_t blinkDouble;       // Two quick blinks
extern const blink_effect_t blinkSleepy;       // Slow heavy-lidded blink
extern const blink_effect_t blinkWinkLeft;     // Left eye only
extern const blink_effect_t blinkWinkRight;    // Right eye only

// Default table used by the scheduler, in a fixed order so an index means the same effect on both boards
extern const blink_effect_t *const blinkEffectTable[];
extern const int blinkEffectCount;

#ifdef __cplusplus
}
#endif
