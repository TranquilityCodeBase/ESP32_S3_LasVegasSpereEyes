// eye_renderer.c - Dirty-rectangle renderer over two page-flipped frame buffers.
// The background never changes, so each frame only redraws the eyes' area: the area they cover now plus
// the area they covered two frames ago (the last time this buffer was drawn). Work happens in small
// internal-RAM strips, then each strip is copied to the PSRAM frame buffer. Both CPU cores draw:
// the calling task takes even strips, a helper task on the other core takes odd strips.
#include "eye_renderer.h"

#include <string.h>
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define STRIP_H     24                  // Strip height in lines (internal RAM: width x 24 x 2 bytes per core)
#define HELPER_CORE 0                   // Core for the helper task (the render task runs on the other one)
#define HELPER_PRIO 5
#define MAX_RECTS   (EYE_MAX_EYES * 2)  // Current + stale rectangles

static const char *TAG = "renderer";

static eye_display_t disp;              // Display hooks
static const eye_style_t *style;        // Current look
static uint16_t *background;            // Static background, full screen, PSRAM
static uint16_t *strips[2];             // Work strip per core, internal RAM
static int fullRedraws;                 // Frames left that must redraw the whole screen
static int parity;                      // Which of the two buffers is the back buffer now
static eye_pose_t lastPoses[2][EYE_MAX_EYES];  // Eyes as last drawn into each buffer
static int lastCount[2];
static int64_t drawUsSum, flipUsSum, drawUsMax;      // Timing totals for eyeRendererStats
static uint32_t statFrames;

// Work shared with the helper task for one frame
typedef struct {
    uint16_t *fb;                           // Frame buffer being drawn
    const eye_rect_t *rects;                // Dirty rectangles
    int rectCount;
    const eye_pose_t *poses;                // Eyes and their bounds
    const eye_rect_t *bounds;
    int eyeCount;
} frame_job_t;

static frame_job_t job;                     // Current frame's work
static TaskHandle_t helperTask;             // Draws the odd strips
static SemaphoreHandle_t helperDone;        // Given when the helper finished its strips

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

// true when two poses differ only in the pupil (position or size)
static bool sameBody(const eye_pose_t *a, const eye_pose_t *b) {
    return a->cx == b->cx && a->cy == b->cy && a->rx == b->rx && a->ry == b->ry &&
           a->openness == b->openness && a->isLeft == b->isLeft;
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

// Draw this worker's share of the frame: every strip whose running number % 2 == worker.
// Each strip gets the background, then every eye that touches it, then goes to the frame buffer.
static void drawStrips(int worker) {
    uint16_t *strip = strips[worker];
    int stripNum = 0;                                                  // Counts strips across all rectangles
    for (int r = 0; r < job.rectCount; r++) {
        const eye_rect_t *rect = &job.rects[r];
        int w = rect->x1 - rect->x0;
        if (w <= 0) continue;
        for (int sy = rect->y0; sy < rect->y1; sy += STRIP_H, stripNum++) {
            if ((stripNum & 1) != worker) continue;                    // The other core's strip
            int h = (rect->y1 - sy < STRIP_H) ? rect->y1 - sy : STRIP_H;
            eye_canvas_t cv = {strip, rect->x0, sy, w, h};
            for (int y = 0; y < h; y++) {                              // Background rows into the strip
                memcpy(strip + y * w, background + (sy + y) * disp.width + rect->x0, w * sizeof(uint16_t));
            }
            eye_rect_t stripRect = {rect->x0, sy, rect->x1, sy + h};
            for (int i = 0; i < job.eyeCount; i++) {                   // Eyes that reach this strip
                if (rectsOverlap(&job.bounds[i], &stripRect)) style->drawEye(&cv, &job.poses[i]);
            }
            for (int y = 0; y < h; y++) {                              // Strip rows into the frame buffer
                memcpy(job.fb + (sy + y) * disp.width + rect->x0, strip + y * w, w * sizeof(uint16_t));
            }
        }
    }
}

// Helper task on the other core: waits for a frame, draws the odd strips, reports back
static void helperLoop(void *arg) {
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        drawStrips(1);
        xSemaphoreGive(helperDone);
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
    for (int i = 0; i < 2; i++) {
        strips[i] = heap_caps_malloc((size_t)disp.width * STRIP_H * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    ESP_RETURN_ON_FALSE(background && strips[0] && strips[1], ESP_ERR_NO_MEM, TAG, "buffers");
    helperDone = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(xTaskCreatePinnedToCore(helperLoop, "render2", 4096, NULL, HELPER_PRIO, &helperTask, HELPER_CORE) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "helper task");
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
        const eye_pose_t *old = lastPoses[parity];                    // What this buffer holds now
        for (int i = 0; i < eyeCount; i++) {
            if (i < lastCount[parity] && style->detailBounds && sameBody(&poses[i], &old[i])) {
                // Eye body unchanged since this buffer was drawn: only the pupil area needs redrawing
                dirty[dirtyCount++] = rectClamp(style->detailBounds(&poses[i]));
                dirty[dirtyCount++] = rectClamp(style->detailBounds(&old[i]));
            } else {
                dirty[dirtyCount++] = bounds[i];                      // Where the eye is now
                if (i < lastCount[parity]) dirty[dirtyCount++] = rectClamp(style->bounds(&old[i]));   // Where it was
            }
        }
        for (int i = eyeCount; i < lastCount[parity]; i++) dirty[dirtyCount++] = rectClamp(style->bounds(&old[i]));  // Eyes gone
        dirtyCount = mergeRects(dirty, dirtyCount);
    }

    int64_t t0 = esp_timer_get_time();
    job = (frame_job_t){disp.backBuffer(), dirty, dirtyCount, poses, bounds, eyeCount};
    xTaskNotifyGive(helperTask);                                      // Other core starts on the odd strips
    drawStrips(0);                                                    // This core does the even strips
    xSemaphoreTake(helperDone, portMAX_DELAY);                        // Wait until the frame is complete
    int64_t t1 = esp_timer_get_time();

    memcpy(lastPoses[parity], poses, eyeCount * sizeof(eye_pose_t));  // Remember what this buffer holds
    lastCount[parity] = eyeCount;
    disp.flip();                                                      // Show it
    parity ^= 1;                                                      // The other buffer is next
    drawUsSum += t1 - t0;                                             // Timing for the log
    if (t1 - t0 > drawUsMax) drawUsMax = t1 - t0;
    flipUsSum += esp_timer_get_time() - t1;
    statFrames++;
}

void eyeRendererStats(uint32_t *drawUs, uint32_t *flipUs, uint32_t *drawMaxUs) {
    *drawMaxUs = (uint32_t)drawUsMax;                                 // Slowest frame
    drawUsMax = 0;
    *drawUs = statFrames ? (uint32_t)(drawUsSum / statFrames) : 0;   // Average per frame
    *flipUs = statFrames ? (uint32_t)(flipUsSum / statFrames) : 0;
    drawUsSum = flipUsSum = 0;
    statFrames = 0;
}
