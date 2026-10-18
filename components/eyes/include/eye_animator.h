// eye_animator.h - Turns gaze sources and blink effects into a smooth face_state_t every frame.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "eye_style.h"
#include "gaze_source.h"

#ifdef __cplusplus
extern "C" {
#endif

void eyeAnimatorInit(const gaze_source_t *const *sources, int count);  // Sources in priority order, highest first
void eyeAnimatorUpdate(uint32_t nowMs, face_state_t *out);             // Advance the animation to nowMs
void eyeAnimatorStartBlink(int effectIndex);                           // Start a blink now (index into blinkEffectTable)
void eyeAnimatorSetAutoBlink(bool enabled);                            // false = only blink when told (sync follower)
uint32_t eyeAnimatorBlinkSeq(uint8_t *lastIndex);                      // Count of blinks started, and the last effect
bool eyeAnimatorTracking(gaze_target_t *target);                       // true = a non-idle source is driving the gaze

#ifdef __cplusplus
}
#endif
