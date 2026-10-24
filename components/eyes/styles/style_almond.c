// style_almond.c - Wide almond-shaped eyes (side to side, not up and down), with a moving iris.
// The white is capped at 3/4 of the screen width. The whole eye shifts with the gaze (its corner reaches the
// screen edge) and the iris does the rest, touching the corner of the white at full gaze. Blinks close the
// lids over the iris.
// Two looks share this code:
//   eyeStyleGrinch - lime-yellow white, green iris, scowling lid slanting down to the nose, shaggy fur and brow
//   eyeStyleElf    - pale blue-white, luminous blue iris, long curled lashes, rosy lower lid, peach skin,
//                    blue hair falling over the forehead, all set in a dark surround so the eye stands out
// All drawn in code. The iris (with pupil and glints) is drawn once into a small picture, then placed each frame.
#include "eye_style.h"

#include <math.h>
#include <stdlib.h>

// ---------- Shared geometry (480x480 screen) ----------
#define SCREEN_C        240.0f      // Screen center
#define ALMOND_A        180.0f      // Half width of the white: 360 px = 3/4 of the screen
#define SHIFT_X         60.0f       // How far the whole eye shifts at full gaze (corner reaches the screen edge)...
#define SHIFT_Y         12.0f
#define EDGE_GAP        0.0f        // ...the iris covers the rest, all the way to the corner of the white
#define TRAVEL_Y_K      0.25f       // Iris up/down travel (x half height)
#define TOP_SHADOW      16.0f       // Soft shadow the upper lid casts on the eye, px

// One look: shape and colors
typedef struct {
    float b;                        // Half height of the white
    float irisR;                    // Iris radius
    float pupilRx, pupilRy;         // Pupil radii (x irisR)
    float pupilSoft;                // Pupil edge softness, px (1 = crisp)
    bool oneGlint;                  // true = a single big catchlight, false = big + small
    float outlineTop, outlineBottom;// Outline thickness along the top and bottom edge
    float outlineFlare;             // Extra top outline toward the outer corner (heavier lash line)
    uint32_t sclera, scleraShade;   // White: lower part, upper part
    uint32_t irisInner, irisOuter, irisRim;   // Iris: middle, outer, dark ring at the edge
    uint32_t outline;
    bool scowl;                     // Grinch: slanted angry lid and furry brow
    bool lashes;                    // Elf: lashes, lid crease, rosy lower lid
    float above, below;             // How far the drawing reaches above and below the white (brow, lashes, lids)
} almond_look_t;

// Iris picture, drawn once per look
typedef struct {
    uint16_t *color;                // (2R+1) x (2R+1) RGB565
    uint8_t *alpha;                 // Coverage 0..255 (round edge)
    int r;                          // Radius in px (picture size 2r+1)
} iris_pic_t;

// ---------- Grinch ----------
#define FUR_GREEN       0x5E9E2E    // Fur
#define FUR_DARK        0x2F5C17
#define FUR_LIGHT       0x8CC24A
#define LID_LINE        0x14200A    // Heavy dark line along the lid edge
#define BROW_COLOR      0x2C5414    // Furrowed brow

static const almond_look_t grinchLook = {
    .b = 92.0f, .irisR = 66.0f, .pupilRx = 0.20f, .pupilRy = 0.42f, .pupilSoft = 1.0f,
    .outlineTop = 5.0f, .outlineBottom = 4.0f,
    .sclera = 0xE2E05A, .scleraShade = 0xA9A72E,
    .irisInner = 0xB4F05A, .irisOuter = 0x52B02C, .irisRim = 0x1F4A12,
    .outline = 0x1A260C,
    .scowl = true,
    .above = 60.0f, .below = 10.0f,
};

// ---------- Elf ----------
#define SKIN_LIGHT      0xE6B89C    // Muted warm peach skin (darker than the eye white)...
#define SKIN_SHADE      0xB98068
#define SKIN_ROSY       0xD98C84    // ...rosier close to the eye
#define ELF_DARK        0x0C0A14    // Dark surround, so the eye is the focus
#define HAIR_TEAL       0x2696B4    // Blue-teal hair over the forehead and down the outer side
#define HAIR_DARK       0x0E4A66
#define HAIR_LIGHT      0x7ADCEC
#define HAIR_TIP        0x3FE6E0    // Tips: lighter and more saturated
#define ELF_BROW        0x3E2618    // Dark brown, thick, softly arched brow
#define LASH_COLOR      0x1E1410
#define CREASE_COLOR    0x8A5444    // Warm shadow in the lid crease
#define ROSY_COLOR      0xEE8C96    // Soft pink lower lid
#define LASH_COUNT      11          // Big cartoon lashes along the upper lid
#define LASH_LEN        58.0f       // Longest (outer corner); the inner ones are about half this
#define LASH_BASE       4.6f        // Thickness at the root, tapering to a point
#define LASH_LEAN       0.20f       // Lashes lean toward the outer corner...
#define LASH_CURL       0.015f      // ...and curl over more and more toward their tips
#define LOWER_LASH_COUNT 24         // Sparse short lashes on the lower lid
#define CREASE_GAP      24.0f       // Lid crease distance above the eye
#define CREASE_SOFT     10.0f       // Half width of the soft crease shadow
#define WING_LEN        34.0f       // Lash-line flick past the outer corner
#define LOWER_LASH_LEN  10.0f       // Short lashes on the lower lid

static const almond_look_t elfLook = {
    .b = 100.0f, .irisR = 74.0f, .pupilRx = 0.29f, .pupilRy = 0.29f, .pupilSoft = 5.0f, .oneGlint = true,
    .outlineTop = 5.0f, .outlineBottom = 2.0f, .outlineFlare = 4.0f,
    .sclera = 0xF4F8FF, .scleraShade = 0xBCC8E0,
    .irisInner = 0xA8E4FF, .irisOuter = 0x3C8EE0, .irisRim = 0x10305E,
    .outline = 0x2A1C18,
    .lashes = true,
    .above = 78.0f, .below = 14.0f,
};

static iris_pic_t grinchIris, elfIris;

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static uint8_t hash8(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (uint8_t)(h >> 24);
}

// Smooth value noise, cells cw x ch px, 0..255
static float valueNoise(float x, float y, float cw, float ch) {
    float fx = x / cw, fy = y / ch;
    int gx = (int)floorf(fx), gy = (int)floorf(fy);
    fx -= gx;
    fy -= gy;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    float a = hash8(gx, gy), b = hash8(gx + 1, gy), c = hash8(gx, gy + 1), d = hash8(gx + 1, gy + 1);
    return a + (b - a) * fx + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

static void mix(uint32_t a, uint32_t b, float t, float *r, float *g, float *bl) {
    t = clampf(t, 0.0f, 1.0f);
    *r = ((a >> 16) & 0xFF) * (1.0f - t) + ((b >> 16) & 0xFF) * t;
    *g = ((a >> 8) & 0xFF) * (1.0f - t) + ((b >> 8) & 0xFF) * t;
    *bl = (a & 0xFF) * (1.0f - t) + (b & 0xFF) * t;
}

static uint32_t pack888(float r, float g, float b) {
    return ((uint32_t)clampf(r, 0, 255) << 16) | ((uint32_t)clampf(g, 0, 255) << 8) | (uint32_t)clampf(b, 0, 255);
}

// Mix an RGB565 pixel toward another, a = 0..256
static inline uint16_t blend565(uint16_t px, uint16_t c, int a) {
    int r = px >> 11, g = (px >> 5) & 63, b = px & 31;
    r += (((c >> 11) - r) * a) >> 8;
    g += ((((c >> 5) & 63) - g) * a) >> 8;
    b += (((c & 31) - b) * a) >> 8;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// ---------- Iris picture ----------

static void buildIris(iris_pic_t *pic, const almond_look_t *look) {
    if (pic->color != NULL) return;
    int r = (int)look->irisR, n = 2 * r + 1;
    pic->r = r;
    pic->color = malloc((size_t)n * n * sizeof(uint16_t));
    pic->alpha = malloc((size_t)n * n);
    if (pic->color == NULL || pic->alpha == NULL) {                     // Out of memory: no iris
        free(pic->color);
        free(pic->alpha);
        pic->color = NULL;
        return;
    }
    float R = look->irisR, prx = look->pupilRx * R, pry = look->pupilRy * R;
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            float u = x - r, v = y - r;
            float d = sqrtf(u * u + v * v), q = d / R;
            float ang = atan2f(v, u);
            float red, grn, blu;
            mix(look->irisInner, look->irisOuter, powf(q, 1.2f), &red, &grn, &blu);   // Bright middle, deeper outside
            float fibers = 0.5f + 0.5f * sinf(ang * 29.0f + 2.0f * sinf(ang * 7.0f));   // Streaks out from the pupil
            float k = 1.0f + (fibers - 0.5f) * 0.32f * clampf((q - 0.25f) / 0.2f, 0.0f, 1.0f);
            red *= k;
            grn *= k;
            blu *= k;
            float rim = clampf((q - 0.80f) / 0.20f, 0.0f, 1.0f);       // Dark ring at the edge
            red += (((look->irisRim >> 16) & 0xFF) - red) * rim;
            grn += (((look->irisRim >> 8) & 0xFF) - grn) * rim;
            blu += ((look->irisRim & 0xFF) - blu) * rim;
            // Pupil, anti-aliased by its approximate distance in px
            float e = sqrtf((u / prx) * (u / prx) + (v / pry) * (v / pry));
            float pupilCover = clampf(((1.0f - e) * fminf(prx, pry)) / look->pupilSoft + 0.5f, 0.0f, 1.0f);   // Soft edge
            red *= 1.0f - pupilCover * 0.94f;
            grn *= 1.0f - pupilCover * 0.94f;
            blu *= 1.0f - pupilCover * 0.94f;
            // Glints: a big one upper left, a small one lower right
            float gx = u + 0.36f * R, gy = v + 0.36f * R, gr = (look->oneGlint ? 0.18f : 0.15f) * R;
            float glint = clampf(gr - sqrtf(gx * gx + gy * gy) + 0.5f, 0.0f, 1.0f);
            if (!look->oneGlint) {
                float sx = u - 0.30f * R, sy = v - 0.26f * R, sr = 0.06f * R;
                glint = fmaxf(glint, 0.8f * clampf(sr - sqrtf(sx * sx + sy * sy) + 0.5f, 0.0f, 1.0f));
            }
            red += (255.0f - red) * glint;
            grn += (255.0f - grn) * glint;
            blu += (255.0f - blu) * glint;
            pic->color[y * n + x] = canvasRgb565(pack888(red, grn, blu));
            pic->alpha[y * n + x] = (uint8_t)(255.0f * clampf(R - d + 0.5f, 0.0f, 1.0f));
        }
    }
}

// ---------- Backgrounds ----------
// These depend on the side (brows slant toward the nose, hair hangs on the outer side): eyeStyleIsLeftEye.

// Grinch's furrowed brow: a thick band slanting down toward the nose, ragged and tapered (eye-local, eye at rest)
static bool inBrow(const almond_look_t *look, int px, float y, float xn) {
    if (y >= 0.0f || xn <= -1.2f || xn >= 1.0f) return false;
    float browBottom = look->b * (-1.10f + 0.50f * (xn + 1.0f)) - 12.0f;
    float thick = (24.0f + 5.0f * (xn + 1.0f)) * clampf((1.0f - xn) * (1.0f / 0.35f), 0.0f, 1.0f) * clampf((xn + 1.2f) * (1.0f / 0.3f), 0.0f, 1.0f);
    float rag = hash8(px / 3, 7) * (8.0f / 255.0f) - 4.0f;
    return y < browBottom + rag && y > browBottom - thick + rag;
}


static void grinchBackground(uint16_t *buf, int w, int h) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            // Shaggy fur: noise stretched tall (strands), plus a fine layer, darker toward the rim
            float strands = valueNoise(x + 0.35f * y, y, 4.0f, 22.0f) / 255.0f;
            float fine = valueNoise(x - 0.2f * y, y, 2.0f, 9.0f) / 255.0f;
            float f = 0.6f * strands + 0.4f * fine;
            float dx = (x - SCREEN_C) / SCREEN_C, dy = (y - SCREEN_C) / SCREEN_C;
            float rim = clampf(dx * dx + dy * dy, 0.0f, 1.0f);
            float r, g, b;
            if (f < 0.5f) mix(FUR_DARK, FUR_GREEN, f * 2.0f, &r, &g, &b);
            else mix(FUR_GREEN, FUR_LIGHT, (f - 0.5f) * 2.0f, &r, &g, &b);
            r *= 1.0f - 0.35f * rim;
            g *= 1.0f - 0.35f * rim;
            b *= 1.0f - 0.35f * rim;
            float ex = x + 0.5f - SCREEN_C, ey = y + 0.5f - SCREEN_C;   // Furrowed brow, fixed above the eye
            if (inBrow(&grinchLook, x, ey, ex / ALMOND_A * (eyeStyleIsLeftEye ? 1.0f : -1.0f))) {
                float k = (0.55f + 0.25f * f) * 1.6f;                   // Still a little furry
                r = ((BROW_COLOR >> 16) & 0xFF) * k;
                g = ((BROW_COLOR >> 8) & 0xFF) * k;
                b = (BROW_COLOR & 0xFF) * k;
            }
            buf[y * w + x] = canvasRgb565(pack888(r, g, b));
        }
    }
}

// Hair: clumps of fine strands falling from the top of the screen over the forehead and down past the outer
// corner of the eye. The strands of a clump spread out at the roots and come together at the tips, which
// gives sharp, pointed locks. o = position toward the outer side of the face (px from the middle).
typedef struct {
    float o0;       // Clump center at the top of the screen
    float sweep;    // Drift toward the outer side per px down
    float yEnd;     // Tip (px from the screen middle, down = +)
    float spread;   // Half width of the clump at the roots
    float phase;    // Wave
} hair_clump_t;

static const hair_clump_t elfClumps[] = {
    {-90.0f, 0.08f, -185.0f, 26.0f, 0.0f},      // Short ones over the forehead toward the nose...
    {-45.0f, 0.10f, -168.0f, 28.0f, 1.3f},
    {0.0f, 0.13f, -148.0f, 30.0f, 2.1f},
    {45.0f, 0.16f, -122.0f, 30.0f, 0.7f},
    {90.0f, 0.18f, -88.0f, 32.0f, 2.8f},        // ...longer ones over the outer corner...
    {135.0f, 0.18f, -40.0f, 32.0f, 1.9f},
    {178.0f, 0.15f, 30.0f, 32.0f, 0.4f},        // ...and down the side of the face
    {218.0f, 0.08f, 120.0f, 30.0f, 2.4f},
    {250.0f, 0.02f, 200.0f, 26.0f, 1.0f},
};
#define ELF_CLUMPS          ((int)(sizeof(elfClumps) / sizeof(elfClumps[0])))
#define STRANDS_PER_CLUMP   36
#define STRAND_WIDTH        2.8f    // At the root, thinning toward the tip

// Small random number 0..1 for strand i of clump c, variant v (fixed: the same hair every boot)
static float strandRand(int c, int i, int v) {
    return hash8(c * 97 + i * 13 + v * 7, c + v * 31 + 11) / 255.0f;
}

// Blend a 0xRRGGBB color into an RGB565 pixel, a = 0..1
static uint16_t blendRgb(uint16_t px, float r, float g, float b, float a) {
    return blend565(px, canvasRgb565(pack888(r, g, b)), (int)(a * 256.0f));
}

static void elfBackground(uint16_t *buf, int w, int h) {
    float outer = eyeStyleIsLeftEye ? -1.0f : 1.0f;                     // Outer side of the face is screen left on the left eye
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float ex = x + 0.5f - SCREEN_C, ey = y + 0.5f - SCREEN_C, o = ex * outer;
            float r, g, b;
            // Muted peach skin, rosier near the eye, fading to near black toward the screen edge
            float dx = ex / 250.0f, dy = ey / (ey < 0.0f ? 230.0f : 175.0f);
            float q = sqrtf(dx * dx + dy * dy);
            mix(SKIN_LIGHT, SKIN_SHADE, q * q, &r, &g, &b);
            float ez = sqrtf((ex / 200.0f) * (ex / 200.0f) + (ey / 120.0f) * (ey / 120.0f));   // Close to the eye
            float rosy = 0.22f * clampf(1.2f - ez, 0.0f, 1.0f);
            r += (((SKIN_ROSY >> 16) & 0xFF) - r) * rosy;
            g += (((SKIN_ROSY >> 8) & 0xFF) - g) * rosy;
            b += ((SKIN_ROSY & 0xFF) - b) * rosy;
            float dark = clampf((q - 0.62f) / 0.40f, 0.0f, 1.0f);
            dark = dark * dark * (3.0f - 2.0f * dark);
            r += (((ELF_DARK >> 16) & 0xFF) - r) * dark;
            g += (((ELF_DARK >> 8) & 0xFF) - g) * dark;
            b += ((ELF_DARK & 0xFF) - b) * dark;

            // Brow close above the eye, made of short hair strokes packed close together: thick at the
            // inner end, rising gently, thinning to a tail past the outer corner. Strokes slant up and out.
            float t = (o + 150.0f) / 360.0f;                            // 0 inner end .. 1 tail
            if (t > 0.0f && t < 1.0f) {
                float mid = -146.0f - 30.0f * sinf(t * 3.14159f * 0.9f) + 12.0f * t;   // Gentle arch
                float half = (2.0f + 11.0f * powf(1.0f - t, 0.8f)) * clampf(t * 16.0f, 0.0f, 1.0f);
                float v = ey - mid;
                float p = (v + 0.45f * o) / 3.0f;                       // Stroke coordinate: one stroke per 3 px
                int stroke = (int)floorf(p);
                float frac = p - stroke;
                float reach = half * (0.75f + 0.5f * hash8(stroke, 3) / 255.0f);   // Strokes end at different places
                float edge = clampf(reach - fabsf(v) + 0.5f, 0.0f, 1.0f);
                float line = clampf(1.0f - fabsf(frac - 0.5f) * 3.2f, 0.0f, 1.0f);  // About 2 px of hair, 1 px gap
                float cover = edge * (0.45f + 0.55f * line);
                float tone = 0.7f + 0.6f * hash8(stroke, 9) / 255.0f;  // Each hair a slightly different brown
                r += (((ELF_BROW >> 16) & 0xFF) * tone - r) * cover;
                g += (((ELF_BROW >> 8) & 0xFF) * tone - g) * cover;
                b += ((ELF_BROW & 0xFF) * tone - b) * cover;
            }
            buf[y * w + x] = canvasRgb565(pack888(r, g, b));
        }
    }

    // Hair strands, row by row: each strand's center and width on this row, then a few pixels around it
    for (int y = 0; y < h; y++) {
        float ey = y + 0.5f - SCREEN_C, down = ey + 240.0f;             // Down from the top of the screen
        for (int c = 0; c < ELF_CLUMPS; c++) {
            const hair_clump_t *cl = &elfClumps[c];
            float clumpLen = cl->yEnd + 240.0f;
            float clumpCenter = cl->o0 + cl->sweep * down + 8.0f * sinf(down * 0.02f + cl->phase);
            float bodyLen = clumpLen * 0.80f;                           // Dark body under the strands: hair, not skin, between them
            if (down < bodyLen) {
                float tb = down / bodyLen;
                float bodyHalf = cl->spread * (1.0f - powf(tb, 1.3f)) * 0.9f;
                float bx = SCREEN_C + clumpCenter * outer;
                int x0 = (int)floorf(bx - bodyHalf - 1.0f), x1 = (int)ceilf(bx + bodyHalf + 1.0f);
                for (int x = x0; x <= x1; x++) {
                    if (x < 0 || x >= w) continue;
                    float cover = 0.9f * clampf(bodyHalf - fabsf(x + 0.5f - bx) + 0.5f, 0.0f, 1.0f);
                    if (cover > 0.0f) buf[y * w + x] = blendRgb(buf[y * w + x], (HAIR_DARK >> 16) & 0xFF, (HAIR_DARK >> 8) & 0xFF, HAIR_DARK & 0xFF, cover);
                }
            }
            for (int i = 0; i < STRANDS_PER_CLUMP; i++) {
                float len = clumpLen * (0.82f + 0.18f * strandRand(c, i, 0));
                if (down > len) continue;
                float tl = down / len;                                  // 0 root .. 1 tip
                float spread = (strandRand(c, i, 1) * 2.0f - 1.0f) * cl->spread;
                float conv = 1.0f - powf(tl, 1.3f);                     // Strands come together toward the tip
                float o = clumpCenter + spread * conv + 3.0f * sinf(down * 0.045f + strandRand(c, i, 2) * 6.0f) * conv;
                float half = 0.5f * STRAND_WIDTH * (1.0f - 0.7f * tl);
                float sx = SCREEN_C + o * outer;                        // Screen x of the strand center
                // Strand color: dark teal to bright, lighter toward the tip
                float shade = strandRand(c, i, 3);
                float hr, hg, hb;
                if (shade < 0.5f) mix(HAIR_DARK, HAIR_TEAL, shade * 2.0f, &hr, &hg, &hb);
                else mix(HAIR_TEAL, HAIR_LIGHT, (shade - 0.5f) * 1.6f, &hr, &hg, &hb);
                float tip = clampf((tl - 0.6f) * 2.0f, 0.0f, 0.6f);
                hr += (((HAIR_TIP >> 16) & 0xFF) - hr) * tip;
                hg += (((HAIR_TIP >> 8) & 0xFF) - hg) * tip;
                hb += ((HAIR_TIP & 0xFF) - hb) * tip;
                int x0 = (int)floorf(sx - half - 1.0f), x1 = (int)ceilf(sx + half + 1.0f);
                for (int x = x0; x <= x1; x++) {
                    if (x < 0 || x >= w) continue;
                    float cover = clampf(half - fabsf(x + 0.5f - sx) + 0.5f, 0.0f, 1.0f);
                    if (cover > 0.0f) buf[y * w + x] = blendRgb(buf[y * w + x], hr, hg, hb, cover);
                }
            }
        }
    }
}

static void grinchPrepare(void) { buildIris(&grinchIris, &grinchLook); }
static void elfPrepare(void) { buildIris(&elfIris, &elfLook); }

// ---------- Layout ----------

// Small gaze changes move only the iris; the eye starts shifting past this
static float deadZone(float g) {
    const float dz = 0.08f;
    if (g > dz) return (g - dz) / (1.0f - dz);
    if (g < -dz) return (g + dz) / (1.0f - dz);
    return 0.0f;
}

static int almondLayout(const face_state_t *face, eye_pose_t *poses, int maxPoses, const almond_look_t *look) {
    if (maxPoses < 1) return 0;
    eye_pose_t *p = &poses[0];
    p->isLeft = face->isLeftEye;
    p->cx = SCREEN_C + deadZone(face->gazeX) * SHIFT_X;
    p->cy = SCREEN_C + deadZone(face->gazeY) * SHIFT_Y;
    p->rx = ALMOND_A;
    p->ry = look->b;
    p->openness = p->isLeft ? face->openLeft : face->openRight;
    p->pupilScale = face->pupilScale;
    p->pupilDx = face->gazeX * (ALMOND_A - look->irisR - EDGE_GAP);    // Iris reaches the corner at full gaze
    p->pupilDy = face->gazeY * TRAVEL_Y_K * look->b;
    p->timeMs = face->timeMs;
    return 1;
}

static int grinchLayout(const face_state_t *f, eye_pose_t *p, int n) { return almondLayout(f, p, n, &grinchLook); }
static int elfLayout(const face_state_t *f, eye_pose_t *p, int n) { return almondLayout(f, p, n, &elfLook); }

// Kept tight: when the eye shifts, everything in here is redrawn
static eye_rect_t almondBounds(const eye_pose_t *pose, const almond_look_t *look) {
    return (eye_rect_t){(int)floorf(pose->cx - ALMOND_A - 24.0f), (int)floorf(pose->cy - pose->ry - look->above),
                        (int)ceilf(pose->cx + ALMOND_A + 24.0f), (int)ceilf(pose->cy + pose->ry + look->below)};
}

static eye_rect_t grinchBounds(const eye_pose_t *pose) { return almondBounds(pose, &grinchLook); }
static eye_rect_t elfBounds(const eye_pose_t *pose) { return almondBounds(pose, &elfLook); }

static eye_rect_t irisBounds(const eye_pose_t *pose) {
    const float r = 80.0f;                                              // Largest iris + edge
    float x = pose->cx + pose->pupilDx, y = pose->cy + pose->pupilDy;
    return (eye_rect_t){(int)floorf(x - r), (int)floorf(y - r), (int)ceilf(x + r), (int)ceilf(y + r)};
}

// ---------- Drawing ----------

// Draw one almond eye. The almond is where two big circles overlap; each pixel gets its distance to the
// upper and lower edge (cheap form, exact near the edge), which drives the outline, shading and extras.
static void almondDraw(eye_canvas_t *cv, const eye_pose_t *pose, const almond_look_t *look, const iris_pic_t *iris) {
    const float A = ALMOND_A;
    float open = clampf(pose->openness, 0.0f, 1.0f);
    float B = fmaxf(1.5f, pose->ry * open);                             // Lids close toward the middle line
    float d = (A * A - B * B) / (2.0f * B), R = B + d, inv2R = 0.5f / R;
    float nose = pose->isLeft ? 1.0f : -1.0f;                           // +x points to the nose on the left eye
    uint16_t outline = canvasRgb565(look->outline);
    uint16_t sclera = canvasRgb565(look->sclera), shade = canvasRgb565(look->scleraShade);
    uint16_t lidLine = canvasRgb565(LID_LINE);
    uint16_t lash = canvasRgb565(LASH_COLOR), crease = canvasRgb565(CREASE_COLOR), rosy = canvasRgb565(ROSY_COLOR);
    float ix0 = pose->cx + pose->pupilDx, iy0 = pose->cy + pose->pupilDy;   // Iris center
    int ir = iris->color ? iris->r : -1, in = 2 * ir + 1;
    const float invA = 1.0f / A;
    // How far past the almond each row needs per-pixel work (outline, lashes, rosy lid); the rest is left alone
    float reachTop = look->outlineTop + look->outlineFlare + 2.0f + (look->lashes ? LASH_LEN : 0.0f);
    float reachBot = look->outlineBottom + 2.0f + (look->lashes ? LOWER_LASH_LEN : 0.0f);
    const float invShadow = 1.0f / TOP_SHADOW, invCrease = 1.0f / (0.8f * A);   // Per-pixel math uses no divides:
                                                                        // the ESP32-S3 FPU has none, so they are slow calls
    float lashX[LASH_COUNT], lashLen[LASH_COUNT], lashInv[LASH_COUNT];  // Where each lash leaves the lid, its length
    for (int k = 0; k < LASH_COUNT; k++) {
        lashX[k] = A * (-0.85f + k * 1.7f / (LASH_COUNT - 1));
        lashLen[k] = LASH_LEN * (0.45f + 0.55f * clampf(1.0f - (lashX[k] * nose * invA + 0.85f) / 1.7f, 0.0f, 1.0f));   // Longer outside
        lashInv[k] = 1.0f / lashLen[k];
    }

    int xa = cv->x0, xb = cv->x0 + cv->w;
    for (int py = cv->y0; py < cv->y0 + cv->h; py++) {
        float y = py + 0.5f - pose->cy;
        float upper = (y - d) * (y - d), lower = (y + d) * (y + d);
        uint16_t *out = cv->buf + (py - cv->y0) * cv->w - cv->x0;

        // Columns of this row near the almond: inside the circle grown by the reach
        float reach = R + (y < 0.0f ? reachTop : reachBot);
        float arg = reach * reach - (y < 0.0f ? upper : lower);
        int xLo = xb, xHi = xb;                                         // Empty unless the row meets the eye
        if (arg > 0.0f) {
            float half = sqrtf(arg);
            xLo = (int)floorf(pose->cx - half);
            xHi = (int)ceilf(pose->cx + half);
            if (xLo < xa) xLo = xa;
            if (xHi > xb) xHi = xb;
            if (xLo > xHi) xLo = xHi;
        }
        if (xLo >= xHi) continue;
        // Shading of the white for this row: darker up under the lid
        int shadeA = (int)(256.0f * clampf(0.75f - (y + B) / (2.0f * B) * 1.1f, 0.0f, 0.75f));
        uint16_t whiteRow = blend565(sclera, shade, shadeA);
        int iy = (int)floorf(py + 0.5f - iy0) + ir;                      // Iris picture row
        int ixOff = (int)floorf(0.5f - ix0) + ir;                       // Iris picture column = px + ixOff
        for (int px = xLo; px < xHi; px++) {
            float x = px + 0.5f - pose->cx;
            float x2 = x * x;
            float dTop = (x2 + upper - R * R) * inv2R;                  // Distance to the upper edge (>0 = above it)
            float dBot = (x2 + lower - R * R) * inv2R;                  // Distance to the lower edge (>0 = below it)
            bool topSide = dTop > dBot;
            float s = topSide ? dTop : dBot;                            // Distance to the almond (>0 = outside)
            float xn = clampf(x * invA, -1.5f, 1.5f) * nose;               // -1 outer corner .. +1 nose corner
            float ow = topSide ? look->outlineTop + look->outlineFlare * clampf((-xn - 0.2f) * 1.25f, 0.0f, 1.0f)
                               : look->outlineBottom;                   // Top line heavier toward the outer corner

            if (s > ow + 1.0f) {
                // Outside the eye: extras on the skin or fur
                if (look->lashes) {
                    float h = s - ow;                                   // Height above the outline
                    if (topSide && y < 0.0f && h < LASH_LEN) {
                        // Lashes: thin strokes along the lid, leaning toward the outer corner, tapering
                        float xb0 = x + nose * (LASH_LEAN + LASH_CURL * h) * h;   // Where this point's lash leaves the lid
                        float t = (xb0 * invA + 0.85f) * ((LASH_COUNT - 1) / 1.7f);
                        int k = (int)(t + 100.5f) - 100;                  // Nearest lash (no floorf call)
                        if (k >= 0 && k < LASH_COUNT) {
                            float lx = lashX[k], len = lashLen[k];
                            float wdt = LASH_BASE * (1.0f - h * lashInv[k]);   // Thick root, sharp tip
                            float cover = clampf(wdt - fabsf(xb0 - lx) + 0.5f, 0.0f, 1.0f);
                            if (h < len && cover > 0.0f) out[px] = blend565(out[px], lash, (int)(cover * 256.0f));
                        }
                    }
                    if (topSide && y < 0.0f && fabsf(h - CREASE_GAP) < CREASE_SOFT && fabsf(x) < 0.8f * A) {   // Lid crease
                        float a = 1.0f - fabsf(h - CREASE_GAP) * (1.0f / CREASE_SOFT);
                        a = a * a * (1.0f - fabsf(x) * invCrease);
                        out[px] = blend565(out[px], crease, (int)(a * 110.0f));     // Soft warm shadow
                    }
                    float wo = -x * nose - 0.93f * A;                   // Past the outer corner
                    if (wo > 0.0f && wo < WING_LEN && y < 6.0f) {       // Flick of the lash line
                        float lineY = -0.42f * wo - 1.0f;
                        float thick = 4.5f * (1.0f - wo * (1.0f / WING_LEN)) + 0.6f;
                        float cover = clampf(thick - fabsf(y - lineY) + 0.5f, 0.0f, 1.0f);
                        if (cover > 0.0f) out[px] = blend565(out[px], lash, (int)(cover * 256.0f));
                    }
                    if (!topSide && y > 0.0f && h < 9.0f) {             // Rosy lower lid
                        out[px] = blend565(out[px], rosy, (int)((1.0f - h * (1.0f / 9.0f)) * 150.0f));
                    }
                    if (!topSide && y > 0.0f && h < LOWER_LASH_LEN) {   // Sparse short lower lashes, denser toward the outer corner
                        float xb0 = x + nose * LASH_LEAN * h;
                        float t = (xb0 * invA + 0.85f) * ((LOWER_LASH_COUNT - 1) / 1.7f);
                        int k = (int)(t + 100.5f) - 100;
                        float lx = A * (-0.85f + k * 1.7f / (LOWER_LASH_COUNT - 1));
                        bool outerThird = lx * nose * invA < -0.4f;
                        if (k >= 2 && k < LOWER_LASH_COUNT - 2 && (k % 3 == 0 || (outerThird && (k & 1)))) {
                            float wdt = 1.6f * (1.0f - h * (1.0f / LOWER_LASH_LEN));
                            float cover = clampf(wdt - fabsf(xb0 - lx) + 0.5f, 0.0f, 1.0f);
                            if (cover > 0.0f) out[px] = blend565(out[px], lash, (int)(cover * 120.0f));
                        }
                    }
                }
                continue;
            }

            uint16_t c;
            if (s >= 0.0f) {
                c = outline;                                            // Outline band
            } else {
                c = whiteRow;
                if (ir >= 0) {                                          // Iris, where it overlaps
                    int ix = px + ixOff;
                    if (ix >= 0 && ix < in && iy >= 0 && iy < in) {
                        int a = iris->alpha[iy * in + ix];
                        if (a) c = blend565(c, iris->color[iy * in + ix], a + (a >> 7));
                    }
                }
                if (dTop > -TOP_SHADOW) c = blend565(c, outline, (int)(90.0f * (1.0f + dTop * invShadow)));   // Lid shadow
                if (s > -1.0f) c = blend565(c, outline, (int)((s + 1.0f) * 256.0f));   // Smooth inner edge
            }
            if (look->scowl) {
                // Scowling lid: everything above a line slanting down toward the nose is lid fur
                float lidY = B * (-0.85f + 0.50f * (xn + 1.0f));
                if (y < lidY - 3.0f) continue;                          // The lid is the face's own fur (and brow)
                if (y < lidY + 3.0f) {
                    c = lidLine;                                        // Heavy line along the lid edge
                } else if (s < 0.0f && y < lidY + 3.0f + TOP_SHADOW) {
                    c = blend565(c, outline, (int)(110.0f * (1.0f - (y - lidY - 3.0f) * invShadow)));   // Lid shadow
                }
            }
            float edge = clampf(ow + 1.0f - s, 0.0f, 1.0f);             // Anti-aliased outer edge of the outline
            out[px] = edge >= 1.0f ? c : blend565(out[px], c, (int)(edge * 256.0f));
        }
    }
}

static void grinchDraw(eye_canvas_t *cv, const eye_pose_t *pose) { almondDraw(cv, pose, &grinchLook, &grinchIris); }
static void elfDraw(eye_canvas_t *cv, const eye_pose_t *pose) { almondDraw(cv, pose, &elfLook, &elfIris); }

const eye_style_t eyeStyleGrinch = {
    .name = "grinch",
    .renderBackground = grinchBackground,
    .layout = grinchLayout,
    .bounds = grinchBounds,
    .drawEye = grinchDraw,
    .detailBounds = irisBounds,
    .prepare = grinchPrepare,
};

const eye_style_t eyeStyleElf = {
    .name = "elf",
    .renderBackground = elfBackground,
    .layout = elfLayout,
    .bounds = elfBounds,
    .drawEye = elfDraw,
    .detailBounds = irisBounds,
    .prepare = elfPrepare,
};
