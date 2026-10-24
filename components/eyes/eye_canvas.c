// eye_canvas.c - Anti-aliased ellipse fill into RGB565 strips.
// Each row is split into a solid middle run (tight store loop) and edge pixels that get a coverage
// value from an approximate signed distance to the ellipse. That keeps edges smooth at low CPU cost.
#include "eye_canvas.h"

#include <math.h>
#include <stdbool.h>

bool eyeStyleIsLeftEye = true;                               // See eye_style.h

static float trimR = 1.0f, trimG = 1.0f, trimB = 1.0f;     // Color trim gains
static float invGamma = 1.0f;                                // 1 / gamma for grays (1 = off)
static float whiteR = 1.0f, whiteG = 1.0f, whiteB = 1.0f;  // White balance gains for grays and white

void canvasSetWhiteBalance(float r, float g, float b) {
    whiteR = r;
    whiteG = g;
    whiteB = b;
}

void canvasSetColorTrim(float r, float g, float b, float grayGamma) {
    trimR = r;
    trimG = g;
    trimB = b;
    invGamma = grayGamma > 0.0f ? 1.0f / grayGamma : 1.0f;
}

// Brighten (gamma > 1) or darken mid grays; black and white stay put
static inline float grayCurve(float c) {
    return 255.0f * powf(c / 255.0f, invGamma);
}

// The gains are weighted by saturation: strong colors (the yellow ball) get the full trim, while
// whites, grays and black stay untouched, so the eye white doesn't pick up a tint. The gray curve is the
// opposite: it only touches grays, for screens that show mid grays darker.
void canvasTrim(float *r, float *g, float *b) {
    float hi = fmaxf(*r, fmaxf(*g, *b));
    float lo = fminf(*r, fminf(*g, *b));
    float sat = hi > 0.0f ? (hi - lo) / hi : 0.0f;              // 0 = gray .. 1 = pure color
    *r = fminf(255.0f, fmaxf(0.0f, *r * (1.0f + (trimR - 1.0f) * sat)));   // Scale and clamp each channel
    *g = fminf(255.0f, fmaxf(0.0f, *g * (1.0f + (trimG - 1.0f) * sat)));
    *b = fminf(255.0f, fmaxf(0.0f, *b * (1.0f + (trimB - 1.0f) * sat)));
    if (invGamma != 1.0f) {                                     // Gray curve, also weighted: grays only
        float w = 1.0f - sat;
        *r += (grayCurve(*r) - *r) * w;
        *g += (grayCurve(*g) - *g) * w;
        *b += (grayCurve(*b) - *b) * w;
    }
    float w = 1.0f - sat;                                       // White balance: grays and white only
    *r = fminf(255.0f, *r * (1.0f + (whiteR - 1.0f) * w));
    *g = fminf(255.0f, *g * (1.0f + (whiteG - 1.0f) * w));
    *b = fminf(255.0f, *b * (1.0f + (whiteB - 1.0f) * w));
}

uint16_t canvasRgb565(uint32_t c) {
    float r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    canvasTrim(&r, &g, &b);
    return (uint16_t)((((int)(r + 0.5f) >> 3) << 11) | (((int)(g + 0.5f) >> 2) << 5) | ((int)(b + 0.5f) >> 3));
}

// Ellipse with the values the per-pixel math needs
typedef struct {
    float cx, cy;
    float rx, ry;
    float irx2, iry2;   // 1 / rx^2, 1 / ry^2
} prepared_t;

static inline prepared_t prepare(const eye_ellipse_t *e) {
    prepared_t p = {e->cx, e->cy, e->rx, e->ry, 1.0f / (e->rx * e->rx), 1.0f / (e->ry * e->ry)};
    return p;
}

// 1 / sqrt(x), bit trick plus one Newton step (about 0.2 % error, plenty for anti-aliasing)
static inline float fastRsqrt(float x) {
    union { float f; uint32_t i; } u = {x};
    u.i = 0x5F3759DF - (u.i >> 1);
    return u.f * (1.5f - 0.5f * x * u.f * u.f);
}

// Half width of the ellipse (grown by grow px) at vertical offset dy from its center, or -1 if the row misses it
static inline float halfWidth(const prepared_t *e, float dy, float grow) {
    float rx = e->rx + grow;                            // Grown radii
    float ry = e->ry + grow;
    if (rx <= 0.0f || ry <= 0.0f) return -1.0f;         // Shrunk away completely
    float t = 1.0f - (dy * dy) / (ry * ry);             // 1 at the center row, 0 at the top and bottom
    if (t <= 0.0f) return -1.0f;
    return rx * sqrtf(t);
}

// Coverage 0..1 of a pixel center by the ellipse. invEdge = 1 / soft edge width in px.
static inline float coverage(const prepared_t *e, float px, float py, float invEdge) {
    float dx = px - e->cx;
    float dy = py - e->cy;
    float ax = dx * e->irx2;                            // Half gradient of the implicit function
    float ay = dy * e->iry2;
    float f = dx * ax + dy * ay - 1.0f;                 // < 0 inside, > 0 outside
    float g2 = ax * ax + ay * ay;
    if (g2 < 1e-12f) return 1.0f;                       // Exact center
    float d = 0.5f * f * fastRsqrt(g2);                 // Approximate distance to the edge in px
    float c = 0.5f - d * invEdge;                       // Distance -> coverage ramp
    return c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
}

// Blend src over dst with alpha 0..256, per channel in RGB565
static inline uint16_t blend565(uint16_t dst, uint16_t src, int a) {
    int dr = dst >> 11, dg = (dst >> 5) & 0x3F, db = dst & 0x1F;     // Unpack destination
    int sr = src >> 11, sg = (src >> 5) & 0x3F, sb = src & 0x1F;     // Unpack source
    dr += ((sr - dr) * a) >> 8;                                      // Lerp each channel
    dg += ((sg - dg) * a) >> 8;
    db += ((sb - db) * a) >> 8;
    return (uint16_t)((dr << 11) | (dg << 5) | db);
}

// First and one-past-last pixel whose center lies within halfWidth hw of cx
static inline void innerRange(float cx, float hw, int *a, int *b) {
    *a = (int)ceilf(cx - hw - 0.5f);
    *b = (int)floorf(cx + hw - 0.5f) + 1;
}

void canvasFillEllipse(eye_canvas_t *cv, const eye_ellipse_t *shape, uint32_t color, uint8_t alpha,
                       float edge, const eye_ellipse_t *clip, const eye_ellipse_t *hole) {
    if (alpha == 0 || shape->rx <= 0.0f || shape->ry <= 0.0f) return;   // Nothing to draw
    if (edge < 1.0f) edge = 1.0f;                                       // Minimum is 1 px anti-aliasing
    const uint16_t src = canvasRgb565(color);
    const float invEdge = 1.0f / edge;
    const int alpha256 = alpha + (alpha >> 7);                          // 0..255 -> 0..256
    const prepared_t s = prepare(shape);
    const prepared_t c = clip ? prepare(clip) : s;
    const prepared_t hp = hole ? prepare(hole) : s;

    // Rows the shape can touch, limited to the canvas
    int yStart = (int)floorf(shape->cy - shape->ry - edge);
    int yEnd = (int)ceilf(shape->cy + shape->ry + edge) + 1;
    if (yStart < cv->y0) yStart = cv->y0;
    if (yEnd > cv->y0 + cv->h) yEnd = cv->y0 + cv->h;

    for (int y = yStart; y < yEnd; y++) {
        float py = y + 0.5f;                                            // Pixel center
        float hwOut = halfWidth(&s, py - s.cy, edge);                   // Outside this: uncovered
        if (hwOut < 0.0f) continue;
        int xa = (int)floorf(s.cx - hwOut);
        int xb = (int)ceilf(s.cx + hwOut) + 1;
        int sa, sb;                                                     // Fully covered run
        float hwIn = halfWidth(&s, py - s.cy, -edge);
        if (hwIn >= 0.0f) innerRange(s.cx, hwIn, &sa, &sb);
        else sa = sb = xa;

        int ca = 0, cb = 0;                                             // Fully inside the clip
        if (clip) {
            float clipOut = halfWidth(&c, py - c.cy, 1.0f);
            if (clipOut < 0.0f) continue;
            int oa = (int)floorf(c.cx - clipOut);
            int ob = (int)ceilf(c.cx + clipOut) + 1;
            if (oa > xa) xa = oa;                                       // Narrow to the clip
            if (ob < xb) xb = ob;
            float clipIn = halfWidth(&c, py - c.cy, -1.0f);
            if (clipIn >= 0.0f) innerRange(c.cx, clipIn, &ca, &cb);
            if (ca > sa) sa = ca;                                       // Solid only where both are solid
            if (cb < sb) sb = cb;
        }
        if (xa < cv->x0) xa = cv->x0;                                   // Limit to the canvas
        if (xb > cv->x0 + cv->w) xb = cv->x0 + cv->w;
        if (xa >= xb) continue;
        if (sa < xa) sa = xa;                                           // Solid run inside the drawn span
        if (sb > xb) sb = xb;
        if (sb < sa) sb = sa;

        int ha = xb, hb = xb;                                           // Pixels to skip (inside the hole)
        if (hole) {
            float holeIn = halfWidth(&hp, py - hp.cy, -1.0f);
            if (holeIn >= 0.0f) innerRange(hp.cx, holeIn, &ha, &hb);
            if (hb <= ha) ha = hb = xb;
        }

        uint16_t *row = cv->buf + (y - cv->y0) * cv->w - cv->x0;        // Index with screen x
        int runEnd = sb < ha ? sb : ha;                                 // Solid run up to the hole
        int runStart = sa > hb ? sa : hb;                               // Solid run after the hole
        for (int x = sa; x < runEnd; x++) row[x] = (alpha256 >= 256) ? src : blend565(row[x], src, alpha256);
        for (int x = runStart; x < sb; x++) row[x] = (alpha256 >= 256) ? src : blend565(row[x], src, alpha256);
        for (int x = xa; x < xb; x++) {                                 // Edge pixels on both sides
            if (x == sa && sb > sa) { x = sb - 1; continue; }           // Skip the solid run
            if (x >= ha && x < hb) { x = hb - 1; continue; }            // Skip the hole
            float px = x + 0.5f;
            float cov = coverage(&s, px, py, invEdge);
            if (clip && (x < ca || x >= cb)) {                          // Near the clip edge: combine coverages
                float cc = coverage(&c, px, py, 1.0f);
                if (cc < cov) cov = cc;
            }
            int a = (int)(cov * alpha256 + 0.5f);                       // Final alpha 0..256
            if (a >= 256) row[x] = src;
            else if (a > 0) row[x] = blend565(row[x], src, a);
        }
    }
}

eye_rect_t canvasEllipseBounds(const eye_ellipse_t *e, float margin) {
    eye_rect_t r = {
        .x0 = (int)floorf(e->cx - e->rx - margin),
        .y0 = (int)floorf(e->cy - e->ry - margin),
        .x1 = (int)ceilf(e->cx + e->rx + margin) + 1,
        .y1 = (int)ceilf(e->cy + e->ry + margin) + 1,
    };
    return r;
}
