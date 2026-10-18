// style_sphere.c - Las Vegas Sphere look: a shaded yellow ball with two tall white cartoon eyes.
// Eyes slide across the ball when looking around and get narrower near the rim, like a real globe.
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
#define EYE_SPACING     88.0f       // Eye center distance from the middle
#define EYE_Y           222.0f      // Eye center height at rest
#define EYE_RX          60.0f       // Eye half width
#define EYE_RY          86.0f       // Eye half height
#define OUTLINE_W       6.0f        // Outline thickness
#define PUPIL_R         30.0f       // Pupil radius
#define FACE_SHIFT_X    52.0f       // How far the whole face slides at full gaze
#define FACE_SHIFT_Y    40.0f
#define SHADOW_DX       4.0f        // Drop shadow offset and softness
#define SHADOW_DY       9.0f
#define SHADOW_SOFT     14.0f

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

static int sphereLayout(const face_state_t *face, eye_pose_t *poses, int maxPoses) {
    if (maxPoses < 2) return 0;
    float shiftX = face->gazeX * FACE_SHIFT_X;                          // Whole face slides toward the target
    float shiftY = face->gazeY * FACE_SHIFT_Y;
    for (int i = 0; i < 2; i++) {
        eye_pose_t *p = &poses[i];
        p->isLeft = (i == 0);
        p->cx = SCREEN_C + (p->isLeft ? -EYE_SPACING : EYE_SPACING) + shiftX;
        p->cy = EYE_Y + shiftY;
        float dx = (p->cx - SCREEN_C) / BALL_R;                         // Position on the ball, -1..1
        float dy = (p->cy - SCREEN_C) / BALL_R;
        p->rx = EYE_RX * (1.0f - 0.35f * dx * dx);                      // Foreshortening toward the rim
        p->ry = EYE_RY * (1.0f - 0.20f * dy * dy);
        p->openness = p->isLeft ? face->openLeft : face->openRight;
        p->pupilScale = face->pupilScale;
        float pr = PUPIL_R * p->pupilScale;
        p->pupilDx = face->gazeX * (p->rx - pr - 4.0f);                 // Pupil looks too, staying inside the white
        p->pupilDy = face->gazeY * (p->ry - pr - 10.0f);
    }
    return 2;
}

static eye_rect_t sphereBounds(const eye_pose_t *pose) {
    // Drop shadow reaches farthest (offset + soft edge); outline is covered by it
    eye_ellipse_t shadow = {pose->cx + SHADOW_DX, pose->cy + SHADOW_DY, pose->rx + OUTLINE_W, pose->ry + OUTLINE_W};
    eye_rect_t r = canvasEllipseBounds(&shadow, SHADOW_SOFT + 1.0f);
    if (r.x0 > pose->cx - pose->rx - OUTLINE_W - 2) r.x0 = (int)(pose->cx - pose->rx - OUTLINE_W - 2);  // Outline on the other side
    if (r.y0 > pose->cy - pose->ry - OUTLINE_W - 2) r.y0 = (int)(pose->cy - pose->ry - OUTLINE_W - 2);
    return r;
}

static void sphereDrawEye(eye_canvas_t *cv, const eye_pose_t *pose) {
    float open = pose->openness < 0.0f ? 0.0f : (pose->openness > 1.0f ? 1.0f : pose->openness);
    float ry = pose->ry * open;                                         // Blink squashes the eye toward its middle

    eye_ellipse_t shadow = {pose->cx + SHADOW_DX, pose->cy + SHADOW_DY, pose->rx + OUTLINE_W, ry + OUTLINE_W};
    canvasFillEllipse(cv, &shadow, EYE_SHADOW, 90, SHADOW_SOFT, NULL); // Soft drop shadow on the ball

    eye_ellipse_t outline = {pose->cx, pose->cy, pose->rx + OUTLINE_W, ry + OUTLINE_W};
    canvasFillEllipse(cv, &outline, EYE_OUTLINE, 255, 1.0f, NULL);     // Shut eye = just this dark line
    if (ry < 1.0f) return;

    eye_ellipse_t sclera = {pose->cx, pose->cy, pose->rx, ry};
    canvasFillEllipse(cv, &sclera, SCLERA_SHADE, 255, 1.0f, NULL);     // Shaded white
    eye_ellipse_t lit = {pose->cx, pose->cy + ry * 0.14f, pose->rx * 0.97f, ry * 0.95f};
    canvasFillEllipse(cv, &lit, SCLERA_WHITE, 255, 10.0f, &sclera);    // Bright lower part, soft lid shadow on top

    float pr = PUPIL_R * pose->pupilScale;
    float px = pose->cx + pose->pupilDx;
    float py = pose->cy + pose->pupilDy;
    eye_ellipse_t pupil = {px, py, pr, pr};
    canvasFillEllipse(cv, &pupil, PUPIL_COLOR, 255, 1.0f, &sclera);    // Pupil, hidden by the lids when blinking

    eye_ellipse_t glintBig = {px - pr * 0.36f, py - pr * 0.38f, pr * 0.30f, pr * 0.30f};
    canvasFillEllipse(cv, &glintBig, GLINT_COLOR, 255, 1.0f, &sclera); // Main reflection
    eye_ellipse_t glintSmall = {px + pr * 0.36f, py + pr * 0.34f, pr * 0.13f, pr * 0.13f};
    canvasFillEllipse(cv, &glintSmall, GLINT_COLOR, 230, 1.0f, &sclera);   // Second, smaller reflection
}

const eye_style_t eyeStyleSphere = {
    .name = "sphere",
    .renderBackground = sphereBackground,
    .layout = sphereLayout,
    .bounds = sphereBounds,
    .drawEye = sphereDrawEye,
};
