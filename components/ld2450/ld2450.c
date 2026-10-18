// ld2450.c - HLK-LD2450 radar driver: UART frame parser, reader task and gaze source.
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ld2450.h"

#define LD2450_BAUD        256000   // Fixed radar baud rate
#define LD2450_FRAME_LEN   30       // 4 header + 3 x 8 target + 2 tail
#define LD2450_TARGET_LEN  8        // Bytes per target
#define LD2450_RX_BUF      1024     // UART driver RX ring buffer size
#define LD2450_TIMEOUT_MS  500      // No frame for this long = no data
#define LD2450_TASK_STACK  3072     // Reader task stack bytes
#define LD2450_PI          3.14159265f

static const char *TAG = "ld2450";
static const uint8_t ld2450Header[4] = {0xAA, 0xFF, 0x03, 0x00};    // Data frame header
static const uint8_t ld2450Tail[2] = {0x55, 0xCC};                  // Data frame tail

static ld2450_config_t ld2450Cfg;                                   // Copy of the user config
static ld2450_target_t ld2450Latest[LD2450_MAX_TARGETS];            // Latest parsed targets
static int64_t ld2450StampUs = 0;                                   // esp_timer time of the latest frame, 0 = none yet
static portMUX_TYPE ld2450Mux = portMUX_INITIALIZER_UNLOCKED;       // Guards latest + stamp
static bool ld2450Started = false;                                  // true once ld2450Init succeeded

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
    while (true) {
        if (uart_read_bytes(ld2450Cfg.uartNum, &b, 1, pdMS_TO_TICKS(100)) != 1) {   // Wait for a byte
            continue;                                   // Timeout, try again
        }
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
            ESP_LOGW(TAG, "Bad tail, resync");          // Corrupt frame, drop it
            continue;
        }
        ld2450_target_t parsed[LD2450_MAX_TARGETS];     // Parse outside the lock
        ld2450ParseFrame(frame, parsed);
        int64_t now = esp_timer_get_time();
        portENTER_CRITICAL(&ld2450Mux);
        memcpy(ld2450Latest, parsed, sizeof(parsed));   // Publish targets
        ld2450StampUs = now;                            // And their timestamp
        portEXIT_CRITICAL(&ld2450Mux);
    }
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
    if (err != ESP_OK) {
        uart_driver_delete(config->uartNum);            // Roll back on failure
        return err;
    }
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

// Gaze source poll: nearest valid target within range, mapped by bearing
static bool ld2450Poll(uint32_t nowMs, gaze_target_t *out) {
    (void)nowMs;                                        // Uses its own clock
    ld2450_target_t t[LD2450_MAX_TARGETS];
    uint32_t ageMs;
    if (!ld2450Started || out == NULL) return false;
    ld2450GetTargets(t, &ageMs);
    if (ageMs > LD2450_TIMEOUT_MS) return false;        // Stale or no data
    float bestDistMm = ld2450Cfg.maxRangeM * 1000.0f;   // Only accept targets inside range
    int best = -1;                                      // Index of the nearest target
    for (int i = 0; i < LD2450_MAX_TARGETS; i++) {
        if (!t[i].valid) continue;
        float d = hypotf((float)t[i].xMm, (float)t[i].yMm);
        if (d <= bestDistMm) {
            bestDistMm = d;
            best = i;
        }
    }
    if (best < 0) return false;                         // Nobody in range
    float bearingDeg = atan2f((float)t[best].xMm, (float)t[best].yMm) * 180.0f / LD2450_PI;   // 0 = straight ahead
    float gx = (ld2450Cfg.maxAngleDeg > 0.0f) ? bearingDeg / ld2450Cfg.maxAngleDeg : 0.0f;
    if (ld2450Cfg.mirrorX) gx = -gx;                    // Flip for mounting
    if (gx > 1.0f) gx = 1.0f;                           // Clamp to the gaze range
    if (gx < -1.0f) gx = -1.0f;
    out->x = gx;
    out->y = 0.0f;                                      // Radar has no height info
    out->distanceM = bestDistMm / 1000.0f;
    return true;
}

const gaze_source_t gazeSourceLd2450 = {
    .name = "ld2450",
    .init = NULL,                                       // main calls ld2450Init with its config
    .poll = ld2450Poll,
};
