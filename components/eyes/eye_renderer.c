// eye_renderer.c - Dirty-rectangle renderer over two page-flipped frame buffers.
// The background never changes, so each frame only redraws the eyes' area: the area they cover now plus
// the area they covered two frames ago (the last time this buffer was drawn). Work happens in a small
// internal-RAM strip, then the strip is copied to the PSRAM frame buffer.
#include "eye_renderer.h"

#include <string.h>
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#define STRIP_H     32                  // Strip height in lines (internal RAM: width x 32 x 2 bytes)
#define MAX_RECTS   (EYE_MAX_EYES * 2)  // Current + stale rectangles

static const char *TAG = "renderer";

static eye_display_t disp;              // Display hooks
static const eye_style_t *style;        // Current look
static uint16_t *background;            // Static background, full screen, PSRAM
static uint16_t *strip;                 // Work strip, internal RAM
static int fullRedraws;                 // Frames left that must redraw the whole screen
static int parity;                      // Which of the two buffers is the back buffer now
static eye_rect_t lastRects[2][EYE_MAX_EYES];  // Eye areas last drawn into each buffer
static int lastCount[2];
static int64_t drawUsSum, flipUsSum;      // Timing totals for eyeRendererStats
static uint32_t statFrames;

static bool rectsOverlap(const eye_rect_t *a, const eye_rect_t *b) {
    return a->x0 < b->x1 && b->x0 < a->x1 && a->y0 < b->y1 && b->y0 < a->y1;
}

static eye_rect_t rectUnion(eye_rect_t a, const eye_rect_t *b) {
    if (b->x0 < a.x0) a.x0 = b->x0;
    if (b->y0 < a.y0) a.y0 = b->y0;
    if (b->x1 > a.x1) a.x1 = b->x1;
    if (b->y1 > a.y1) a.y1 = b->y1;
    return a;
}

static eye_rect_t rectClamp(eye_rect_t r) {
    if (r.x0 < 0) r.x0 = 0;
    if (r.y0 < 0) r.y0 = 0;
    if (r.x1 > disp.width) r.x1 = disp.width;
    if (r.y1 > disp.height) r.y1 = disp.height;
    return r;
}

// Merge overlapping rectangles so no pixel is drawn twice. Returns the new count.
static int mergeRects(eye_rect_t *rects, int count) {
    bool merged = true;
    while (merged) {
        merged = false;
        for (int i = 0; i < count && !merged; i++) {
            for (int j = i + 1; j < count; j++) {
                if (rectsOverlap(&rects[i], &rects[j])) {
                    rects[i] = rectUnion(rects[i], &rects[j]);    // Grow i, drop j
                    rects[j] = rects[--count];
                    merged = true;
                    break;
                }
            }
        }
    }
    return count;
}

// Redraw one screen rectangle: background, then every eye that touches it, strip by strip
static void drawRect(uint16_t *fb, const eye_rect_t *r, const eye_pose_t *poses, const eye_rect_t *bounds, int eyeCount) {
    int w = r->x1 - r->x0;
    if (w <= 0 || r->y1 <= r->y0) return;
    for (int sy = r->y0; sy < r->y1; sy += STRIP_H) {
        int h = (r->y1 - sy < STRIP_H) ? r->y1 - sy : STRIP_H;
        eye_canvas_t cv = {strip, r->x0, sy, w, h};
        for (int y = 0; y < h; y++) {                                  // Background rows into the strip
            memcpy(strip + y * w, background + (sy + y) * disp.width + r->x0, w * sizeof(uint16_t));
        }
        eye_rect_t stripRect = {r->x0, sy, r->x1, sy + h};
        for (int i = 0; i < eyeCount; i++) {                           // Eyes that reach this strip
            if (rectsOverlap(&bounds[i], &stripRect)) style->drawEye(&cv, &poses[i]);
        }
        for (int y = 0; y < h; y++) {                                  // Strip rows into the frame buffer
            memcpy(fb + (sy + y) * disp.width + r->x0, strip + y * w, w * sizeof(uint16_t));
        }
    }
}

void eyeRendererSetStyle(const eye_style_t *newStyle) {
    style = newStyle;
    style->renderBackground(background, disp.width, disp.height);    // New static background
    fullRedraws = 2;                                                  // Both buffers need everything
    ESP_LOGI(TAG, "Style: %s", style->name);
}

esp_err_t eyeRendererInit(const eye_display_t *display, const eye_style_t *initialStyle) {
    disp = *display;
    size_t bgBytes = (size_t)disp.width * disp.height * sizeof(uint16_t);
    background = heap_caps_malloc(bgBytes, MALLOC_CAP_SPIRAM);
    strip = heap_caps_malloc((size_t)disp.width * STRIP_H * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(background && strip, ESP_ERR_NO_MEM, TAG, "buffers");
    eyeRendererSetStyle(initialStyle);
    return ESP_OK;
}

void eyeRendererDraw(const face_state_t *face) {
    eye_pose_t poses[EYE_MAX_EYES];
    eye_rect_t bounds[EYE_MAX_EYES];
    int eyeCount = style->layout(face, poses, EYE_MAX_EYES);          // Where the eyes are this frame
    for (int i = 0; i < eyeCount; i++) bounds[i] = rectClamp(style->bounds(&poses[i]));

    eye_rect_t dirty[MAX_RECTS];
    int dirtyCount = 0;
    if (fullRedraws > 0) {                                            // Whole screen
        dirty[dirtyCount++] = (eye_rect_t){0, 0, disp.width, disp.height};
        fullRedraws--;
    } else {
        for (int i = 0; i < eyeCount; i++) dirty[dirtyCount++] = bounds[i];        // Where eyes are now
        for (int i = 0; i < lastCount[parity]; i++) dirty[dirtyCount++] = lastRects[parity][i];  // Where they were in this buffer
        dirtyCount = mergeRects(dirty, dirtyCount);
    }

    int64_t t0 = esp_timer_get_time();
    uint16_t *fb = disp.backBuffer();
    for (int i = 0; i < dirtyCount; i++) drawRect(fb, &dirty[i], poses, bounds, eyeCount);
    int64_t t1 = esp_timer_get_time();

    memcpy(lastRects[parity], bounds, eyeCount * sizeof(eye_rect_t)); // Remember what this buffer holds
    lastCount[parity] = eyeCount;
    disp.flip();                                                      // Show it
    parity ^= 1;                                                      // The other buffer is next
    drawUsSum += t1 - t0;                                             // Timing for the log
    flipUsSum += esp_timer_get_time() - t1;
    statFrames++;
}

void eyeRendererStats(uint32_t *drawUs, uint32_t *flipUs) {
    *drawUs = statFrames ? (uint32_t)(drawUsSum / statFrames) : 0;   // Average per frame
    *flipUs = statFrames ? (uint32_t)(flipUsSum / statFrames) : 0;
    drawUsSum = flipUsSum = 0;
    statFrames = 0;
}
