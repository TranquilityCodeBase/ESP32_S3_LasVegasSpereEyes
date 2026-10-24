// app_main.c - Las Vegas Sphere eyeball: brings up the display, picks gaze inputs, and runs the render loop.
#include <stdio.h>

#include "app_config.h"
#include "blink_effect.h"
#include "board_backlight.h"
#include "board_display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "eye_animator.h"
#include "eye_canvas.h"
#include "eye_renderer.h"
#include "eye_style.h"
#include "eye_sync.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gaze_source.h"
#include "ld2450.h"
#include "nvs_flash.h"

// Real secrets live in the gitignored secrets.h. Without it the example placeholders are used.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets_example.h"
#endif

static const char *TAG = "app";

static const gaze_source_t *gazeSources[3];     // Priority order, idle last
static int gazeSourceCount;

// Designs to rotate through, in order. Add a new style here (and in eye_style.h). Append only: the slave
// gets the position in this list over ESP-NOW, so both boards must be flashed with the same list.
static const eye_style_t *const stylePlaylist[] = {
    &eyeStyleSphere,        // Las Vegas Sphere cartoon eye
    &eyeStyleSauron,        // Flaming dark-lord eye
    &eyeStyleSphereDrunk,   // Bloodshot, droopy Sphere eye
};
#define STYLE_COUNT ((int)(sizeof(stylePlaylist) / sizeof(stylePlaylist[0])))

static int styleIndex;                          // Design on screen now
static uint32_t styleStartMs;                   // When it went up
static uint32_t styleSwitchAtMs;                // Slave: when the master will switch to the next design (0 = not known)
static uint32_t styleSettleUntilMs;             // Slave: just switched on time, don't correct from older packets until then

static uint32_t nowMs(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// Put a design on screen (redraws the whole screen once)
static void showStyle(int index) {
    styleIndex = index % STYLE_COUNT;
    styleStartMs = nowMs();
    eyeRendererSetStyle(stylePlaylist[styleIndex]);
}

// Time left on the current design, ms (at least 1), or 0 when designs don't rotate
static uint16_t styleMsLeft(void) {
    if (APP_STYLE_SECONDS == 0 || STYLE_COUNT < 2) return 0;
    int32_t left = (int32_t)(APP_STYLE_SECONDS * 1000u) - (int32_t)(nowMs() - styleStartMs);
    return left < 1 ? 1 : (left > 65535 ? 65535 : (uint16_t)left);
}

// Next design every APP_STYLE_SECONDS. The master keeps its own time. The slave switches at the moment
// the master's countdown says, so both change on the same frame instead of the slave lagging behind.
static void styleStep(void) {
    if (APP_SYNC_ROLE == EYE_SYNC_ROLE_FOLLOWER) {
        if (styleSwitchAtMs != 0 && (int32_t)(nowMs() - styleSwitchAtMs) >= 0) {
            styleSwitchAtMs = 0;
            showStyle(styleIndex + 1);
            styleSettleUntilMs = nowMs() + 1000;                        // Packets sent just before the switch are stale
        }
        return;
    }
    if (styleMsLeft() == 1) showStyle(styleIndex + 1);
}

// Sync: the leader sends its state, the follower copies blinks and the design (gaze comes in through gazeSourceSync)
static void syncStep(void) {
    static uint32_t leaderSeqSeen;                                      // Last leader blink we copied
    eye_sync_state_t state = {0};
    if (APP_SYNC_ROLE == EYE_SYNC_ROLE_LEADER) {
        gaze_target_t target;
        state.hasTarget = eyeAnimatorTracking(&target);
        state.gazeX = target.x;
        state.gazeY = target.y;
        state.blinkSeq = eyeAnimatorBlinkSeq(&state.blinkIndex);
        state.styleIndex = (uint8_t)styleIndex;
        state.styleMsLeft = styleMsLeft();
        eyeSyncPublish(&state);
    } else if (APP_SYNC_ROLE == EYE_SYNC_ROLE_FOLLOWER) {
        bool linked = eyeSyncGetLatest(&state, 1000);                  // Gaze comes in through gazeSourceSync
        if (linked && state.blinkSeq != leaderSeqSeen) {                // Master started a blink: copy it
            if (leaderSeqSeen != 0) {                                   // Skip the first message after boot
                eyeAnimatorStartBlink(state.blinkIndex);
                ESP_LOGI(TAG, "Blink from master: %s", blinkEffectTable[state.blinkIndex % blinkEffectCount]->name);
            }
            leaderSeqSeen = state.blinkSeq;
        }
        if (linked) {
            uint32_t now = nowMs();
            if (state.styleMsLeft > 0 && state.styleIndex % STYLE_COUNT == styleIndex) {
                styleSwitchAtMs = now + state.styleMsLeft - state.ageMs;  // Master's countdown, minus the time in flight
                if (styleSwitchAtMs == 0) styleSwitchAtMs = 1;          // 0 means "not known"
            }
            if (state.styleIndex % STYLE_COUNT != styleIndex && (int32_t)(now - styleSettleUntilMs) >= 0) {
                styleSwitchAtMs = 0;                                    // Out of step (boot, missed packets): catch up now
                showStyle(state.styleIndex);
            }
        }
    }
}

#if APP_DEBUG_FRAME_DUMP
// Debug: print the on-screen frame, downsampled, as hex over the console, plus the face state
static void dumpFrame(const face_state_t *face) {
    const int step = 6;                                                 // 480 / 6 = 80 x 80 samples
    uint16_t *fb = boardDisplayFrontBuffer();
    printf("FRAME %d %d gaze %.3f %.3f open %.2f %.2f\n", BOARD_LCD_H_RES / step, BOARD_LCD_V_RES / step,
           face->gazeX, face->gazeY, face->openLeft, face->openRight);
    for (int y = 0; y < BOARD_LCD_V_RES; y += step) {
        for (int x = 0; x < BOARD_LCD_H_RES; x += step) printf("%04x", fb[y * BOARD_LCD_H_RES + x]);
        printf("\n");
    }
    printf("ENDFRAME\n");
}
#endif

static void renderTask(void *arg) {
    // The panel is created here, on the render core, so its refresh interrupt runs on this core too.
    // On core 0 the Wi-Fi interrupts delayed it and the picture slipped up with a band at the bottom.
    ESP_ERROR_CHECK(boardDisplayInit());
    boardBacklightSet(APP_BACKLIGHT_PERCENT);                           // Per-board brightness
    eye_display_t display = {
        .width = BOARD_LCD_H_RES,
        .height = BOARD_LCD_V_RES,
        .backBuffer = boardDisplayBackBuffer,
        .flip = boardDisplayFlip,
    };
    canvasSetColorTrim(APP_COLOR_TRIM_R, APP_COLOR_TRIM_G, APP_COLOR_TRIM_B, APP_GRAY_GAMMA);   // Match this screen to the other one
    canvasSetWhiteBalance(APP_WHITE_R, APP_WHITE_G, APP_WHITE_B);
    ESP_ERROR_CHECK(eyeRendererInit(&display, stylePlaylist[0]));
    for (int i = 1; i < STYLE_COUNT; i++) eyeRendererPrepare(stylePlaylist[i]);   // Draw every background now, so switches are instant
    styleStartMs = nowMs();

    const TickType_t framePeriod = pdMS_TO_TICKS(1000 / APP_TARGET_FPS);
    TickType_t lastWake = xTaskGetTickCount();
    uint32_t fpsStartMs = nowMs();
    int frames = 0;
    face_state_t face;
    while (true) {
        styleStep();                                                    // Time for the next design?
        syncStep();
        eyeAnimatorUpdate(nowMs(), &face);                              // Where to look, how open
        face.isLeftEye = APP_EYE_IS_LEFT;                               // Which eye of the pair this board draws
        eyeRendererDraw(&face);                                         // Draw and flip
#if APP_DEBUG_FRAME_DUMP
        if (frames % (APP_TARGET_FPS * 3) == 0) dumpFrame(&face);       // Every 3 s
#endif
        if (++frames == APP_TARGET_FPS * 10) {                          // Frame rate in the log every 10 s
            uint32_t elapsed = nowMs() - fpsStartMs;
            uint32_t drawUs, flipUs, drawMaxUs;
            eyeRendererStats(&drawUs, &flipUs, &drawMaxUs);
            ESP_LOGI(TAG, "%.1f fps, draw %lu us (max %lu), flip wait %lu us", frames * 1000.0f / elapsed,
                     (unsigned long)drawUs, (unsigned long)drawMaxUs, (unsigned long)flipUs);
            frames = 0;
            fpsStartMs = nowMs();
        }
        vTaskDelayUntil(&lastWake, framePeriod);                        // Steady pace
    }
}

void app_main(void) {
    esp_err_t err = nvs_flash_init();                                   // Wi-Fi (ESP-NOW) needs NVS
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Optional ESP-NOW link to the other board
    static const uint8_t pmk[16] = SECRET_SYNC_PMK;
    static const uint8_t lmk[16] = SECRET_SYNC_LMK;
    static const uint8_t peerMac[6] = SECRET_SYNC_PEER_MAC;
    eye_sync_config_t syncConfig = {
        .role = APP_SYNC_ROLE,
        .channel = APP_SYNC_CHANNEL,
        .groupId = APP_SYNC_GROUP_ID,
        .peerMac = SECRET_SYNC_USE_PEER_MAC ? peerMac : NULL,
        .pmk = SECRET_SYNC_USE_KEYS ? pmk : NULL,
        .lmk = SECRET_SYNC_USE_KEYS ? lmk : NULL,
    };
    if (eyeSyncInit(&syncConfig) != ESP_OK) ESP_LOGW(TAG, "ESP-NOW sync failed to start");
    if (APP_SYNC_ROLE == EYE_SYNC_ROLE_FOLLOWER) gazeSources[gazeSourceCount++] = &gazeSourceSync;

#if APP_RADAR_ENABLED
    ld2450_config_t radarConfig = {
        .uartNum = APP_RADAR_UART,
        .rxPin = APP_RADAR_RX_PIN,
        .txPin = APP_RADAR_TX_PIN,
        .mirrorX = APP_RADAR_MIRROR_X,
        .maxAngleDeg = APP_RADAR_MAX_ANGLE,
        .maxRangeM = APP_RADAR_MAX_RANGE_M,
    };
    if (ld2450Init(&radarConfig) == ESP_OK) gazeSources[gazeSourceCount++] = &gazeSourceLd2450;
    else ESP_LOGW(TAG, "LD2450 failed to start");
#endif
    // Always last. Master: look around when nobody is there. Slave: hold center until the master says otherwise.
    gazeSources[gazeSourceCount++] = (APP_SYNC_ROLE == EYE_SYNC_ROLE_FOLLOWER) ? &gazeSourceCenter : &gazeSourceIdle;
    if (APP_SYNC_ROLE == EYE_SYNC_ROLE_FOLLOWER) eyeAnimatorSetAutoBlink(false);   // Slave blinks only on command
    eyeAnimatorInit(gazeSources, gazeSourceCount);

    xTaskCreatePinnedToCore(renderTask, "render", 6144, NULL, APP_RENDER_PRIORITY, NULL, APP_RENDER_CORE);
}
