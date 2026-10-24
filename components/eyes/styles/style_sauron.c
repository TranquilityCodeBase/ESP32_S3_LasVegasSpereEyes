// style_sauron.c - Flaming dark-lord eye (inspired by the Eye of Sauron): a pointed almond of fire with a
// black slit pupil, ringed by flames that lick upward. One eye per screen, so the two boards make a pair.
// All drawn in code, no stored images. The fire is a heat map built once (heat 0..255 per pixel, eye-local),
// turned into color through a palette each frame. A scrolling noise tile makes the flames flicker.
#include "eye_style.h"

#include <math.h>
#include <stdlib.h>

// ---------- Look ----------
#define BG_GLOW         0x260500    // Dark ember red behind the eye (screen middle)
#define BG_EDGE         0x000000    // Black at the screen rim
#define SLIT_COLOR      0x000000    // The pupil
#define SLIT_GLOW       0xFFE490    // White-hot glow around the pupil

// ---------- Geometry (480x480 screen) ----------
#define SCREEN_C        240.0f      // Screen center
#define ALMOND_A        160         // Almond half width (pointed tips left and right)
#define ALMOND_B        100         // Almond half height
#define CORONA          50          // How far the flames reach past the almond
#define TEX_W           (2 * (ALMOND_A + CORONA))   // Heat map size, eye-local (420 x 300)
#define TEX_H           (2 * (ALMOND_B + CORONA))
#define SHIFT_X         55.0f       // How far the eye slides at full gaze
#define SHIFT_Y         25.0f
#define PUPIL_TRAVEL_X  0.40f       // Pupil travel inside the almond (x half width)
#define PUPIL_TRAVEL_Y  0.08f       // (x half height)
#define SLIT_RX_K       0.065f      // Slit half width (x ALMOND_A)
#define SLIT_RY_K       0.82f       // Slit half height (x ALMOND_B)

// ---------- Fire motion ----------
#define NOISE_SIZE      128         // Noise tile, wraps around (power of 2)
#define FLAME_RISE      70          // Flame rise speed, noise rows per second
#define MIN_OPEN        0.04f       // Shut eye still shows a thin line of fire

static uint16_t *heatMap;           // TEX_W x TEX_H: low byte = heat, high byte = flicker amount. NULL = not built
static uint8_t noise[NOISE_SIZE][NOISE_SIZE];   // Tileable value noise, 0..255
static uint16_t palette[256];       // Heat -> RGB565 glow, added onto the background

// 4x4 ordered dither, so the background glow has no visible bands in RGB565
static const uint8_t bayer4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

// Heat palette stops: black, dark red, red, orange, amber, yellow, white-hot
static const struct { uint8_t heat; uint32_t color; } paletteStops[] = {
    {0, 0x000000}, {40, 0x1A0000}, {90, 0x6A0A00}, {140, 0xC02800},
    {180, 0xFF6A00}, {215, 0xFFA830}, {240, 0xFFE080}, {255, 0xFFFFE0},
};

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Small integer hash -> 0..255, for the noise lattice
static uint8_t hash8(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (uint8_t)(h >> 24);
}

// Smooth value noise on a lattice of 'cell' px, wrapping at NOISE_SIZE so the tile repeats seamlessly
static float valueNoise(int x, int y, int cell) {
    int cells = NOISE_SIZE / cell;
    int gx = x / cell, gy = y / cell;
    float fx = (float)(x % cell) / cell, fy = (float)(y % cell) / cell;
    fx = fx * fx * (3.0f - 2.0f * fx);                                  // Smoothstep between lattice points
    fy = fy * fy * (3.0f - 2.0f * fy);
    int x1 = (gx + 1) % cells, y1 = (gy + 1) % cells;
    float a = hash8(gx, gy), b = hash8(x1, gy), c = hash8(gx, y1), d = hash8(x1, y1);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

// Signed distance to the almond in px (negative inside). The almond is where two big circles overlap.
static float almondDistance(float x, float y) {
    const float d = (float)(ALMOND_A * ALMOND_A - ALMOND_B * ALMOND_B) / (2.0f * ALMOND_B);   // Circle center offset
    const float r = ALMOND_B + d;                                                             // Circle radius
    float top = sqrtf(x * x + (y - d) * (y - d)) - r;                   // Circle below the middle (top edge)
    float bottom = sqrtf(x * x + (y + d) * (y + d)) - r;                // Circle above the middle (bottom edge)
    return top > bottom ? top : bottom;
}

// Heat (0..255) and flicker amount (0..255) at an eye-local point
static void fireAt(float x, float y, float *heat, float *flicker) {
    float s = almondDistance(x, y);
    if (s < 0.0f) {
        // Iris: amber middle cooling to deep red-orange at the edge, with fiery rays out from the middle.
        // The white-hot part is drawn around the slit instead, so it follows the pupil.
        float q = sqrtf((x / ALMOND_A) * (x / ALMOND_A) + (y / ALMOND_B) * (y / ALMOND_B));
        if (q > 1.0f) q = 1.0f;
        float ang = atan2f(y, x);
        float rays = 0.45f * sinf(ang * 23.0f + 1.3f) + 0.30f * sinf(ang * 37.0f + 0.7f) + 0.25f * sinf(ang * 61.0f + 2.1f);
        float h = 228.0f - 118.0f * powf(q, 1.1f) + rays * 45.0f * q;
        float rim = clampf(1.0f + s / 10.0f, 0.0f, 1.0f);               // 0..1 over the last 10 px inside the edge
        *heat = h + (235.0f - h) * rim;                                 // Bright burning ring along the edge
        *flicker = 25.0f;                                               // Gentle shimmer inside
    } else {
        // Corona: glow falling off away from the almond; the noise tears it into flame tongues
        float h = 235.0f * expf(-s / 22.0f);
        *heat = h;
        *flicker = h * 0.95f * clampf(s / 15.0f, 0.0f, 1.0f);           // Solid at the edge, ragged farther out
    }
}

static void buildPalette(void) {
    int stop = 0;
    for (int h = 0; h < 256; h++) {
        while (paletteStops[stop + 1].heat < h) stop++;
        uint32_t a = paletteStops[stop].color, b = paletteStops[stop + 1].color;
        float t = (float)(h - paletteStops[stop].heat) / (paletteStops[stop + 1].heat - paletteStops[stop].heat);
        uint32_t r = (uint32_t)(((a >> 16) & 0xFF) * (1.0f - t) + ((b >> 16) & 0xFF) * t);
        uint32_t g = (uint32_t)(((a >> 8) & 0xFF) * (1.0f - t) + ((b >> 8) & 0xFF) * t);
        uint32_t bl = (uint32_t)((a & 0xFF) * (1.0f - t) + (b & 0xFF) * t);
        palette[h] = canvasRgb565((r << 16) | (g << 8) | bl);           // Per-board color trim applied here
    }
}

// Build the heat map, noise tile and palette once (about 250 KB, lands in PSRAM)
static void buildFire(void) {
    if (heatMap == NULL) {
        heatMap = malloc((size_t)TEX_W * TEX_H * sizeof(uint16_t));
        if (heatMap == NULL) return;                                    // Out of memory: the eye just won't draw
        for (int ty = 0; ty < TEX_H; ty++) {
            for (int tx = 0; tx < TEX_W; tx++) {
                float heat, flicker;
                fireAt(tx + 0.5f - TEX_W / 2, ty + 0.5f - TEX_H / 2, &heat, &flicker);
                int h = (int)clampf(heat, 0.0f, 255.0f), f = (int)clampf(flicker, 0.0f, 255.0f);
                heatMap[ty * TEX_W + tx] = (uint16_t)(h | (f << 8));
            }
        }
        for (int y = 0; y < NOISE_SIZE; y++) {                          // Two octaves: big tongues + fine crackle
            for (int x = 0; x < NOISE_SIZE; x++) {
                noise[y][x] = (uint8_t)(0.65f * valueNoise(x, y, 16) + 0.35f * valueNoise(x, y, 8));
            }
        }
    }
    buildPalette();                                                     // Every time: the color trim may have changed
}

// Black screen with a dark ember glow in the middle
static void sauronBackground(uint16_t *buf, int w, int h) {
    buildFire();
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float dx = (x + 0.5f - SCREEN_C) / SCREEN_C, dy = (y + 0.5f - SCREEN_C) / SCREEN_C;
            float t = clampf(1.0f - sqrtf(dx * dx + dy * dy), 0.0f, 1.0f);
            t = t * t;                                                  // Glow strongest in the middle
            float r = ((BG_EDGE >> 16) & 0xFF) + ((float)((BG_GLOW >> 16) & 0xFF) - ((BG_EDGE >> 16) & 0xFF)) * t;
            float g = ((BG_EDGE >> 8) & 0xFF) + ((float)((BG_GLOW >> 8) & 0xFF) - ((BG_EDGE >> 8) & 0xFF)) * t;
            float b = (BG_EDGE & 0xFF) + ((float)(BG_GLOW & 0xFF) - (BG_EDGE & 0xFF)) * t;
            canvasTrim(&r, &g, &b);
            float d = bayer4[y & 3][x & 3] / 16.0f;                     // Dither offset 0..1
            int r5 = (int)(r / 255.0f * 31.0f + d), g6 = (int)(g / 255.0f * 63.0f + d), b5 = (int)(b / 255.0f * 31.0f + d);
            buf[y * w + x] = (uint16_t)(((r5 > 31 ? 31 : r5) << 11) | ((g6 > 63 ? 63 : g6) << 5) | (b5 > 31 ? 31 : b5));
        }
    }
}

// Small gaze changes move only the pupil; the eye starts sliding past this
static float deadZone(float g) {
    const float dz = 0.08f;
    if (g > dz) return (g - dz) / (1.0f - dz);
    if (g < -dz) return (g + dz) / (1.0f - dz);
    return 0.0f;
}

static int sauronLayout(const face_state_t *face, eye_pose_t *poses, int maxPoses) {
    if (maxPoses < 1) return 0;
    eye_pose_t *p = &poses[0];
    p->isLeft = face->isLeftEye;
    p->cx = SCREEN_C + deadZone(face->gazeX) * SHIFT_X;                 // The whole eye turns toward the target...
    p->cy = SCREEN_C + deadZone(face->gazeY) * SHIFT_Y;
    p->rx = ALMOND_A;
    p->ry = ALMOND_B;
    p->openness = p->isLeft ? face->openLeft : face->openRight;
    p->pupilScale = face->pupilScale;
    p->pupilDx = face->gazeX * PUPIL_TRAVEL_X * ALMOND_A;               // ...and the slit leads it
    p->pupilDy = face->gazeY * PUPIL_TRAVEL_Y * ALMOND_B;
    p->timeMs = face->timeMs;
    return 1;
}

// Screen area of the heat map around this eye
static eye_rect_t sauronBounds(const eye_pose_t *pose) {
    int icx = (int)lroundf(pose->cx), icy = (int)lroundf(pose->cy);
    return (eye_rect_t){icx - TEX_W / 2, icy - TEX_H / 2, icx + TEX_W / 2, icy + TEX_H / 2};
}

// Add a glow color onto an RGB565 pixel, each channel capped at full
static inline uint16_t addGlow(uint16_t px, uint16_t glow) {
    int r = (px >> 11) + (glow >> 11), g = ((px >> 5) & 63) + ((glow >> 5) & 63), b = (px & 31) + (glow & 31);
    return (uint16_t)(((r > 31 ? 31 : r) << 11) | ((g > 63 ? 63 : g) << 5) | (b > 31 ? 31 : b));
}

static void sauronDrawEye(eye_canvas_t *cv, const eye_pose_t *pose) {
    if (heatMap == NULL) return;
    float open = clampf(pose->openness, MIN_OPEN, 1.0f);               // Blink squashes the fire toward its middle line
    int icx = (int)lroundf(pose->cx), icy = (int)lroundf(pose->cy);
    float t = pose->timeMs / 1000.0f;
    int rise = (int)(pose->timeMs * FLAME_RISE / 1000);                 // Noise scrolls up: flames rise
    int sway = (int)(6.0f * sinf(t * 1.7f));                            // ...and lean a little side to side
    int pulse = (int)(256.0f + 14.0f * sinf(t * 2.7f) + 6.0f * sinf(t * 11.9f));   // Whole fire breathes, x256

    int xStart = icx - TEX_W / 2 > cv->x0 ? icx - TEX_W / 2 : cv->x0;  // Columns this strip shares with the heat map
    int xEnd = icx + TEX_W / 2 < cv->x0 + cv->w ? icx + TEX_W / 2 : cv->x0 + cv->w;
    for (int y = cv->y0; y < cv->y0 + cv->h; y++) {
        int ty = (int)floorf((y + 0.5f - icy) / open) + TEX_H / 2;      // Heat map row, stretched while blinking
        if (ty < 0 || ty >= TEX_H) continue;
        const uint16_t *heatRow = heatMap + ty * TEX_W;
        const uint8_t *noiseRow = noise[((ty >> 1) + rise) & (NOISE_SIZE - 1)];   // Rows doubled: taller tongues
        uint16_t *out = cv->buf + (y - cv->y0) * cv->w - cv->x0;
        for (int x = xStart; x < xEnd; x++) {
            int tx = x - icx + TEX_W / 2;
            uint16_t v = heatRow[tx];
            if (v == 0) continue;                                       // Nothing here: leave the background
            int h = (((v & 0xFF) * pulse) >> 8) + (((v >> 8) * (noiseRow[(tx + sway) & (NOISE_SIZE - 1)] - 128)) >> 7);
            if (h <= 0) continue;
            out[x] = addGlow(out[x], palette[h > 255 ? 255 : h]);
        }
    }

    if (pose->openness < 0.15f) return;                                 // Slit hidden while the eye is nearly shut
    eye_ellipse_t almond = {pose->cx, pose->cy, ALMOND_A, ALMOND_B * open};   // Keeps the slit inside the eye
    float px = pose->cx + pose->pupilDx, py = pose->cy + pose->pupilDy;
    float srx = SLIT_RX_K * ALMOND_A * pose->pupilScale, sry = SLIT_RY_K * ALMOND_B * open;
    eye_ellipse_t glow = {px, py, srx + 30.0f, sry * 1.04f};
    canvasFillEllipse(cv, &glow, SLIT_GLOW, 200, 26.0f, &almond, NULL);       // White-hot around the slit
    eye_ellipse_t slit = {px, py, srx, sry};
    canvasFillEllipse(cv, &slit, SLIT_COLOR, 255, 1.5f, &almond, NULL);       // The black slit itself
}

const eye_style_t eyeStyleSauron = {
    .name = "sauron",
    .renderBackground = sauronBackground,
    .layout = sauronLayout,
    .bounds = sauronBounds,
    .drawEye = sauronDrawEye,
    .detailBounds = NULL,           // The flames move every frame, so the whole eye always redraws
};
