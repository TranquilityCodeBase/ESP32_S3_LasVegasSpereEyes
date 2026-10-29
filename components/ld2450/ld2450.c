// ld2450.c - HLK-LD2450 radar driver: UART frame parser, reader task and gaze source.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ld2450.h"
#include "driver/gpio.h"

#define LD2450_BAUD        256000   // Fixed radar baud rate
#define LD2450_FRAME_LEN   30       // 4 header + 3 x 8 target + 2 tail
#define LD2450_TARGET_LEN  8        // Bytes per target
#define LD2450_RX_BUF      1024     // UART driver RX ring buffer size
#define LD2450_TIMEOUT_MS  500      // No frame for this long = no data
#define LD2450_TASK_STACK  3072     // Reader task stack bytes
#define LD2450_PARKED_MM   300      // Moved less than this for parkedMs = parked (fan, wall echo)
#define LD2450_LOCK_MM     800      // A target this close to where the followed person should be is them
#define LD2450_PI          3.14159265f

static const char *TAG = "ld2450";
static const uint8_t ld2450Header[4] = {0xAA, 0xFF, 0x03, 0x00};    // Data frame header
static const uint8_t ld2450Tail[2] = {0x55, 0xCC};                  // Data frame tail

static ld2450_config_t ld2450Cfg;                                   // Copy of the user config
static ld2450_target_t ld2450Latest[LD2450_MAX_TARGETS];            // Latest parsed targets
static int64_t ld2450StampUs = 0;                                   // esp_timer time of the latest frame, 0 = none yet
static portMUX_TYPE ld2450Mux = portMUX_INITIALIZER_UNLOCKED;       // Guards latest + stamp
static bool ld2450Started = false;                                  // true once ld2450Init succeeded
static ld2450_stats_t ld2450Counts;                                 // Byte and frame counters for wiring checks

// Decode the radar's sign-magnitude int16: bit15 set = positive, clear = negative (per HLK-LD2450 manual example).
// Doubt: this follows the manual's worked examples (0x030E -> -782, 0x86B1 -> +1713); verify the X sign on real hardware.
static int16_t ld2450DecodeSigned(uint16_t raw) {
    int16_t mag = (int16_t)(raw & 0x7FFF);              // Magnitude without the sign bit
    return (raw & 0x8000) ? mag : (int16_t)-mag;        // Bit15 set = positive
}

// Parse one validated 30-byte frame into targets
static void ld2450ParseFrame(const uint8_t *frame, ld2450_target_t out[LD2450_MAX_TARGETS]) {
    for (int i = 0; i < LD2450_MAX_TARGETS; i++) {
        const uint8_t *p = frame + 4 + i * LD2450_TARGET_LEN;   // Start of this target block
        uint16_t rawX = (uint16_t)(p[0] | (p[1] << 8));         // Little-endian fields
        uint16_t rawY = (uint16_t)(p[2] | (p[3] << 8));
        uint16_t rawS = (uint16_t)(p[4] | (p[5] << 8));
        uint16_t res = (uint16_t)(p[6] | (p[7] << 8));
        bool empty = (rawX == 0 && rawY == 0 && rawS == 0 && res == 0);   // All zero = empty slot
        out[i].valid = !empty;
        out[i].xMm = ld2450DecodeSigned(rawX);
        out[i].yMm = ld2450DecodeSigned(rawY);
        out[i].speedCmS = ld2450DecodeSigned(rawS);
        out[i].resMm = res;
    }
}

// Reader task: byte-wise resync on the header, check the tail, publish the frame
static void ld2450Task(void *arg) {
    uint8_t frame[LD2450_FRAME_LEN];    // Frame being assembled
    int idx = 0;                        // Bytes collected so far
    uint8_t b;                          // Current byte
    int16_t heldX[LD2450_MAX_TARGETS] = {0};            // Last position seen per slot
    int16_t heldY[LD2450_MAX_TARGETS] = {0};
    int64_t changedUs[LD2450_MAX_TARGETS] = {0};        // When that slot's position last changed
    int16_t anchorX[LD2450_MAX_TARGETS] = {0};          // Where each slot was when it last moved a real distance
    int16_t anchorY[LD2450_MAX_TARGETS] = {0};
    int64_t movedUs[LD2450_MAX_TARGETS] = {0};          // When that was
    while (true) {
        if (uart_read_bytes(ld2450Cfg.uartNum, &b, 1, pdMS_TO_TICKS(100)) != 1) {   // Wait for a byte
            continue;                                   // Timeout, try again
        }
        ld2450Counts.bytes++;                           // Any byte at all proves the radar's TX wire reaches us
        if (idx < 4) {                                  // Still matching the header
            if (b == ld2450Header[idx]) {
                frame[idx++] = b;                       // Header byte matches
            } else if (b == ld2450Header[0]) {
                frame[0] = b;                           // Mismatch, but this byte may start a new header
                idx = 1;
            } else {
                idx = 0;                                // Lost sync, keep hunting
            }
            continue;
        }
        frame[idx++] = b;                               // Payload or tail byte
        if (idx < LD2450_FRAME_LEN) continue;           // Frame not complete yet
        idx = 0;                                        // Next byte starts a new frame
        if (frame[28] != ld2450Tail[0] || frame[29] != ld2450Tail[1]) {
            ld2450Counts.badFrames++;                   // Corrupt frame, drop it (counted, not logged: 10 frames a second)
            continue;
        }
        ld2450_target_t parsed[LD2450_MAX_TARGETS];     // Parse outside the lock
        ld2450ParseFrame(frame, parsed);
        int64_t now = esp_timer_get_time();
        // A real person's reading jitters a few mm every frame. When someone walks out of view the radar keeps
        // repeating their last position, so a reading that hasn't changed at all for a while is a held track.
        for (int i = 0; i < LD2450_MAX_TARGETS; i++) {
            if (parsed[i].xMm != heldX[i] || parsed[i].yMm != heldY[i]) {
                heldX[i] = parsed[i].xMm;               // Moved: remember where and when
                heldY[i] = parsed[i].yMm;
                changedUs[i] = now;
            }
            parsed[i].held = ld2450Cfg.heldMs > 0 && (now - changedUs[i]) > ld2450Cfg.heldMs * 1000LL;
            // Clutter (a fan, an echo off a wall) jitters but stays put. People walk somewhere.
            if (hypotf((float)(parsed[i].xMm - anchorX[i]), (float)(parsed[i].yMm - anchorY[i])) > LD2450_PARKED_MM) {
                anchorX[i] = parsed[i].xMm;
                anchorY[i] = parsed[i].yMm;
                movedUs[i] = now;
            }
            parsed[i].parked = ld2450Cfg.parkedMs > 0 && (now - movedUs[i]) > ld2450Cfg.parkedMs * 1000LL;
        }
        if (ld2450Cfg.trace) {                          // One line per frame for the PC capture script
            printf("RT %lld", now / 1000);              // Frame time, ms since boot
            for (int i = 0; i < LD2450_MAX_TARGETS; i++) {
                if (!parsed[i].valid) printf(" -");     // Empty slot
                else printf(" %d,%d,%d,%u", parsed[i].xMm, parsed[i].yMm, parsed[i].speedCmS, parsed[i].resMm);
            }
            printf("\n");
        }
        portENTER_CRITICAL(&ld2450Mux);
        memcpy(ld2450Latest, parsed, sizeof(parsed));   // Publish targets
        ld2450StampUs = now;                            // And their timestamp
        ld2450Counts.goodFrames++;
        portEXIT_CRITICAL(&ld2450Mux);
    }
}

// ---------- Radar settings over the serial link (Hi-Link LD2450 serial command protocol) ----------
// Command frame: FD FC FB FA, length (2 bytes, of command + value), command (2), value, 04 03 02 01.
// The radar answers with the same header, command | 0x0100, and a status word (0 = done).

// Send one command and wait for its answer. reply gets the answer's value bytes after the status word.
static bool ld2450Command(uint16_t cmd, const uint8_t *value, int valueLen, uint8_t *reply, int replyMax) {
    static const uint8_t head[4] = {0xFD, 0xFC, 0xFB, 0xFA};
    static const uint8_t tail[4] = {0x04, 0x03, 0x02, 0x01};
    uint8_t out[48];
    int n = 0;
    memcpy(out, head, 4);
    n = 4;
    out[n++] = (uint8_t)(2 + valueLen);                                 // Length of command + value
    out[n++] = 0;
    out[n++] = (uint8_t)cmd;
    out[n++] = (uint8_t)(cmd >> 8);
    if (valueLen > 0) {
        memcpy(out + n, value, valueLen);
        n += valueLen;
    }
    memcpy(out + n, tail, 4);
    n += 4;
    uart_flush_input(ld2450Cfg.uartNum);
    uart_write_bytes(ld2450Cfg.uartNum, out, n);

    // Hunt for the answer header among the data frames still streaming in
    int64_t deadline = esp_timer_get_time() + 500000;
    int matched = 0;
    int seen = 0;                                                       // Bytes received while waiting, for the log
    uint8_t b;
    while (esp_timer_get_time() < deadline) {
        if (uart_read_bytes(ld2450Cfg.uartNum, &b, 1, pdMS_TO_TICKS(20)) != 1) continue;
        seen++;
        matched = (b == head[matched]) ? matched + 1 : (b == head[0] ? 1 : 0);
        if (matched < 4) continue;
        uint8_t hdr[6];                                                 // Length, command | 0x0100, status
        if (uart_read_bytes(ld2450Cfg.uartNum, hdr, 6, pdMS_TO_TICKS(50)) != 6) return false;
        int len = hdr[0] | (hdr[1] << 8);
        uint16_t ackCmd = (uint16_t)(hdr[2] | (hdr[3] << 8));
        uint16_t status = (uint16_t)(hdr[4] | (hdr[5] << 8));
        uint8_t rest[40];
        int restLen = len - 4;                                          // Value bytes after the status word
        if (restLen < 0 || restLen > (int)sizeof(rest)) return false;
        if (restLen > 0 && uart_read_bytes(ld2450Cfg.uartNum, rest, restLen, pdMS_TO_TICKS(50)) != restLen) return false;
        if (reply != NULL) memcpy(reply, rest, restLen < replyMax ? restLen : replyMax);
        if (ackCmd != (cmd | 0x0100) || status != 0) {
            ESP_LOGW(TAG, "Command 0x%04x answered 0x%04x status %u", cmd, ackCmd, status);
            return false;
        }
        return true;
    }
    ESP_LOGW(TAG, "Command 0x%04x: no answer (%d other bytes came in)", cmd, seen);
    return false;                                                       // No answer: radar RX wire, or old firmware
}

static void ld2450PutI16(uint8_t *p, int16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)((uint16_t)v >> 8);
}

static int16_t ld2450GetI16(const uint8_t *p) {
    return (int16_t)(p[0] | (p[1] << 8));
}

// Send targetMode and the zone from the config. The radar remembers both, so -1 keeps what was set last time.
static void ld2450ApplySettings(void) {
    const ld2450_config_t *c = &ld2450Cfg;
    if (c->targetMode < 0 && c->zoneType < 0) return;                  // Nothing to change
    const uint8_t enable[2] = {0x01, 0x00};
    if (!ld2450Command(0x00FF, enable, 2, NULL, 0)) {
        ESP_LOGW(TAG, "Radar did not answer the config command (radar RX wired to GPIO%d?)", c->txPin);
        ld2450Command(0x00FE, NULL, 0, NULL, 0);                       // Make sure it isn't left in config mode
        return;
    }
    if (c->targetMode >= 0) {
        bool ok = ld2450Command(c->targetMode == 1 ? 0x0080 : 0x0090, NULL, 0, NULL, 0);
        ESP_LOGI(TAG, "%s-target mode: %s", c->targetMode == 1 ? "Single" : "Multi", ok ? "set" : "FAILED");
    }
    if (c->zoneType >= 0) {
        uint8_t v[26] = {0};                                            // Type, then 3 rectangles of x1 y1 x2 y2
        ld2450PutI16(v, (int16_t)c->zoneType);
        ld2450PutI16(v + 2, c->zoneX1Mm);
        ld2450PutI16(v + 4, c->zoneY1Mm);
        ld2450PutI16(v + 6, c->zoneX2Mm);
        ld2450PutI16(v + 8, c->zoneY2Mm);                               // Rectangles 2 and 3 stay empty
        bool ok = ld2450Command(0x00C2, v, sizeof(v), NULL, 0);
        uint8_t back[26] = {0};
        bool read = ld2450Command(0x00C1, NULL, 0, back, sizeof(back)); // Read it back to be sure
        ESP_LOGI(TAG, "Zone %s: type %d, x %d..%d, y %d..%d mm. Radar now has type %d, x %d..%d, y %d..%d%s",
                 ok ? "set" : "FAILED", c->zoneType, c->zoneX1Mm, c->zoneX2Mm, c->zoneY1Mm, c->zoneY2Mm,
                 ld2450GetI16(back), ld2450GetI16(back + 2), ld2450GetI16(back + 6), ld2450GetI16(back + 4),
                 ld2450GetI16(back + 8), read ? "" : " (read back FAILED)");
    }
    ld2450Command(0x00FE, NULL, 0, NULL, 0);                           // Back to reporting targets
}

esp_err_t ld2450Init(const ld2450_config_t *config) {
    if (config == NULL) return ESP_ERR_INVALID_ARG;     // Config is required
    if (ld2450Started) return ESP_ERR_INVALID_STATE;    // Only once
    ld2450Cfg = *config;                                // Keep our own copy
    uart_config_t uartCfg = {
        .baud_rate = LD2450_BAUD,                       // 256000 baud
        .data_bits = UART_DATA_8_BITS,                  // 8N1
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_driver_install(config->uartNum, LD2450_RX_BUF, 0, 0, NULL, 0);   // RX buffer only
    if (err != ESP_OK) return err;
    err = uart_param_config(config->uartNum, &uartCfg);
    if (err == ESP_OK) err = uart_set_pin(config->uartNum, config->txPin, config->rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err == ESP_OK) err = gpio_set_pull_mode(config->rxPin, GPIO_PULLUP_ONLY);   // A loose RX wire reads idle, not noise
    if (err != ESP_OK) {
        uart_driver_delete(config->uartNum);            // Roll back on failure
        return err;
    }
    ld2450ApplySettings();                              // Before the reader task takes over the UART
    if (xTaskCreate(ld2450Task, "ld2450", LD2450_TASK_STACK, NULL, 5, NULL) != pdPASS) {
        uart_driver_delete(config->uartNum);
        return ESP_ERR_NO_MEM;
    }
    ld2450Started = true;
    ESP_LOGI(TAG, "Started on UART%d rx=%d tx=%d", (int)config->uartNum, config->rxPin, config->txPin);
    return ESP_OK;
}

int ld2450GetTargets(ld2450_target_t out[LD2450_MAX_TARGETS], uint32_t *ageMs) {
    int64_t stamp;                                      // Local copy of the timestamp
    portENTER_CRITICAL(&ld2450Mux);
    memcpy(out, ld2450Latest, sizeof(ld2450Latest));    // Copy latest frame
    stamp = ld2450StampUs;
    portEXIT_CRITICAL(&ld2450Mux);
    if (ageMs != NULL) {
        // No frame yet reports a huge age
        *ageMs = (stamp == 0) ? UINT32_MAX : (uint32_t)((esp_timer_get_time() - stamp) / 1000);
    }
    int count = 0;                                      // Number of valid targets
    for (int i = 0; i < LD2450_MAX_TARGETS; i++) {
        if (out[i].valid) count++;
    }
    return count;
}

void ld2450GetStats(ld2450_stats_t *out) {
    if (out == NULL) return;
    portENTER_CRITICAL(&ld2450Mux);
    *out = ld2450Counts;                                // Counters only grow, a slightly old copy is fine
    portEXIT_CRITICAL(&ld2450Mux);
}

// Gaze source poll. Follows one person: who we follow keeps being followed until they are gone for
// lockLostMs, so a second person nearer the radar doesn't make the eyes flick between them.
// Only people who have moved recently count, and the eyes aim slightly ahead of them to hide the radar's delay.
static bool ld2450Poll(uint32_t nowMs, gaze_target_t *out) {
    static int64_t seenStampUs;                         // Radar frame already used
    static bool locked;                                 // Following someone
    static float lockX, lockY;                          // Their position, mm
    static float lockVx;                                // Their sideways speed, mm/s (smoothed)
    static int64_t lockSeenUs;                          // When they were last matched
    static int64_t tracedStampUs;                       // Frame whose gaze was last traced
    (void)nowMs;                                        // Uses the radar's own frame times
    if (!ld2450Started || out == NULL) return false;
    ld2450_target_t t[LD2450_MAX_TARGETS];
    int64_t stamp;
    portENTER_CRITICAL(&ld2450Mux);
    memcpy(t, ld2450Latest, sizeof(t));
    stamp = ld2450StampUs;
    portEXIT_CRITICAL(&ld2450Mux);
    int64_t now = esp_timer_get_time();
    if (stamp == 0 || now - stamp > LD2450_TIMEOUT_MS * 1000LL) return false;   // No data

    if (stamp != seenStampUs) {                         // New radar frame: update who we follow
        float maxMm = ld2450Cfg.maxRangeM * 1000.0f;
        float minMm = ld2450Cfg.minRangeM * 1000.0f;
        float predX = lockX + lockVx * (stamp - seenStampUs) / 1e6f;   // Where the followed person should be now
        int best = -1;                                  // Candidate to follow
        float bestScore = 1e9f;
        for (int i = 0; i < LD2450_MAX_TARGETS; i++) {
            if (!t[i].valid) continue;                                          // Empty slot
            if (t[i].held || t[i].parked) continue;                             // Lost track, or clutter (when those filters are on)
            float distMm = hypotf((float)t[i].xMm, (float)t[i].yMm);
            if (distMm > maxMm || distMm < minMm) continue;                  // Too far away, or too close to be a passer-by
            float score = locked ? hypotf(t[i].xMm - predX, t[i].yMm - lockY)   // Nearest to the followed person
                                 : hypotf((float)t[i].xMm, (float)t[i].yMm);    // Or simply nearest to the radar
            if (locked && score > LD2450_LOCK_MM) continue;                    // Someone else
            if (score < bestScore) {
                bestScore = score;
                best = i;
            }
        }
        if (best >= 0) {
            float dt = (stamp - seenStampUs) / 1e6f;
            if (locked && dt > 0.0f && dt < 0.5f) {
                // Smooth out the radar's jitter (about 10 cm on someone standing still at 2 m)
                float a = (ld2450Cfg.smoothMs > 0) ? 1.0f - expf(-dt * 1000.0f / ld2450Cfg.smoothMs) : 1.0f;
                float newX = lockX + a * (t[best].xMm - lockX);
                lockY += a * (t[best].yMm - lockY);
                lockVx = 0.6f * lockVx + 0.4f * (newX - lockX) / dt;   // Smoothed sideways speed
                lockX = newX;
            } else {
                lockVx = 0.0f;                          // New person: start where they are, no speed yet
                lockX = t[best].xMm;
                lockY = t[best].yMm;
            }
            lockSeenUs = stamp;
            locked = true;
        } else if (locked && stamp - lockSeenUs > ld2450Cfg.lockLostMs * 1000LL) {
            locked = false;                             // Gone: next frame picks whoever is nearest
        }
        seenStampUs = stamp;
    }
    if (!locked || stamp - lockSeenUs > ld2450Cfg.lockLostMs * 1000LL) return false;

    float aimX = lockX + lockVx * ld2450Cfg.leadMs / 1000.0f;  // A little ahead of the person
    float bearingDeg = atan2f(aimX, lockY) * 180.0f / LD2450_PI;   // 0 = straight ahead
    float gx = (ld2450Cfg.maxAngleDeg > 0.0f) ? bearingDeg / ld2450Cfg.maxAngleDeg : 0.0f;
    if (ld2450Cfg.mirrorX) gx = -gx;                    // Flip for mounting
    if (gx > 1.0f) gx = 1.0f;                           // Clamp to the gaze range
    if (gx < -1.0f) gx = -1.0f;
    if (ld2450Cfg.trace && stamp == seenStampUs && stamp != tracedStampUs) {
        printf("RG %lld %.3f\n", stamp / 1000, gx);    // Where this frame points the eyes, for the timing check
        tracedStampUs = stamp;
    }
    out->x = gx;
    out->y = 0.0f;                                      // Radar has no height info
    out->distanceM = hypotf(lockX, lockY) / 1000.0f;
    return true;
}

const gaze_source_t gazeSourceLd2450 = {
    .name = "ld2450",
    .init = NULL,                                       // main calls ld2450Init with its config
    .poll = ld2450Poll,
};
