// eye_canvas.h - Tiny anti-aliased raster for RGB565 strips. Shapes are ellipses with soft or crisp edges.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// A window onto the screen: buf holds rows y0..y0+h-1, columns x0..x0+w-1, stride w
typedef struct {
    uint16_t *buf;
    int x0, y0;         // Screen position of buf[0]
    int w, h;           // Size in pixels
} eye_canvas_t;

// Axis-aligned ellipse in screen pixels (float for sub-pixel motion)
typedef struct {
    float cx, cy;       // Center
    float rx, ry;       // Radii
} eye_ellipse_t;

// Screen rectangle, half-open: [x0, x1) x [y0, y1)
typedef struct {
    int x0, y0, x1, y1;
} eye_rect_t;

// Fill an ellipse. color = 0xRRGGBB, alpha 0..255, edge = soft edge width in px (1 = crisp anti-aliasing).
// clip = optional ellipse the fill is limited to (anti-aliased), or NULL.
void canvasFillEllipse(eye_canvas_t *cv, const eye_ellipse_t *shape, uint32_t color, uint8_t alpha,
                       float edge, const eye_ellipse_t *clip);

// Bounding box of an ellipse grown by margin px
eye_rect_t canvasEllipseBounds(const eye_ellipse_t *e, float margin);

// Pack 0xRRGGBB to RGB565
static inline uint16_t canvasRgb565(uint32_t c) {
    return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
}

#ifdef __cplusplus
}
#endif
