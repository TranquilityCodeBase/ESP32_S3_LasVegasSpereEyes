// eye_renderer.h - Draws a style into double-buffered frame buffers, redrawing only around the eyes.
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "eye_style.h"

#ifdef __cplusplus
extern "C" {
#endif

// Display hooks, so the renderer does not depend on a board
typedef struct {
    int width, height;                  // Screen size in pixels
    uint16_t *(*backBuffer)(void);      // Buffer to draw into
    void (*flip)(void);                 // Show it, return when the other buffer is free
} eye_display_t;

esp_err_t eyeRendererInit(const eye_display_t *display, const eye_style_t *style);    // Renders the background once
void eyeRendererSetStyle(const eye_style_t *style);                                   // Swap style (redraws everything)
void eyeRendererDraw(const face_state_t *face);                                       // Draw one frame and flip
void eyeRendererStats(uint32_t *drawUs, uint32_t *flipUs);                            // Average draw and flip time since last call

#ifdef __cplusplus
}
#endif
