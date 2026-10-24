// style_sphere.c - Las Vegas Sphere look: shaded yellow ball with tall white cartoon eyes.
// eyeStyleSphere draws one big eye per screen (each board is one eye). eyeStyleSphereFace puts both eyes
// on one screen. Eyes slide across the ball when looking around and get narrower near the rim, like a globe.
#include "eye_style.h"

#include <math.h>

// ---------- Look ----------
#define BALL_LIT        0xFFD83A    // Yellow where the light hits
#define BALL_SHADE      0xF0A800    // Deeper orange-yellow toward the rim
#define BALL_HIGHLIGHT  0xFFF2B0    // Soft specular spot, upper left
#define EYE_SHADOW      0xB87800    // Drop shadow under each eye
#define EYE_OUTLINE     0x2E1C00    // Dark brown outline (also the closed-eye line)
#define SCLERA_SHADE    0xD9E0EA    // Cool gray under the upper lid
#define SCLERA_WHITE    0xFFFFFF
#define PUPIL_COLOR     0x121212
#define GLINT_COLOR     0xFFFFFF

// ---------- Geometry (480x480 screen) ----------
#define SCREEN_C        240.0f      // Screen and ball center
#define BALL_R          240.0f      // Ball radius = round panel radius

// Proportions of one eye, relative to its half height (ry). Same look at any size.
#define OUTLINE_K       0.07f       // Outline thickness
#define PUPIL_K         0.35f       // Pupil radius
#define SHADOW_DX_K     0.05f       // Drop shadow offset
#define SHADOW_DY_K     0.10f
#define SHADOW_SOFT_MAX 10.0f       // Soft edge limit in px (wider bands cost CPU)
#define LID_SOFT_MAX    8.0f        // Soft edge of the lid shadow on the white

// One eye per screen (each board is one eye of the face)
#define EYE1_RX         128.0f      // Eye half width
#define EYE1_RY         184.0f      // Eye half height
#define EYE1_SHIFT_X    118.0f      // How far the eye slides at full gaze: the white just reaches the screen edge,
                                    // the outline goes off screen. Past ~119 the white itself gets cut off.
#define EYE1_SHIFT_Y    30.0f
#define EYE1_PUPIL_GAP  0.0f        // White left between pupil and eye edge at full gaze (x ry): 0 = pupil touches the edge

// Two eyes on one screen (whole face on one board)
#define EYE2_SPACING    88.0f       // Eye center distance from the middle
#define EYE2_Y          222.0f      // Eye center height at rest
#define EYE2_RX         60.0f
#define EYE2_RY         86.0f
#define EYE2_SHIFT_X    52.0f
#define EYE2_SHIFT_Y    40.0f
#define EYE2_PUPIL_GAP  0.05f

// 4x4 ordered dither, so the gradient has no visible bands in RGB565
static const uint8_t bayer4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

// Mix two 0xRRGGBB colors, t 0..1, result per channel as float 0..255
static inline void mixColor(uint32_t a, uint32_t b, float t, float *r, float *g, float *bl) {
    *r = ((a >> 16) & 0xFF) * (1.0f - t) + ((b >> 16) & 0xFF) * t;
    *g = ((a >> 8) & 0xFF) * (1.0f - t) + ((b >> 8) & 0xFF) * t;
    *bl = (a & 0xFF) * (1.0f - t) + (b & 0xFF) * t;
}

// Ball shading: light from upper left and in front. Flat-cartoon, so the effect stays gentle.
static void sphereBackground(uint16_t *buf, int w, int h) {
    const float lx = -0.35f, ly = -0.45f, lz = 0.82f;                  // Light direction (unit length)
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float nx = (x + 0.5f - SCREEN_C) / BALL_R;                  // Surface normal of the ball
            float ny = (y + 0.5f - SCREEN_C) / BALL_R;
            float rr = nx * nx + ny * ny;
            if (rr > 1.0f) rr = 1.0f;                                   // Corners (hidden by the round glass)
            float nz = sqrtf(1.0f - rr);
            float lambert = nx * lx + ny * ly + nz * lz;                // 1 = facing the light
            if (lambert < 0.0f) lambert = 0.0f;
            float shade = 1.0f - powf(lambert, 0.6f);                   // 0 lit .. 1 shaded
            float r, g, b;
            mixColor(BALL_LIT, BALL_SHADE, shade * 0.9f, &r, &g, &b);
            float spec = powf(lambert, 40.0f) * 0.55f;                  // Small soft highlight
            r += (((BALL_HIGHLIGHT >> 16) & 0xFF) - r) * spec;         // Blend toward the highlight color
            g += (((BALL_HIGHLIGHT >> 8) & 0xFF) - g) * spec;
            b += ((BALL_HIGHLIGHT & 0xFF) - b) * spec;
            float d = bayer4[y & 3][x & 3] / 16.0f;                     // Dither offset 0..1
            int r5 = (int)(r / 255.0f * 31.0f + d);                     // Quantize with dither
            int g6 = (int)(g / 255.0f * 63.0f + d);
            int b5 = (int)(b / 255.0f * 31.0f + d);
            if (r5 > 31) r5 = 31;
            if (g6 > 63) g6 = 63;
            if (b5 > 31) b5 = 31;
            buf[y * w + x] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
        }
    }
}

// Fill one pose: size with foreshortening toward the rim, openness, and pupil offset from the gaze.
// pupilGap is the white left beside the pupil at full gaze, as a fraction of ry.
static void placeEye(const face_state_t *face, eye_pose_t *p, float cx, float cy, float rx, float ry, float pupilGap) {
    p->cx = cx;
    p->cy = cy;
    float dx = (cx - SCREEN_C) / BALL_R;                                // Position on the ball, -1..1
    float dy = (cy - SCREEN_C) / BALL_R;
    p->rx = rx * (1.0f - 0.35f * dx * dx);                              // Narrower toward the rim, like a globe
    p->ry = ry * (1.0f - 0.20f * dy * dy);
    p->openness = p->isLeft ? face->openLeft : face->openRight;
    p->pupilScale = face->pupilScale;
    float pr = PUPIL_K * p->ry * p->pupilScale;
    p->pupilDx = face->gazeX * (p->rx - pr - pupilGap * p->ry);        // Pupil looks too, staying inside the white
    p->pupilDy = face->gazeY * (p->ry - pr - 0.12f * p->ry);
}

// Small gaze changes move only the pupil; the eye body starts sliding past this.
// Keeps the eye still while resting, which also lets the renderer redraw just the pupil.
static float deadZone(float g) {
    const float dz = 0.08f;
    if (g > dz) return (g - dz) / (1.0f - dz);
    if (g < -dz) return (g + dz) / (1.0f - dz);
    return 0.0f;
}

static int sphereEyeLayout(const face_state_t *face, eye_pose_t *poses, int maxPoses) {
    if (maxPoses < 1) return 0;
    poses[0].isLeft = face->isLeftEye;                                  // This board's eye
    placeEye(face, &poses[0], SCREEN_C + deadZone(face->gazeX) * EYE1_SHIFT_X, SCREEN_C + deadZone(face->gazeY) * EYE1_SHIFT_Y,
             EYE1_RX, EYE1_RY, EYE1_PUPIL_GAP);
    return 1;
}

static int sphereFaceLayout(const face_state_t *face, eye_pose_t *poses, int maxPoses) {
    if (maxPoses < 2) return 0;
    float shiftX = face->gazeX * EYE2_SHIFT_X;                          // Whole face slides toward the target
    float shiftY = face->gazeY * EYE2_SHIFT_Y;
    for (int i = 0; i < 2; i++) {
        poses[i].isLeft = (i == 0);
        float cx = SCREEN_C + (poses[i].isLeft ? -EYE2_SPACING : EYE2_SPACING) + shiftX;
        placeEye(face, &poses[i], cx, EYE2_Y + shiftY, EYE2_RX, EYE2_RY, EYE2_PUPIL_GAP);
    }
    return 2;
}

static float shadowSoft(const eye_pose_t *pose) {
    float soft = 0.16f * pose->ry;                                      // Softness grows with size, capped
    return soft > SHADOW_SOFT_MAX ? SHADOW_SOFT_MAX : soft;
}

static eye_rect_t sphereBounds(const eye_pose_t *pose) {
    float ow = OUTLINE_K * pose->ry;
    // Drop shadow reaches farthest down and right; the outline reaches farthest up and left
    eye_ellipse_t shadow = {pose->cx + SHADOW_DX_K * pose->ry, pose->cy + SHADOW_DY_K * pose->ry, pose->rx + ow, pose->ry + ow};
    eye_rect_t r = canvasEllipseBounds(&shadow, shadowSoft(pose) + 1.0f);
    eye_ellipse_t outline = {pose->cx, pose->cy, pose->rx + ow, pose->ry + ow};
    eye_rect_t o = canvasEllipseBounds(&outline, 2.0f);
    if (o.x0 < r.x0) r.x0 = o.x0;
    if (o.y0 < r.y0) r.y0 = o.y0;
    return r;
}

static eye_rect_t sphereDetailBounds(const eye_pose_t *pose) {
    float pr = PUPIL_K * pose->ry * pose->pupilScale;                   // Glints sit inside the pupil
    eye_ellipse_t pupil = {pose->cx + pose->pupilDx, pose->cy + pose->pupilDy, pr, pr};
    return canvasEllipseBounds(&pupil, 2.0f);
}

static void sphereDrawEye(eye_canvas_t *cv, const eye_pose_t *pose) {
    float open = pose->openness < 0.0f ? 0.0f : (pose->openness > 1.0f ? 1.0f : pose->openness);
    float ry = pose->ry * open;                                         // Blink squashes the eye toward its middle
    float ow = OUTLINE_K * pose->ry;                                    // Outline width for this size

    eye_ellipse_t outline = {pose->cx, pose->cy, pose->rx + ow, ry + ow};
    eye_ellipse_t sclera = {pose->cx, pose->cy, pose->rx, ry};
    float lidSoft = 0.12f * pose->ry > LID_SOFT_MAX ? LID_SOFT_MAX : 0.12f * pose->ry;
    eye_ellipse_t lit = {pose->cx, pose->cy + ry * 0.14f, pose->rx * 0.97f, ry * 0.95f};
    eye_ellipse_t litCore = {lit.cx, lit.cy, lit.rx - lidSoft, lit.ry - lidSoft};    // Where the white is solid
    bool open1 = ry >= 1.0f;                                            // Any white showing

    eye_ellipse_t shadow = {pose->cx + SHADOW_DX_K * pose->ry, pose->cy + SHADOW_DY_K * pose->ry, pose->rx + ow, ry + ow};
    canvasFillEllipse(cv, &shadow, EYE_SHADOW, 90, shadowSoft(pose), NULL, &outline);   // Soft drop shadow, only outside the eye
    canvasFillEllipse(cv, &outline, EYE_OUTLINE, 255, 1.0f, NULL, open1 ? &sclera : NULL);   // Shut eye = just this dark line
    if (!open1) return;

    canvasFillEllipse(cv, &sclera, SCLERA_SHADE, 255, 1.0f, NULL, litCore.ry > 0.0f ? &litCore : NULL);   // Shaded white
    canvasFillEllipse(cv, &lit, SCLERA_WHITE, 255, lidSoft, &sclera, NULL);   // Bright lower part, soft lid shadow on top

    float pr = PUPIL_K * pose->ry * pose->pupilScale;
    float px = pose->cx + pose->pupilDx;
    float py = pose->cy + pose->pupilDy;
    eye_ellipse_t pupil = {px, py, pr, pr};
    canvasFillEllipse(cv, &pupil, PUPIL_COLOR, 255, 1.0f, &sclera, NULL);    // Pupil, hidden by the lids when blinking

    // Glints sit upper left on both eyes: one light source for the whole face
    eye_ellipse_t glintBig = {px - pr * 0.36f, py - pr * 0.38f, pr * 0.30f, pr * 0.30f};
    canvasFillEllipse(cv, &glintBig, GLINT_COLOR, 255, 1.0f, &sclera, NULL); // Main reflection
    eye_ellipse_t glintSmall = {px + pr * 0.36f, py + pr * 0.34f, pr * 0.13f, pr * 0.13f};
    canvasFillEllipse(cv, &glintSmall, GLINT_COLOR, 230, 1.0f, &sclera, NULL);   // Second, smaller reflection
}

const eye_style_t eyeStyleSphere = {
    .name = "sphere",
    .renderBackground = sphereBackground,
    .layout = sphereEyeLayout,
    .bounds = sphereBounds,
    .drawEye = sphereDrawEye,
    .detailBounds = sphereDetailBounds,
};

const eye_style_t eyeStyleSphereFace = {
    .name = "sphere_face",
    .renderBackground = sphereBackground,
    .layout = sphereFaceLayout,
    .bounds = sphereBounds,
    .drawEye = sphereDrawEye,
    .detailBounds = sphereDetailBounds,
};
