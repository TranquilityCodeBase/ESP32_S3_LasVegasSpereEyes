// eye_style.h - Pluggable look of the face: background, where the eyes sit, and how one eye is drawn.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "eye_canvas.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EYE_MAX_EYES 4      // Most eyes a style may place

// Animated state of the whole face, produced by the animator each frame
typedef struct {
    float gazeX, gazeY;         // Smoothed look direction, -1..1 (+x right, +y down)
    float openLeft, openRight;  // Lid openness, 0 shut .. 1 open
    float pupilScale;           // 1 = normal, >1 dilated
    uint32_t timeMs;            // Animation clock, for styles with idle motion
    bool isLeftEye;             // Which eye this board shows (one-eye styles), viewer's left = true
} face_state_t;

// One eye ready to draw, in screen pixels
typedef struct {
    float cx, cy;               // Eye center
    float rx, ry;               // Eye radii when fully open
    float openness;             // 0 shut .. 1 open
    float pupilDx, pupilDy;     // Pupil offset from the eye center
    float pupilScale;           // Pupil size factor
    bool isLeft;                // Viewer's left eye
} eye_pose_t;

typedef struct eye_style {
    const char *name;
    void (*renderBackground)(uint16_t *buf, int w, int h);                      // Full static background, once
    int (*layout)(const face_state_t *face, eye_pose_t *poses, int maxPoses);   // Place the eyes, returns count
    eye_rect_t (*bounds)(const eye_pose_t *pose);                               // Screen area one eye can touch
    void (*drawEye)(eye_canvas_t *cv, const eye_pose_t *pose);                  // Draw one eye into the canvas
} eye_style_t;

// Built-in styles (styles/*.c)
extern const eye_style_t eyeStyleSphere;      // Yellow ball, one big white cartoon eye per screen (Las Vegas Sphere)
extern const eye_style_t eyeStyleSphereFace;  // Same look, both eyes on one screen

#ifdef __cplusplus
}
#endif
